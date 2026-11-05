// The environment interface (core/scene/env) and the Scene that steps it (core/scene/Scene), on the
// host, with Morph World as the first 6-DoF environment (#513).
//
//   scene_env_test                      run the checks
//   scene_env_test --dump <seed> [hover]
//                                       print the uWorld a free camera uploads after 6 s of forward
//                                       flight (or of hovering) ending at glslcheck's eye (0, 0, 37),
//                                       for tools/glslcheck --set uWorld=... with --face
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "scene/Scene.hpp"
#include "scene/env/Environment.hpp"
#include "scene/env/MorphWorldEnv.hpp"
#include "viz/world/WorldGenome.hpp"

using geode::scene::env::EnvInput;
using geode::scene::env::MorphWorldEnv;
using geode::scene::env::Vec3;
namespace env = geode::scene::env;
namespace world = geode::viz::world;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

constexpr float kDt = 1.0f / 60.0f;

// The 26 directions out of a cube's centre: its 6 faces, 12 edges and 8 corners.
std::vector<Vec3> directions26() {
    std::vector<Vec3> out;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            for (int z = -1; z <= 1; ++z) {
                if (x == 0 && y == 0 && z == 0) continue;
                const float n = std::sqrt(static_cast<float>(x * x + y * y + z * z));
                out.push_back({static_cast<float>(x) / n, static_cast<float>(y) / n, static_cast<float>(z) / n});
            }
        }
    }
    return out;
}

GeodeSoundFrame groove() {
    GeodeSoundFrame f{};
    f.presence = 0.8f;
    f.loudness = 0.6f;
    f.intensity = 0.5f;
    f.bassAtt = 1.0f;
    f.kickEnv = 0.3f;
    f.scaleBrightness = 0.5f;
    f.keyMajor = 0.5f;
    f.densitySlow = 0.5f;
    f.runDensity = 0.4f;
    return f;
}

// A camera pose at `pos`, facing `fwd` (unit), any roll: the world must not care.
geode::viz::CameraPose poseAt(const Vec3& pos, const Vec3& fwd) {
    geode::viz::CameraPose c;
    c.pos = pos;
    c.fwd = fwd;
    // Any up not along fwd; right = up x fwd, up again = fwd x right.
    Vec3 up = std::fabs(fwd[1]) > 0.9f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
    Vec3 r{up[1] * fwd[2] - up[2] * fwd[1], up[2] * fwd[0] - up[0] * fwd[2], up[0] * fwd[1] - up[1] * fwd[0]};
    const float rn = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (float& v : r) v /= rn;
    up = {fwd[1] * r[2] - fwd[2] * r[1], fwd[2] * r[0] - fwd[0] * r[2], fwd[0] * r[1] - fwd[1] * r[0]};
    c.right = r;
    c.up = up;
    return c;
}

EnvInput input(const GeodeSoundFrame* sound, const geode::viz::CameraPose* cam, bool freeCamera) {
    EnvInput in;
    in.sound = sound;
    in.camera = cam;
    in.freeCamera = freeCamera;
    // As the Scene does: the frame's own moments.
    in.section = sound != nullptr && sound->sectionChange > 0.5f;
    in.drop = sound != nullptr && sound->drop > 0.5f;
    in.controls.morph = 0.6f;
    in.controls.turbulence = 0.4f;
    return in;
}

// The eye's bubble, the way a ray leaves it: at the eye the world is at least eyeRadius away, and
// along each of 26 rays a point s out is at least eyeRadius - s away, so every ray travels eyeRadius
// before it can meet matter, whichever way the camera looks.
void eyeIsOpen(const MorphWorldEnv& e, const Vec3& eye, const std::string& where) {
    const float r = e.eyeRadius();
    const float at = e.sdf(eye);
    check(std::isfinite(at) && at >= r - 1e-3f, where + ": the eye has its bubble (" + std::to_string(at) + ")");
    int shut = 0;
    for (const Vec3& d : directions26()) {
        for (float s : {0.25f, 0.5f, 0.75f, 0.95f}) {
            const Vec3 p{eye[0] + d[0] * r * s, eye[1] + d[1] * r * s, eye[2] + d[2] * r * s};
            const float v = e.sdf(p);
            if (!(std::isfinite(v) && v >= r * (1.0f - s) - 1e-3f)) ++shut;
        }
    }
    check(shut == 0, where + ": open along all 26 rays (" + std::to_string(shut) + " samples shut)");
}

void registryKnowsMorphWorld() {
    check(env::has("morph_world"), "Morph World has an environment");
    check(!env::has("plasma") && !env::has("dive_lattice"), "a style not yet ported has none");
    auto e = env::make("morph_world");
    check(e != nullptr && e->sixDof(), "Morph World's environment is 6-DoF");
    check(e != nullptr && e->family() == "morph_world" && e->variant() == "morph_world", "its family and variant");
    check(env::make("plasma") == nullptr, "make() is null for a style with no environment");
    if (e == nullptr) return;
    const auto& schema = e->params().schema();
    check(schema.idsUnique(), "the env's param ids are unique");
    check(schema.find("env.morph") && schema.find("env.corridor") && schema.find("env.repeatFloor"),
          "the env declares its params through the signal schema");
    const env::UniformBlock u = e->uniforms();
    check(u.name != nullptr && std::string(u.name) == "uWorld" && u.vec4Count == world::kVec4, "the block is uWorld[18]");
}

void openAroundTheEyeAtEverySeed() {
    // Eyes the free camera can reach: on the travel axis, off it, far off it and far back, each facing a
    // different way, at several seeds.
    const std::array<Vec3, 5> eyes{{{0.0f, 0.0f, 37.0f}, {2.5f, -1.5f, 12.0f}, {9.0f, 4.0f, -60.0f},
                                    {-31.0f, 22.0f, 300.0f}, {0.4f, 7.7f, 5000.0f}}};
    const std::array<Vec3, 4> faces{{{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}}};
    const GeodeSoundFrame f = groove();
    for (uint64_t seed : {0ull, 1ull, 42ull, 0xDEADBEEFull, 0x5EED5EEDull}) {
        MorphWorldEnv e("morph_world");
        e.reset(seed);
        for (size_t i = 0; i < eyes.size(); ++i) {
            const geode::viz::CameraPose cam = poseAt(eyes[i], faces[i % faces.size()]);
            for (int n = 0; n < 30; ++n) e.step(input(&f, &cam, true), kDt);
            eyeIsOpen(e, eyes[i], "seed " + std::to_string(seed) + " eye " + std::to_string(i));
        }
    }
}

void openThroughAGlide() {
    // A drop turns the world (MorphDriver::kDropGlide); the eye stays open at every point of the glide,
    // flying forward, turning round and sliding sideways meanwhile.
    for (uint64_t seed : {3ull, 77ull, 1234ull}) {
        MorphWorldEnv e("morph_world");
        e.reset(seed);
        GeodeSoundFrame f = groove();
        Vec3 pos{0.0f, 0.0f, 0.0f};
        geode::viz::CameraPose cam = poseAt(pos, {0.0f, 0.0f, 1.0f});
        for (int n = 0; n < 60; ++n) e.step(input(&f, &cam, true), kDt);
        int sampled = 0;
        for (int drop = 0; drop < 2; ++drop) {
            const int before = e.driver().retargets();
            f.drop = 1.0f;
            e.step(input(&f, &cam, true), kDt);
            f.drop = 0.0f;
            check(e.driver().retargets() == before + 1, "a drop retargets the world");
            for (int n = 0; n < 150; ++n) {
                const float t = static_cast<float>(n) * kDt;
                pos = {1.2f * std::sin(t), 0.6f * std::sin(1.7f * t), pos[2] + 1.6f * kDt};
                const Vec3 fwd{std::sin(2.0f * t), 0.0f, std::cos(2.0f * t)};
                cam = poseAt(pos, fwd);
                e.step(input(&f, &cam, true), kDt);
                if (n % 10 == 0) {
                    const float progress = e.driver().progress();
                    if (progress > 0.0f && progress < 1.0f) ++sampled;
                    eyeIsOpen(e, pos, "seed " + std::to_string(seed) + " glide " + std::to_string(drop) + " frame " +
                                          std::to_string(n));
                }
            }
        }
        check(sampled >= 6, "the probe ran mid-glide (" + std::to_string(sampled) + " samples)");
    }
}

void theTubeOnlyForForwardFlight() {
    const GeodeSoundFrame f = groove();
    // Forward along +z at the rig's base speed: the tube opens to the driver's corridor.
    MorphWorldEnv fly("morph_world");
    fly.reset(9);
    for (int n = 0; n < 240; ++n) {
        const geode::viz::CameraPose cam = poseAt({0.0f, 0.0f, 1.6f * static_cast<float>(n) * kDt}, {0.0f, 0.0f, 1.0f});
        fly.step(input(&f, &cam, true), kDt);
    }
    check(fly.tubeWeight() > 0.9f, "forward flight opens the tube (" + std::to_string(fly.tubeWeight()) + ")");
    check(fly.view()[world::Corridor] > 1.0f, "and the uploaded corridor is the driver's");
    const float* u = fly.uniforms().data;
    check(u[world::Corridor] == fly.view()[world::Corridor], "the block and the CPU genome agree on the corridor");
    check(u[world::RepXY] == fly.view()[world::RepXY] && u[world::RepXY] >= 0.25f - 1e-6f,
          "a free camera sees at least the repeat floor");

    // Hovering, or sliding sideways: no tube, only the bubble and the wake.
    MorphWorldEnv hover("morph_world");
    hover.reset(9);
    for (int n = 0; n < 240; ++n) {
        const geode::viz::CameraPose cam = poseAt({1.5f * static_cast<float>(n) * kDt, 0.0f, 10.0f}, {0.0f, 1.0f, 0.0f});
        hover.step(input(&f, &cam, true), kDt);
    }
    check(hover.tubeWeight() < 0.05f, "a sideways slide closes the tube (" + std::to_string(hover.tubeWeight()) + ")");
    check(hover.view()[world::Corridor] < -3.0f, "a closed tube's radius is below zero");

    // The option off: no tube even in forward flight.
    MorphWorldEnv off("morph_world");
    off.reset(9);
    for (int n = 0; n < 240; ++n) {
        off.params().input(env::kMwCorridor, geode::signal::ParamInput::set(0.0f));
        const geode::viz::CameraPose cam = poseAt({0.0f, 0.0f, 1.6f * static_cast<float>(n) * kDt}, {0.0f, 0.0f, 1.0f});
        off.step(input(&f, &cam, true), kDt);
    }
    check(off.tubeWeight() < 0.05f, "env.corridor off keeps the tube closed");
}

void railsDrawsTheDriverExactly() {
    // A camera on rails gets the block MorphDriver publishes, bit for bit (#348's look, untouched).
    const GeodeSoundFrame f = groove();
    MorphWorldEnv e("morph_world");
    e.reset(5);
    for (int n = 0; n < 120; ++n) {
        const geode::viz::CameraPose cam = poseAt({0.0f, 0.0f, 1.6f * static_cast<float>(n) * kDt}, {0.0f, 0.0f, 1.0f});
        e.step(input(&f, &cam, false), kDt);
    }
    const float* u = e.uniforms().data;
    const auto& d = e.driver().uniforms();
    bool same = true;
    for (int i = 0; i < world::kUniformCount; ++i) same = same && u[i] == d[static_cast<size_t>(i)];
    check(same, "rails uploads the driver's uWorld unchanged");
}

void sceneStepsTheEnvironmentOnThePlaySeed() {
    // The Scene: an empty one for a style without an environment, Morph World's with one; the same play
    // seed and frames give the same block, and a new seed starts the world over.
    geode::scene::Scene flat("plasma");
    check(!flat.hasEnvironment(), "a style with no environment has an empty Scene");
    geode::scene::FrameInput none;
    flat.step(none);

    const GeodeSoundFrame f = groove();
    auto run = [&](uint64_t seed) {
        geode::scene::Scene s("morph_world");
        geode::viz::CameraPose cam = poseAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
        for (int n = 0; n < 90; ++n) {
            cam.pos[2] = 1.6f * static_cast<float>(n) * kDt;
            geode::scene::FrameInput in;
            in.sound = &f;
            in.dt = kDt;
            in.playSeed = seed;
            in.camera = &cam;
            in.freeCamera = true;
            s.step(in);
        }
        const env::UniformBlock u = s.environment()->uniforms();
        return std::vector<float>(u.data, u.data + u.vec4Count * 4);
    };
    geode::scene::Scene probe("morph_world");
    check(probe.hasEnvironment() && probe.environment()->sixDof(), "Morph World's Scene holds its environment");
    check(run(11) == run(11), "the same seed and frames give the same world");
    check(run(11) != run(12), "another play seed is another world");
}

// What the overlay pass would draw at each frame of a Morph World play, with a drop at frame dropAt.
struct DropRun {
    std::vector<int> counts;           // shapes packed per frame
    std::vector<std::vector<float>> blocks;  // the packed bytes per frame, as floats
    std::vector<int> live;             // live objects per frame
    float maxBurst = 0.0f;             // the largest Burst progress any packed shape showed
    bool sawGrowIn = false;            // a shape drawn under its full scale while entering
};

DropRun playWithADrop(uint64_t seed, int frames, int dropAt) {
    geode::scene::Scene s("morph_world");
    geode::viz::CameraPose cam = poseAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
    DropRun out;
    geode::scene::object::OverlayFrame f;
    for (int n = 0; n < frames; ++n) {
        GeodeSoundFrame sound = groove();
        if (n == dropAt) sound.drop = 0.8f;
        cam.pos[2] = 1.6f * static_cast<float>(n) * kDt;
        geode::scene::FrameInput in;
        in.sound = &sound;
        in.dt = kDt;
        in.playSeed = seed;
        in.camera = &cam;
        in.freeCamera = true;
        s.step(in);
        const int c = s.packOverlay(f);
        out.counts.push_back(c);
        out.live.push_back(static_cast<int>(s.objects().pool().live()));
        out.blocks.emplace_back(f.data(), f.data() + c * 20);
        for (int i = 0; i < c; ++i) {
            out.maxBurst = std::max(out.maxBurst, f.shapes[i].life[2]);
            if (f.shapes[i].life[1] < 0.1f && f.shapes[i].life[3] > 0.0f) out.sawGrowIn = true;
        }
    }
    return out;
}

void aDropOnMorphWorldThrowsShapesThatBurstOut() {
    // The tracer bullet (#515): Morph World declares a drop spawner; the drop throws a few shapes into
    // view in front of the camera, they grow in, and they leave by Burst, all deterministically per seed.
    geode::scene::Scene probe("morph_world");
    check(probe.objects().spawnerCount() == 1, "Morph World declares its drop spawner");

    const int frames = 360;
    const int dropAt = 60;
    const DropRun a = playWithADrop(5, frames, dropAt);
    bool quietBefore = true;
    for (int n = 0; n < dropAt; ++n) quietBefore = quietBefore && a.live[n] == 0;
    check(quietBefore, "nothing spawns before the drop");
    check(a.live[dropAt] >= 3 && a.live[dropAt] <= 5, "the drop throws three to five shapes");
    int peak = 0;
    for (int c : a.counts) peak = std::max(peak, c);
    check(peak >= 3, "the overlay packs them for drawing");
    check(a.sawGrowIn, "they grow in from a point (a Collapse entry run backwards)");
    check(a.maxBurst > 0.5f, "they leave by Burst");
    check(a.live.back() == 0 && a.counts.back() == 0, "and are gone a few seconds after the drop");

    // In front of the camera at the drop: the eye was at z = 1.6 s, looking +z; the shapes start 3.2 ahead.
    const DropRun first = playWithADrop(5, dropAt + 20, dropAt);
    const auto& blk = first.blocks.back();
    bool ahead = !blk.empty();
    const float eyeZ = 1.6f * static_cast<float>(dropAt) * kDt;
    for (std::size_t i = 0; i + 20 <= blk.size(); i += 20) ahead = ahead && blk[i + 2] > eyeZ + 1.5f;
    check(ahead, "the shapes are thrown into view, ahead of the camera of the drop");

    const DropRun b = playWithADrop(5, frames, dropAt);
    const DropRun c = playWithADrop(6, frames, dropAt);
    check(a.blocks == b.blocks, "the same seed and sound draw the same shapes, bit for bit");
    check(a.blocks != c.blocks, "another play seed throws other shapes");
}

int dump(uint64_t seed, bool hover) {
    // Six seconds ending at glslcheck's eye (0, 0, 37): flown forward at the rig's base speed, or held.
    MorphWorldEnv e("morph_world");
    e.reset(seed);
    e.setAssetsBound(true);
    const GeodeSoundFrame f = groove();
    const int frames = 360;
    for (int n = 0; n <= frames; ++n) {
        const float z = hover ? 37.0f : 37.0f - 1.6f * static_cast<float>(frames - n) * kDt;
        const geode::viz::CameraPose cam = poseAt({0.0f, 0.0f, z}, {0.0f, 0.0f, 1.0f});
        EnvInput in = input(&f, &cam, true);
        in.controls.morph = 0.0f;
        in.controls.turbulence = 0.0f;
        e.step(in, kDt);
    }
    const env::UniformBlock u = e.uniforms();
    std::vector<float> out(u.data, u.data + u.vec4Count * 4);
    // glslcheck binds the baked assets (as world_genome_test --dump says), which this host run has not loaded.
    out[world::Assets] = 1.0f;
    for (size_t i = 0; i < out.size(); ++i) std::printf("%s%g", i ? "," : "", static_cast<double>(out[i]));
    std::printf("\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--dump") {
        return dump(std::strtoull(argv[2], nullptr, 0), argc >= 4 && std::string(argv[3]) == "hover");
    }
    registryKnowsMorphWorld();
    openAroundTheEyeAtEverySeed();
    openThroughAGlide();
    theTubeOnlyForForwardFlight();
    railsDrawsTheDriverExactly();
    sceneStepsTheEnvironmentOnThePlaySeed();
    aDropOnMorphWorldThrowsShapesThatBurstOut();
    if (failures == 0) std::printf("scene_env_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
