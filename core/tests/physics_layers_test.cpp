// Host-side checks for the physics layers' pure maths (core/viz/physics/PhysicsPlan): the plan, the layer order,
// the off case, the music clock and the params that carry them. Built and run by CI ("Native host tests" in
// .github/workflows/native-host-tests.yml) with the system compiler; no GL, no device.
//
// What is pinned: every look's defaults plan nothing (the frame path is the one it was, nothing allocated); the
// plan's grids and population are fixed, live and in an export alike; the composite at a fluid mix of 0 is the
// style bit for bit, the fluid sits behind the style's light or is screened over it, and the particles in front
// never take the frame past white; the dye, the stir and the keep window step on the music clock alone, so
// silence holds them and the same frames replay the same layers; the four params are modulation targets the
// safety rules hold.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "viz/Lfo.hpp"
#include "viz/Params.hpp"
#include "viz/VisualSafety.hpp"
#include "viz/physics/PhysicsPlan.hpp"

using geode::viz::LfoTarget;
using geode::viz::SceneParams;
namespace physics = geode::viz::physics;
namespace safety = geode::viz::safety;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

SceneParams with(float fluid, float particles) {
    SceneParams p;
    p.simFluid = fluid;
    p.simParticles = particles;
    return p;
}

void everyLookPlansNothing() {
    const physics::Plan plan = physics::planFor(SceneParams{});
    check(!plan.any() && !plan.fluid && !plan.particles, "a look's defaults plan no physics layer");
    check(plan.simRes == 0 && plan.dyeRes == 0 && plan.particleSide == 0, "and ask for no grid and no particles");
    check(!physics::planFor(with(physics::kOn * 0.5f, physics::kOn * 0.5f)).any(),
          "a mix under the floor draws nothing");
    check(!physics::planFor(with(std::nanf(""), -1.0f)).any(), "a NaN or negative mix is off");
    // The force and the fluid's place alone turn nothing on.
    SceneParams p;
    p.simForce = 2.0f;
    p.simLayerOrder = 1.0f;
    check(!physics::planFor(p).any(), "force and place without a mix plan nothing");
}

void bothLayersArePlanned() {
    const SceneParams both = with(1.0f, 1.0f);
    const physics::Plan full = physics::planFor(both);
    check(full.fluid && full.particles && full.fluidMix == 1.0f && full.particleMix == 1.0f, "both layers run");
    check(full.particleSide > 0, "the particles have a population");
    check(full.simRes > 0 && full.dyeRes > full.simRes && full.pressureIterations > 0, "the fluid's grids are sized");
    // A grid larger than the fluid styles' Low rung would cost the phone a second fluid scene.
    check(full.simRes <= 96 && full.dyeRes <= 384, "the layer is a low-res fluid");
    const physics::Plan particlesOnly = physics::planFor(with(0.0f, 0.5f));
    check(particlesOnly.particles && !particlesOnly.fluid && particlesOnly.simRes == 0, "particles alone run no fluid");
    const physics::Plan fluidOnly = physics::planFor(with(0.5f, 0.0f));
    check(fluidOnly.fluid && !fluidOnly.particles && fluidOnly.particleSide == 0, "the fluid alone draws no particles");
    SceneParams wild = with(4.0f, 9.0f);
    wild.simForce = 7.0f;
    wild.simLayerOrder = -3.0f;
    const physics::Plan held = physics::planFor(wild);
    check(held.fluidMix == 1.0f && held.particleMix == 1.0f && held.force == 2.0f && held.order == 0.0f, "the plan holds every value to its range");
}

bool same(const physics::Rgb& a, const physics::Rgb& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

bool inUnit(const physics::Rgb& c) { return c.r >= 0.0f && c.r <= 1.0f && c.g >= 0.0f && c.g <= 1.0f && c.b >= 0.0f && c.b <= 1.0f; }

std::vector<physics::Rgb> swatches() {
    std::vector<physics::Rgb> out;
    for (float r : {0.0f, 0.1f, 0.37f, 0.8f, 1.0f}) {
        for (float g : {0.0f, 0.25f, 0.9f}) {
            for (float b : {0.0f, 0.5f, 1.0f}) out.push_back({r, g, b});
        }
    }
    return out;
}

void theLayersStackBackToFront() {
    const auto colours = swatches();
    for (const auto& style : colours) {
        for (const auto& dye : colours) {
            for (float order : {0.0f, 0.5f, 1.0f}) {
                check(same(physics::layer(style, dye, 0.0f, order), style), "a fluid mix of 0 is the style bit for bit");
                for (float mix : {0.25f, 1.0f}) check(inUnit(physics::layer(style, dye, mix, order)), "the fluid never takes the frame past white");
            }
            // Over is the screen: it only ever lightens.
            const physics::Rgb over = physics::layer(style, dye, 1.0f, 1.0f);
            check(over.r >= style.r && over.g >= style.g && over.b >= style.b, "the fluid over the style only lightens it");
        }
    }
    // Behind: a lit pixel of the style stays as it is, a black one shows the fluid.
    const physics::Rgb lit{0.9f, 0.8f, 0.7f};
    const physics::Rgb ink{0.2f, 0.6f, 1.0f};
    check(same(physics::layer(lit, ink, 1.0f, 0.0f), lit), "the fluid behind leaves the style's light as it is");
    check(same(physics::layer({0.0f, 0.0f, 0.0f}, ink, 1.0f, 0.0f), ink), "and fills the style's black with its ink");
    // In front: particles screen on, so many over one pixel stay at white, and a dark mote leaves the picture.
    const physics::Rgb base = physics::layer({0.1f, 0.1f, 0.1f}, ink, 1.0f, 0.0f);
    check(same(physics::front(base, {0.0f, 0.0f, 0.0f}), base), "a dark particle leaves the picture as it was");
    physics::Rgb piled = base;
    for (int i = 0; i < 50; ++i) piled = physics::front(piled, {0.8f, 0.9f, 1.0f});
    check(inUnit(piled) && piled.b == 1.0f, "fifty particles over one pixel reach white and stop there");
    // The order: a particle lands on the layered picture, so it shows over the fluid and the style alike.
    const physics::Rgb mote{1.0f, 1.0f, 1.0f};
    check(same(physics::front(physics::layer(lit, ink, 1.0f, 1.0f), mote), mote), "the particles are the front layer");
}

void theMusicClockDrivesEverything() {
    check(physics::dyeWeight(0.0f) == 0.0f && physics::dyeWeight(-1.0f) == 0.0f && physics::dyeWeight(std::nanf("")) == 0.0f,
          "a stopped clock moves no dye");
    for (float dt : {1.0f / 120.0f, 1.0f / 60.0f, 1.0f / 30.0f, 0.1f}) {
        const float whole = physics::dyeWeight(dt);
        const float half = physics::dyeWeight(dt * 0.5f);
        const float twoHalves = 1.0f - (1.0f - half) * (1.0f - half);
        check(whole > 0.0f && whole < 1.0f, "a step moves the dye part of the way");
        check(std::fabs(twoHalves - whole) < 1e-5f, "two half steps land where one whole step does");
    }
    // The stir's phase: silence holds it, and one second reads the same at 30, 60 and 120 fps.
    physics::Clock still;
    for (int i = 0; i < 600; ++i) still.step(0.0f);
    check(still.stirTime == 0.0f, "silence holds the particles' stir");
    float phases[3] = {};
    int k = 0;
    for (int fps : {30, 60, 120}) {
        physics::Clock c;
        for (int i = 0; i < fps; ++i) c.step(1.0f / static_cast<float>(fps));
        phases[k++] = c.stirTime;
    }
    check(std::fabs(phases[0] - phases[1]) < 1e-4f && std::fabs(phases[1] - phases[2]) < 1e-4f, "the stir is frame-rate independent");
    // Determinism: the same frames give the same phase, bit for bit (an export replays them).
    physics::Clock a;
    physics::Clock b;
    for (int i = 0; i < 1000; ++i) {
        const float dt = (i % 7 == 0) ? 0.0f : 1.0f / static_cast<float>(24 + i % 5);
        a.step(dt);
        b.step(dt);
    }
    check(a.stirTime == b.stirTime && a.stirTime > 0.0f, "the same frames step the same stir");
    physics::Clock longPlay;
    for (int i = 0; i < 60 * 60 * 60; ++i) longPlay.step(1.0f / 60.0f);
    check(std::isfinite(longPlay.stirTime) && longPlay.stirTime < 6.2831853f * 64.0f + 1e-3f, "an hour's play keeps the phase wrapped");
    // The force: 0 stills the picture's push, the curl and the drift; more pushes harder.
    check(physics::swirlGain(0.0f) == 0.0f && physics::curlStrength(0.0f) == 0.0f && physics::driftSpeed(0.0f) == 0.0f,
          "a force of 0 stirs nothing");
    check(physics::swirlGain(2.0f) > physics::swirlGain(1.0f) && physics::driftSpeed(2.0f) > physics::driftSpeed(1.0f),
          "a harder force stirs harder");
    check(physics::kVelocityDissipation > 0.0f && physics::kDyeDissipation > 0.0f, "the stir and the ink both let go");
}

void theGridsOutliveAShortDip() {
    physics::Keep keep;
    check(!keep.step(false), "nothing is kept before a layer was on");
    check(keep.step(true), "a layer on keeps its grids");
    for (int i = 0; i < physics::kIdleFrames; ++i) check(keep.step(false), "a dip to 0 keeps them for the idle window");
    check(!keep.step(false), "then they are freed");
    check(!keep.step(false), "and stay freed");
    check(keep.step(true), "a layer on again keeps them again");
}

void theParticlesAreBornOverTheWholeFrame() {
    for (float aspect : {0.45f, 1.0f, 2.2f}) {
        const auto s = physics::spawns(aspect);
        check(s == physics::spawns(aspect), "the births are the same each time");
        float minX = 9.0f, maxX = -9.0f, minY = 9.0f, maxY = -9.0f;
        for (int i = 0; i < physics::kSpawnPoints; ++i) {
            const float x = s[static_cast<size_t>(i * 4)];
            const float y = s[static_cast<size_t>(i * 4 + 1)];
            const float jitter = s[static_cast<size_t>(i * 4 + 3)];
            check(std::fabs(x) <= aspect && std::fabs(y) <= 1.0f, "every birth point is inside the frame");
            check(s[static_cast<size_t>(i * 4 + 2)] == 1.0f && jitter > 0.0f, "every point weighs the same and spreads");
            minX = std::fmin(minX, x - jitter);
            maxX = std::fmax(maxX, x + jitter);
            minY = std::fmin(minY, y - jitter);
            maxY = std::fmax(maxY, y + jitter);
        }
        check(minX <= -aspect * 0.9f && maxX >= aspect * 0.9f && minY <= -0.9f && maxY >= 0.9f, "the births reach every edge");
    }
    check(physics::spawns(std::nanf(""))[0] == physics::spawns(1.0f)[0], "an unknown aspect is square");
}

void theParamsAreTargetsTheSafetyRulesHold() {
    const SceneParams base{};
    check(geode::viz::applyLfoTarget(base, LfoTarget::SimFluid, 0.5f).simFluid == 0.5f, "a wave turns the fluid layer up");
    check(geode::viz::applyLfoTarget(base, LfoTarget::SimParticles, 2.0f).simParticles == 1.0f, "a route holds the particles to full");
    check(geode::viz::applyLfoTarget(base, LfoTarget::SimLayerOrder, 1.0f).simLayerOrder == 1.0f, "a knob moves the fluid over the style");
    check(geode::viz::applyLfoTarget(base, LfoTarget::SimForce, -5.0f).simForce == 0.0f, "an envelope stills the force at 0");
    for (LfoTarget t : {LfoTarget::SimFluid, LfoTarget::SimParticles, LfoTarget::SimLayerOrder}) {
        check(safety::limitLfoRate(10.0f, t) == safety::kWcagFlashesPerSecond, "a wave on a physics mix runs at 3 Hz at most");
    }
    check(safety::limitLfoRate(10.0f, LfoTarget::SimForce) == 10.0f, "the force is motion, not light");
    SceneParams p;
    p.simForce = 2.0f;
    p.simFluid = 1.0f;
    const SceneParams calm = safety::apply(p, true);
    check(calm.simForce == 2.0f * safety::kReducedMotionScale, "reduced motion slows the stir");
    check(calm.simFluid == 1.0f, "and leaves the layer's mix to the user");
    check(safety::apply(p, false).simForce == 2.0f, "full motion leaves the force alone");
}

}  // namespace

int main() {
    everyLookPlansNothing();
    bothLayersArePlanned();
    theLayersStackBackToFront();
    theMusicClockDrivesEverything();
    theGridsOutliveAShortDip();
    theParticlesAreBornOverTheWholeFrame();
    theParamsAreTargetsTheSafetyRulesHold();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("physics_layers_test: all passed\n");
    return 0;
}
