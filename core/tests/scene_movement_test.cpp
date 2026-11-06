// Host tests for core/scene/movement (#516): each behaviour against a closed form or an invariant, the
// same result at 30, 60 and 120 fps, no allocation while stepping, and every number a param row. Built
// and run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL,
// no device. Every check drives the steppers through their public interface: bodies, params, dt and a
// seed go in, bodies come out.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <new>
#include <string_view>
#include <vector>

#include "scene/camera/CameraMath.hpp"
#include "scene/movement/MovementParams.hpp"
#include "signal/params/ParamStore.hpp"

using namespace geode::scene;
using namespace geode::scene::movement;

// ---- Allocation counter: the steppers promise none per frame ----------------------------------------
static std::size_t gAllocations = 0;

void* operator new(std::size_t n) {
    ++gAllocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }
bool near(const Vec3& a, const Vec3& b, float tol) { return length(sub(a, b)) <= tol; }

constexpr int kRates[3] = {30, 60, 120};

// A seeded cloud of bodies in a cube of half-width `half` round `centre`, at rest unless `speed`.
std::vector<Body> cloud(std::size_t n, std::uint32_t seed, float half, Vec3 centre = {0, 0, 0}, float speed = 0.0f) {
    Rng rng{seed, 0};
    std::vector<Body> out(n);
    for (Body& b : out) {
        b.pos = add(centre, Vec3{rng.signedNext() * half, rng.signedNext() * half, rng.signedNext() * half});
        b.vel = {rng.signedNext() * speed, rng.signedNext() * speed, rng.signedNext() * speed};
    }
    return out;
}

float minPairDistance(const std::vector<Body>& bodies) {
    float best = 1e30f;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        for (std::size_t j = i + 1; j < bodies.size(); ++j) best = std::min(best, length(sub(bodies[i].pos, bodies[j].pos)));
    }
    return best;
}

std::vector<float> defaultsOf(const geode::signal::ParamSchema& s) {
    std::vector<float> v;
    for (std::size_t i = 0; i < s.size(); ++i) v.push_back(s.def(static_cast<geode::signal::ParamId>(i)).defaultValue);
    return v;
}

// ---- Params -----------------------------------------------------------------------------------------

void testSchemas() {
    const geode::signal::ParamSchema all[] = {orbitSchema(), driftSchema(), flockSchema(), followSchema(), bounceSchema(),
                                              reactSchema(), motionSchema(), morphSchema(), collapseSchema(), splitSchema()};
    const std::size_t counts[] = {kOrbitParamCount, kDriftParamCount, kFlockParamCount, kFollowParamCount, kBounceParamCount,
                                  kReactParamCount, kMotionParamCount, kMorphParamCount, kCollapseParamCount, kSplitParamCount};
    std::vector<std::string_view> ids;
    for (std::size_t s = 0; s < std::size(all); ++s) {
        check(all[s].size() == counts[s], "schema: one row per enum value");
        check(all[s].idsUnique(), "schema: ids unique within a behaviour");
        for (std::size_t i = 0; i < all[s].size(); ++i) {
            const std::string_view id = all[s].def(static_cast<geode::signal::ParamId>(i)).id;
            check(id.substr(0, 9) == "movement.", "schema: ids start movement.");
            ids.push_back(id);
        }
    }
    bool unique = true;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = i + 1; j < ids.size(); ++j) unique = unique && ids[i] != ids[j];
    }
    check(unique, "schema: ids unique across behaviours");

    // The tables' defaults are the structs' defaults, through the schema and with no values at all.
    check(orbitParams(defaultsOf(orbitSchema())) == OrbitParams{}, "defaults: orbit");
    check(driftParams(defaultsOf(driftSchema())) == DriftParams{}, "defaults: drift");
    check(flockParams(defaultsOf(flockSchema())) == FlockParams{}, "defaults: flock");
    check(followParams(defaultsOf(followSchema())) == FollowParams{}, "defaults: follow");
    check(bounceParams(defaultsOf(bounceSchema())) == BounceParams{}, "defaults: bounce");
    check(reactParams(defaultsOf(reactSchema())) == ReactParams{}, "defaults: react");
    check(motionParams(defaultsOf(motionSchema())) == MotionParams{}, "defaults: motion");
    check(morphParams(defaultsOf(morphSchema())) == MorphParams{}, "defaults: morph");
    check(collapseParams(defaultsOf(collapseSchema())) == CollapseParams{}, "defaults: collapse");
    check(splitParams(defaultsOf(splitSchema())) == SplitParams{}, "defaults: split");
    check(orbitParams({}) == OrbitParams{}, "defaults: an empty vector is the defaults");

    // A route on a row reaches the behaviour: base + modulation, clamped once by the row's range.
    geode::signal::ParamStore store(orbitSchema());
    store.clearModulation();
    store.modulate(kOrbitRadius, geode::signal::Combine::Add, 1000.0f);
    store.modulate(kOrbitRate, geode::signal::Combine::Add, 1.0f);
    std::vector<float> eff;
    store.effectiveAll(eff);
    const OrbitParams routed = orbitParams(eff);
    check(near(routed.radius, 50.0f, 1e-6f), "route: orbit radius modulated and clamped to its max");
    check(near(routed.rate, 1.5f, 1e-6f), "route: orbit rate modulated");
    // Out-of-range values pass the one clamp.
    const float wild[] = {0, 0, 0, 0, 1, 0, -5.0f, 0.5f, 0.5f};
    check(orbitParams(wild).radius == 0.0f, "clamp: a negative radius reads as 0");
    const float splitWild[] = {500.0f};
    check(splitParams(splitWild).count == kMaxSplitChildren, "clamp: split count capped");
}

// ---- Orbit ------------------------------------------------------------------------------------------

void testOrbit() {
    OrbitParams p;
    p.radius = 1.0f;
    p.rate = 2.0f;
    Body b;
    b.pos = {1.0f, 0.0f, 0.0f};
    bool held = true;
    for (int f = 0; f < 60; ++f) {
        stepOrbit(b, p, 1.0f / 60.0f);
        const float r = std::sqrt(b.pos[0] * b.pos[0] + b.pos[2] * b.pos[2]);
        held = held && near(r, 1.0f, 1e-4f) && near(b.pos[1], 0.0f, 1e-6f);
    }
    check(held, "orbit: a body on the ring keeps its radius every frame");
    // Rotating (1,0,0) right-handed about +Y by θ gives (cos θ, 0, -sin θ); θ = rate · 1 s.
    check(near(b.pos, Vec3{std::cos(2.0f), 0.0f, -std::sin(2.0f)}, 1e-3f), "orbit: the phase is rate · t");
    check(near(length(b.vel), 2.0f, 2e-2f), "orbit: speed on a unit ring is the rate");

    // Capture: radius and height fall as e^{-t/τ} onto the ring.
    OrbitParams c;
    c.radius = 1.0f;
    c.rate = 0.7f;
    c.capture = 0.5f;
    for (int fps : kRates) {
        Body q;
        q.pos = {3.0f, 1.0f, 0.0f};
        for (int f = 0; f < fps; ++f) stepOrbit(q, c, 1.0f / static_cast<float>(fps));
        const float r = std::sqrt(q.pos[0] * q.pos[0] + q.pos[2] * q.pos[2]);
        const float e = std::exp(-2.0f);
        check(near(r, 1.0f + 2.0f * e, 1e-3f), "orbit capture: radius = R + (r0 - R) e^{-t/τ}");
        check(near(q.pos[1], e, 1e-3f), "orbit capture: height = h0 e^{-t/τ}");
        const float angle = std::atan2(-q.pos[2], q.pos[0]);
        check(near(angle, 0.7f, 1e-3f), "orbit capture: phase = rate · t at every frame rate");
    }
}

// ---- Follow and the spring --------------------------------------------------------------------------

void testFollow() {
    FollowParams p;
    p.seconds = 0.5f;  // ω = 4
    p.damping = 1.0f;
    for (int fps : kRates) {
        Body b;
        b.pos = {5.0f, 0.0f, 0.0f};
        bool overshoot = false;
        float atOne = 0.0f;
        for (int f = 0; f < 5 * fps; ++f) {
            stepFollow(b, p, 1.0f / static_cast<float>(fps));
            overshoot = overshoot || b.pos[0] < -1e-6f;
            if (f == fps - 1) atOne = b.pos[0];
        }
        check(!overshoot, "follow: critically damped never overshoots");
        // x(t) = x0 (1 + ωt) e^{-ωt}.
        check(near(atOne, 5.0f * 5.0f * std::exp(-4.0f), 1e-4f), "follow: matches the critical closed form at 1 s");
        check(std::fabs(b.pos[0]) < 1e-3f, "follow: converges onto the target");
    }
    // The same law as the camera's Spring.
    {
        Body b;
        b.pos = {2.0f, 0.0f, 0.0f};
        b.vel = {1.0f, 0.0f, 0.0f};
        camera::Spring s;
        s.x = 2.0f;
        s.v = 1.0f;
        for (int f = 0; f < 40; ++f) {
            stepFollow(b, p, 1.0f / 60.0f);
            s.step(0.0f, 1.0f / 60.0f, 0.5f);
        }
        check(near(b.pos[0], s.x, 1e-5f) && near(b.vel[0], s.v, 1e-4f), "follow: equals camera::Spring when critical");
    }
    // Under-damped swings past; over-damped creeps in without passing.
    {
        FollowParams u = p;
        u.damping = 0.3f;
        FollowParams o = p;
        o.damping = 2.0f;
        Body bu;
        Body bo;
        bu.pos = bo.pos = {1.0f, 0.0f, 0.0f};
        bool uPast = false;
        bool oPast = false;
        for (int f = 0; f < 600; ++f) {
            stepFollow(bu, u, 1.0f / 60.0f);
            stepFollow(bo, o, 1.0f / 60.0f);
            uPast = uPast || bu.pos[0] < 0.0f;
            oPast = oPast || bo.pos[0] < -1e-6f;
        }
        check(uPast, "follow: under-damped overshoots");
        check(!oPast && std::fabs(bo.pos[0]) < 1e-2f, "follow: over-damped converges without overshoot");
    }
    // The spring is exact over any split of time.
    {
        float x1 = 1.0f, v1 = 0.5f, x2 = 1.0f, v2 = 0.5f;
        springStep(x1, v1, 3.0f, 0.4f, 1.0f);
        for (int i = 0; i < 10; ++i) springStep(x2, v2, 3.0f, 0.4f, 0.1f);
        check(near(x1, x2, 1e-5f) && near(v1, v2, 1e-5f), "spring: one step of 1 s = ten of 0.1 s");
    }
}

// ---- Bounce -----------------------------------------------------------------------------------------

void testBounce() {
    BounceParams p;
    p.strength = 2.0f;
    p.seconds = 0.4f;  // ω = 5
    p.damping = 1.0f;
    // Critical: offset(t) = v0 t e^{-ωt}, peaking at v0 / (ω e) when t = 1/ω.
    {
        Body b;
        BounceState s;
        float peak = 0.0f;
        for (int f = 0; f < 3 * 240; ++f) {
            stepBounce(b, s, p, f == 0 ? 1.0f : 0.0f, 1.0f / 240.0f);
            peak = std::max(peak, b.pos[1]);
        }
        check(near(peak, 2.0f / (5.0f * std::exp(1.0f)), 2e-3f), "bounce: the peak is v0 / (ω e)");
        check(std::fabs(b.pos[1]) < 1e-3f && std::fabs(s.offset) < 1e-3f, "bounce: decays back to rest");
    }
    // Beats at 0 s and 0.5 s (a frame boundary at every rate) land the same at 30, 60 and 120 fps.
    BounceParams bouncy = p;
    bouncy.damping = 0.35f;
    float ref = 0.0f;
    for (int fps : kRates) {
        Body b;
        BounceState s;
        for (int f = 0; f < fps * 3 / 2; ++f) {
            const float kick = (f == 0 || f == fps / 2) ? 1.0f : 0.0f;
            stepBounce(b, s, bouncy, kick, 1.0f / static_cast<float>(fps));
        }
        if (fps == 30) ref = b.pos[1];
        check(near(b.pos[1], ref, 1e-4f), "bounce: 30, 60 and 120 fps agree");
    }
}

// ---- Drift ------------------------------------------------------------------------------------------

void testDrift() {
    DriftParams p;
    p.amplitude = 1.0f;
    p.seed = 7;
    MotionParams m;
    m.drag = 0.0f;
    bool bounded = true;
    for (int i = 0; i < 200; ++i) {
        const Vec3 f = driftField(p, Vec3{i * 0.37f, i * -0.11f, i * 0.05f}, i * 0.1f);
        bounded = bounded && std::fabs(f[0]) <= 1.0f && std::fabs(f[1]) <= 1.0f && std::fabs(f[2]) <= 1.0f;
    }
    check(bounded, "drift: the field stays within its amplitude");
    const Vec3 a = driftField(p, {0.3f, 0.4f, 0.5f}, 1.0f);
    const Vec3 b = driftField(p, {0.301f, 0.4f, 0.5f}, 1.0f);
    check(near(a, b, 0.02f), "drift: the field is smooth");
    DriftParams other = p;
    other.seed = 8;
    check(!near(driftField(other, {0.3f, 0.4f, 0.5f}, 1.0f), a, 1e-4f), "drift: another seed is another field");

    std::vector<Body> ref;
    for (int fps : kRates) {
        std::vector<Body> bodies = cloud(8, 3, 2.0f);
        float t = 0.0f;
        const float dt = 1.0f / static_cast<float>(fps);
        for (int f = 0; f < 3 * fps; ++f) {
            stepDrift(bodies, p, m, t, dt);
            t += dt;
        }
        if (fps == 30) ref = bodies;
        bool same = true;
        for (std::size_t i = 0; i < bodies.size(); ++i) same = same && near(bodies[i].pos, ref[i].pos, 2e-3f);
        check(same, "drift: 30, 60 and 120 fps agree");
        bool moved = true;
        for (std::size_t i = 0; i < bodies.size(); ++i) moved = moved && bodies[i].vel != Vec3{0.0f, 0.0f, 0.0f};
        check(moved, "drift: the field moves the bodies");
    }
    // Determinism: the same seed and inputs give bit-identical bodies.
    std::vector<Body> x = cloud(8, 3, 2.0f);
    std::vector<Body> y = cloud(8, 3, 2.0f);
    for (int f = 0; f < 30; ++f) {
        stepDrift(x, p, m, f / 60.0f, 1.0f / 60.0f);
        stepDrift(y, p, m, f / 60.0f, 1.0f / 60.0f);
    }
    bool identical = true;
    for (std::size_t i = 0; i < x.size(); ++i) identical = identical && x[i].pos == y[i].pos && x[i].vel == y[i].vel;
    check(identical, "drift: deterministic");
}

// ---- Flock ------------------------------------------------------------------------------------------

void testFlock() {
    FlockParams p;
    p.minDistance = 0.4f;
    MotionParams m;
    m.drag = 0.2f;
    m.maxSpeed = 4.0f;
    FlockScratch scratch(256);

    // Brute force (32 bodies, packed tightly): the minimum separation holds every frame after the first.
    {
        std::vector<Body> bodies = cloud(32, 11, 1.0f, {0, 0, 0}, 1.0f);
        float worst = 1e30f;
        for (int f = 0; f < 600; ++f) {
            stepFlock(bodies, p, m, 1.0f / 60.0f, scratch);
            worst = std::min(worst, minPairDistance(bodies));
        }
        check(worst >= 0.8f * p.minDistance, "flock: keeps the minimum separation (32 bodies)");
    }
    // The grid (200 bodies) finds the same neighbours as every pair.
    {
        std::vector<Body> gridBodies = cloud(200, 5, 5.0f, {0, 0, 0}, 1.0f);
        std::vector<Body> pairBodies = gridBodies;
        FlockScratch pairs(256);
        pairs.gridAbove = 100000;
        stepFlock(gridBodies, p, m, 1.0f / 60.0f, scratch);
        stepFlock(pairBodies, p, m, 1.0f / 60.0f, pairs);
        bool same = true;
        for (std::size_t i = 0; i < gridBodies.size(); ++i) same = same && near(gridBodies[i].pos, pairBodies[i].pos, 1e-4f);
        check(same, "flock: the grid equals the all-pairs search");
        float worst = 1e30f;
        for (int f = 0; f < 300; ++f) {
            stepFlock(gridBodies, p, m, 1.0f / 60.0f, scratch);
            worst = std::min(worst, minPairDistance(gridBodies));
        }
        check(worst >= 0.8f * p.minDistance, "flock: keeps the minimum separation on the grid (200 bodies)");
    }
    // Alignment: every body within reach of every other, no separation and no cruise, a common drift
    // under random headings: alignment damps the differences away and the flock shares one heading.
    {
        FlockParams a = p;
        a.radius = 20.0f;
        a.separation = 0.0f;
        a.cruise = 0.0f;
        a.alignment = 3.0f;
        std::vector<Body> bodies = cloud(24, 21, 1.5f, {0, 0, 0}, 1.0f);
        for (Body& b : bodies) b.vel = add(b.vel, Vec3{2.0f, 0.0f, 0.0f});
        for (int f = 0; f < 600; ++f) stepFlock(bodies, a, m, 1.0f / 60.0f, scratch);
        Vec3 mean{0, 0, 0};
        for (const Body& b : bodies) mean = add(mean, normalize(b.vel));
        check(length(mean) / static_cast<float>(bodies.size()) > 0.95f, "flock: alignment gives a shared heading");
    }
    // Frame rates agree over a second.
    std::vector<Body> ref;
    for (int fps : kRates) {
        std::vector<Body> bodies = cloud(16, 9, 1.5f, {0, 0, 0}, 1.0f);
        for (int f = 0; f < fps; ++f) stepFlock(bodies, p, m, 1.0f / static_cast<float>(fps), scratch);
        if (fps == 30) ref = bodies;
        bool same = true;
        for (std::size_t i = 0; i < bodies.size(); ++i) same = same && near(bodies[i].pos, ref[i].pos, 1e-3f);
        check(same, "flock: 30, 60 and 120 fps agree");
    }
    // Dead bodies take no part and do not move.
    {
        std::vector<Body> bodies = cloud(8, 4, 1.0f, {0, 0, 0}, 1.0f);
        bodies[3].alive = false;
        const Vec3 was = bodies[3].pos;
        for (int f = 0; f < 30; ++f) stepFlock(bodies, p, m, 1.0f / 60.0f, scratch);
        check(bodies[3].pos == was, "flock: a dead body stays put");
    }
}

// ---- React to the camera ----------------------------------------------------------------------------

void testReact() {
    MotionParams m;
    m.drag = 0.8f;
    ReactParams scatter;
    scatter.mode = static_cast<int>(ReactMode::Scatter);
    scatter.radius = 3.0f;
    scatter.strength = 6.0f;
    ForceInput in;
    in.eye = {1.0f, 2.0f, -1.0f};

    std::vector<Body> ref;
    for (int fps : kRates) {
        std::vector<Body> bodies = cloud(20, 13, 1.6f, in.eye);
        bodies[0].pos = add(in.eye, Vec3{0.2f, 0.0f, 0.0f});
        for (int f = 0; f < 3 * fps; ++f) stepReact(bodies, scatter, m, in, 1.0f / static_cast<float>(fps));
        bool out = true;
        for (const Body& b : bodies) out = out && length(sub(b.pos, in.eye)) > scatter.radius;
        check(out, "scatter: every body leaves the radius");
        if (fps == 30) ref = bodies;
        bool same = true;
        for (std::size_t i = 0; i < bodies.size(); ++i) same = same && near(bodies[i].pos, ref[i].pos, 1e-3f);
        check(same, "scatter: 30, 60 and 120 fps agree");
    }
    // A body already outside is untouched by scatter.
    {
        std::vector<Body> far{Body{}};
        far[0].pos = add(in.eye, Vec3{0.0f, 5.0f, 0.0f});
        stepReact(far, scatter, m, in, 1.0f / 60.0f);
        check(far[0].pos == add(in.eye, Vec3{0.0f, 5.0f, 0.0f}), "scatter: outside the radius nothing moves");
    }
    // Swarm gathers onto the shell of `radius` round the eye, from outside and from inside.
    {
        ReactParams swarm;
        swarm.mode = static_cast<int>(ReactMode::Swarm);
        swarm.radius = 3.0f;
        swarm.strength = 6.0f;
        MotionParams heavy;
        heavy.drag = 2.0f;
        std::vector<Body> bodies(2);
        bodies[0].pos = add(in.eye, Vec3{10.0f, 0.0f, 0.0f});
        bodies[1].pos = add(in.eye, Vec3{0.0f, 0.0f, 0.5f});
        for (int f = 0; f < 600; ++f) stepReact(bodies, swarm, heavy, in, 1.0f / 60.0f);
        check(near(length(sub(bodies[0].pos, in.eye)), 3.0f, 0.05f), "swarm: comes in onto the shell");
        check(near(length(sub(bodies[1].pos, in.eye)), 3.0f, 0.05f), "swarm: goes out onto the shell");
    }
    // Collide: never inside the sphere, and the inward speed comes back out.
    {
        ReactParams col;
        col.mode = static_cast<int>(ReactMode::Collide);
        col.radius = 2.0f;
        col.restitution = 0.5f;
        MotionParams none;
        none.drag = 0.0f;
        std::vector<Body> bodies(1);
        bodies[0].pos = add(in.eye, Vec3{0.0f, 0.0f, 5.0f});
        bodies[0].vel = {0.0f, 0.0f, -10.0f};
        bool outside = true;
        for (int f = 0; f < 60; ++f) {
            stepReact(bodies, col, none, in, 1.0f / 60.0f);
            outside = outside && length(sub(bodies[0].pos, in.eye)) >= col.radius - 1e-4f;
        }
        check(outside, "collide: never inside the sphere");
        check(near(bodies[0].vel[2], 5.0f, 1e-3f), "collide: the inward speed reflects by the restitution");
    }
}

// ---- Morph and collapse -----------------------------------------------------------------------------

void testShape() {
    for (int e = 0; e < kEaseCount; ++e) {
        const Ease curve = static_cast<Ease>(e);
        bool monotonic = true;
        float last = 0.0f;
        for (int i = 0; i <= 100; ++i) {
            const float v = ease(curve, i / 100.0f);
            monotonic = monotonic && v >= last - 1e-6f && v >= 0.0f && v <= 1.0f;
            last = v;
        }
        check(ease(curve, 0.0f) == 0.0f && near(ease(curve, 1.0f), 1.0f, 1e-6f), "ease: runs 0 to 1");
        check(monotonic, "ease: monotonic within 0..1");
    }
    check(near(ease(Ease::Smooth, 0.25f), 0.15625f, 1e-6f), "ease: smoothstep closed form");

    MorphParams mp;
    mp.duration = 2.0f;
    for (int fps : kRates) {
        MorphState s;
        beginMorph(s, 4);
        check(s.from == 0 && s.to == 4 && s.progress == 0.0f, "morph: begins from the shape shown");
        const int frames = fps * 2;
        for (int f = 0; f < frames / 2; ++f) stepMorph(s, mp, 1.0f / static_cast<float>(fps));
        check(near(s.linear, 0.5f, 1e-4f) && near(s.progress, 0.5f, 1e-4f), "morph: halfway at half the duration");
        for (int f = frames / 2; f < frames; ++f) stepMorph(s, mp, 1.0f / static_cast<float>(fps));
        check(s.progress == 1.0f && !s.running(), "morph: done at its duration");
        beginMorph(s, 4);
        check(!s.running(), "morph: to the shape already shown does nothing");
        beginMorph(s, 2);
        check(s.from == 4 && s.to == 2, "morph: the next starts from the last target");
    }

    CollapseParams cp;
    cp.duration = 0.8f;
    cp.twist = 1.0f;
    for (int fps : kRates) {
        Body b;
        b.scale = 2.0f;
        const Quat start = b.orient;
        CollapseState s;
        beginCollapse(s, b);
        const int frames = static_cast<int>(std::lround(cp.duration * fps));
        bool shrinking = true;
        float last = b.scale;
        for (int f = 0; f < frames - 1; ++f) {
            stepCollapse(b, s, cp, 1.0f / static_cast<float>(fps));
            shrinking = shrinking && b.scale <= last;
            last = b.scale;
        }
        check(shrinking, "collapse: the scale only falls");
        check(b.scale > 0.0f && b.alive, "collapse: not yet gone a frame before its duration");
        stepCollapse(b, s, cp, 1.0f / static_cast<float>(fps));
        check(b.scale == 0.0f && !b.alive && s.done, "collapse: reaches 0 at its duration");
        check(near(angleBetween(start, b.orient), 1.0f, 1e-3f), "collapse: turns its whole twist");
    }
    {
        CollapseParams keep = cp;
        keep.kill = false;
        Body b;
        CollapseState s;
        beginCollapse(s, b);
        for (int f = 0; f < 60; ++f) stepCollapse(b, s, keep, 1.0f / 60.0f);
        check(b.alive && b.scale == 0.0f, "collapse: kill off leaves the body alive at 0");
    }
}

// ---- Split ------------------------------------------------------------------------------------------

bool sameBodies(const Body* a, const Body* b, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (a[i].pos != b[i].pos || a[i].vel != b[i].vel || a[i].scale != b[i].scale) return false;
    }
    return true;
}

void testSplit() {
    SplitParams p;
    p.count = 8;
    Body parent;
    parent.pos = {1.0f, 2.0f, 3.0f};
    parent.vel = {1.0f, 2.0f, 0.0f};
    parent.scale = 1.5f;

    Body a[16];
    Body b[16];
    Body c[16];
    Body pa = parent;
    Body pb = parent;
    Body pc = parent;
    const std::size_t na = split(pa, p, 42, a);
    const std::size_t nb = split(pb, p, 42, b);
    const std::size_t nc = split(pc, p, 43, c);
    check(na == 8 && nb == 8 && nc == 8, "split: writes count children");
    check(sameBodies(a, b, na), "split: the same seed gives the same children");
    check(!sameBodies(a, c, na), "split: another seed gives other children");
    check(!pa.alive, "split: the parent dies");

    // Momentum (children of mass m/N) and centre of mass are the parent's.
    Vec3 meanVel{0, 0, 0};
    Vec3 meanPos{0, 0, 0};
    for (std::size_t i = 0; i < na; ++i) {
        meanVel = add(meanVel, a[i].vel);
        meanPos = add(meanPos, a[i].pos);
        check(a[i].alive && a[i].age == 0.0f && near(a[i].scale, 0.75f, 1e-6f), "split: children are born alive at their scale");
    }
    check(near(scale(meanVel, 1.0f / na), parent.vel, 1e-4f), "split: momentum is conserved");
    check(near(scale(meanPos, 1.0f / na), parent.pos, 1e-4f), "split: centre of mass is kept");

    // Without conservation or inheritance every child flies inside the cone round the velocity.
    SplitParams cone = p;
    cone.conserve = false;
    cone.inherit = 0.0f;
    Body pd = parent;
    Body d[16];
    const std::size_t nd = split(pd, cone, 7, d);
    const Vec3 axis = normalize(parent.vel);
    bool inside = true;
    for (std::size_t i = 0; i < nd; ++i) inside = inside && dot(normalize(d[i].vel), axis) >= std::cos(cone.spread) - 1e-4f;
    check(inside, "split: children spread within the cone");

    // Slots and keepParent.
    SplitParams emit = p;
    emit.keepParent = true;
    Body pe = parent;
    Body few[3];
    check(split(pe, emit, 1, few) == 3, "split: never writes past the slots given");
    check(pe.alive, "split: an emitter keeps its parent");
    Body dead = parent;
    dead.alive = false;
    check(split(dead, p, 1, a) == 0, "split: a dead body splits into nothing");
    // A parent at rest spreads over the whole sphere and still conserves momentum (zero).
    Body still = parent;
    still.vel = {0, 0, 0};
    const std::size_t ns = split(still, p, 9, a);
    Vec3 sum{0, 0, 0};
    for (std::size_t i = 0; i < ns; ++i) sum = add(sum, a[i].vel);
    check(ns == 8 && length(sum) < 1e-4f, "split: at rest the children's momentum sums to 0");
}

// ---- Body, and no allocation ------------------------------------------------------------------------

void testBodyAndAllocation() {
    Body b;
    b.spin = {0.0f, kPi, 0.0f};
    for (int f = 0; f < 60; ++f) turnAndAge(b, 1.0f / 60.0f);
    check(near(angleBetween(Quat{}, b.orient), kPi, 1e-3f) && near(b.age, 1.0f, 1e-4f), "body: spin turns, age counts");
    check(substepsFor(1.0f / 30.0f) == 8 && substepsFor(1.0f / 60.0f) == 4 && substepsFor(1.0f / 120.0f) == 2,
          "substeps: 30, 60 and 120 fps run the same 1/240 s steps");

    // Everything set up first; then a frame of every behaviour must not touch the heap.
    std::vector<Body> flock = cloud(100, 2, 3.0f, {0, 0, 0}, 1.0f);
    std::vector<Body> children(16);
    FlockScratch scratch(128);
    FlockParams fp;
    DriftParams dp;
    ReactParams rp;
    ForceSet set;
    set.flock = &fp;
    set.drift = &dp;
    set.react = &rp;
    ForceInput in;
    OrbitParams op;
    FollowParams wp;
    BounceParams bp;
    BounceState bs;
    MorphParams mp;
    MorphState ms;
    CollapseParams cp;
    CollapseState cs;
    SplitParams sp;
    const std::vector<float> values = defaultsOf(orbitSchema());
    beginCollapse(cs, flock[1]);
    beginMorph(ms, 3);

    const std::size_t before = gAllocations;
    for (int f = 0; f < 10; ++f) {
        stepForces(flock, set, in, 1.0f / 60.0f, scratch);
        stepDrift(flock, dp, MotionParams{}, 0.0f, 1.0f / 60.0f);
        stepReact(flock, rp, MotionParams{}, in, 1.0f / 60.0f);
        stepOrbit(std::span<Body>(flock).first(10), orbitParams(values), 1.0f / 60.0f);
        stepFollow(flock[0], wp, 1.0f / 60.0f);
        stepBounce(flock[0], bs, bp, f == 0 ? 1.0f : 0.0f, 1.0f / 60.0f);
        stepMorph(ms, mp, 1.0f / 60.0f);
        stepCollapse(flock[1], cs, cp, 1.0f / 60.0f);
        Body parent = flock[2];
        split(parent, sp, static_cast<std::uint32_t>(f), children);
        for (Body& b : flock) turnAndAge(b, 1.0f / 60.0f);
    }
    check(gAllocations == before, "no allocation while stepping");
}

}  // namespace

int main() {
    testSchemas();
    testOrbit();
    testFollow();
    testBounce();
    testDrift();
    testFlock();
    testReact();
    testShape();
    testSplit();
    testBodyAndAllocation();
    if (failures == 0) std::printf("scene_movement_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
