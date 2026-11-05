// Host tests for core/scene/object (#515): the base object, its params, the pool and its cap, the
// lifecycle and its exits, the spawners, camera targets, and the same objects at 30, 60 and 120 fps.
// Built and run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler;
// no GL, no device. Every check drives the objects through their public interface: specs, params,
// events and dt go in; bodies, looks, packed shapes and camera targets come out.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "scene/object/ObjectSystem.hpp"

using namespace geode::scene;
using namespace geode::scene::object;
using geode::signal::Combine;
using geode::signal::Event;
using geode::signal::EventKind;
using geode::signal::EventQueue;
using geode::signal::ParamDef;
using geode::signal::ParamId;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; }

struct Timed {
    double time;
    EventKind kind;
    float strength;
};

// Steps a system frame by frame at `fps` from frame `from` (inclusive of its events) to `toSeconds`.
// Frame k ends at k / fps; an event at time T lands on the frame k = round(T × fps), so events on a
// 1/30 s grid land at the same moment at 30, 60 and 120 fps.
struct Runner {
    ObjectSystem& sys;
    float fps;
    std::vector<Timed> events;
    camera::Pose camera{};
    long frame = 0;

    void runTo(double seconds) {
        const long last = std::lround(seconds * fps);
        while (frame < last) {
            ++frame;
            EventQueue q;
            for (const Timed& e : events) {
                if (std::lround(e.time * fps) == frame) q.push(Event{e.kind, e.strength, 0, static_cast<double>(frame) / fps});
            }
            sys.beginFrame();
            FrameIn in;
            in.dt = 1.0f / fps;
            in.events = &q;
            in.camera = camera;
            sys.step(in);
        }
    }
};

ObjectSpec plainShape() {
    ObjectSpec s;
    s.fadeCurve = 0.0f;  // linear fades, so the checks read straight off the clock
    s.death = Death::Never;
    return s;
}

void stepOnce(ObjectSystem& sys, float dt, const EventQueue* q = nullptr) {
    sys.beginFrame();
    FrameIn in;
    in.dt = dt;
    in.events = q;
    sys.step(in);
}

// ---- Params and route addressing -------------------------------------------------------------------

void testSchemaAndAddressing() {
    const auto& shape = objectSchema(Kind::Shape);
    check(shape.size() == kShapeParamCount, "the shape schema has every base row and every shape row");
    check(shape.idsUnique(), "row ids are unique");
    check(objectSchema(Kind::Light).size() == kBaseParamCount, "a kind with no rows of its own has the base rows");
    check(shape.find("pos.x") == std::optional<ParamId>(kPosX), "pos.x is ObjectParam kPosX");
    check(shape.find("shape.round") == std::optional<ParamId>(kShapeRound), "shape.round is ShapeParam kShapeRound");

    char buf[64];
    const std::size_t n = formatTarget(buf, sizeof buf, 17, "mat.glow");
    check(n > 0 && std::string(buf) == "param.object.17.mat.glow", "formatTarget writes param.object.<instance>.<param>");
    const auto parsed = parseTarget(buf);
    check(parsed && parsed->instance == 17 && parsed->param == "mat.glow", "parseTarget reads it back");
    check(!parseTarget("param.object.0.pos.x"), "instance 0 is never an object");
    check(!parseTarget("param.camera.1.pos.x"), "another part's prefix is not an object's");
    check(!parseTarget("param.object.12"), "an address needs a row");
    check(!parseTarget("param.object.1x.pos.x"), "the instance is plain decimal");
    check(formatTarget(buf, 8, 17, "mat.glow") == 0 && buf[0] == '\0', "an address that does not fit writes nothing");
}

void testEveryPropertyIsARouteTarget() {
    ObjectSystem sys;
    sys.reset(1);
    ObjectSpec spec = plainShape();
    const std::uint32_t id = sys.place(spec);
    check(id != 0, "a user's shape is placed");

    int rows = 0;
    int reached = 0;
    sys.pool().forEachTarget([&](std::uint32_t instance, std::string_view rowId, const ParamDef& def) {
        ++rows;
        char address[96];
        formatTarget(address, sizeof address, instance, rowId);
        const auto ref = sys.pool().resolve(address);
        if (!ref) {
            std::printf("  unresolved: %s\n", address);
            return;
        }
        const float before = sys.pool().effective(*ref);
        // Pull it 37 % up its range: every row's default is elsewhere, so the value must move.
        const float target = def.min + 0.37f * (def.max - def.min);
        sys.pool().modulate(*ref, Combine::Toward, target, 1.0f);
        const float after = sys.pool().effective(*ref);
        if (after != before) {
            ++reached;
        } else {
            std::printf("  not moved by its route: %s\n", address);
        }
    });
    check(rows == static_cast<int>(kShapeParamCount), "forEachTarget lists every row of the live shape");
    check(reached == rows, "every property of an object is reachable as a route target and moves under one");

    // Routes land in what is drawn: glow modulated this frame is the glow packed this frame.
    sys.beginFrame();
    const auto glow = sys.pool().resolve(id, "mat.glow");
    sys.pool().modulate(*glow, Combine::Add, 2.0f);
    FrameIn in;
    in.dt = 1.0f / 60.0f;
    sys.step(in);
    ShapeGpu g[4];
    check(sys.packShapes(g, 4) == 1 && near(g[0].material[1], 2.0f), "a route on mat.glow reaches the packed shape");
    sys.beginFrame();
    sys.step(in);
    sys.packShapes(g, 4);
    check(near(g[0].material[1], 0.0f), "a route never outlives its frame");

    sys.kill(id);
    for (int i = 0; i < 60; ++i) stepOnce(sys, 1.0f / 60.0f);
    check(!sys.pool().resolve(id, "mat.glow"), "an address for an object that has gone resolves to nothing");
}

// ---- The pool ---------------------------------------------------------------------------------------

void testPoolCaps() {
    ObjectPool pool;
    check(pool.capacity() == 64 && ObjectPool::kMaxShapesAndModels == 64, "the pool holds 64 shapes or models");
    ObjectSpec spec = plainShape();
    std::uint32_t first = 0;
    for (int i = 0; i < 64; ++i) {
        const SpawnResult r = pool.spawn(spec, Origin::Music);
        if (i == 0) first = r.instance;
        check(r.placement == Placement::Placed, "the first 64 are placed");
    }
    const SpawnResult over = pool.spawn(spec, Origin::Music);
    check(over.placement == Placement::PlacedEvicting && over.evicted == first, "past the cap, the oldest short-lived object goes");
    check(pool.live() == 64, "the live count never passes the cap");

    ObjectPool full;
    for (int i = 0; i < 64; ++i) full.spawn(spec, Origin::User);
    check(full.spawn(spec, Origin::Music).placement == Placement::Refused, "a pool of persistent objects refuses music objects");
    check(full.spawn(spec, Origin::User).placement == Placement::Refused, "and refuses users' too: nothing persistent is dropped");
    check(full.refused() == 2, "refusals are counted");

    ObjectPool mixed;
    for (int i = 0; i < 10; ++i) mixed.spawn(spec, Origin::User);
    std::uint32_t oldestMusic = 0;
    for (int i = 0; i < 54; ++i) {
        const auto r = mixed.spawn(spec, Origin::Music);
        if (i == 0) oldestMusic = r.instance;
    }
    const SpawnResult user = mixed.spawn(spec, Origin::User);
    check(user.placement == Placement::PlacedEvicting && user.evicted == oldestMusic, "a user's object takes the oldest music object's place");

    ObjectSpec light = spec;
    light.kind = Kind::Light;
    check(ObjectPool().spawn(light, Origin::User).placement == Placement::Refused, "lights are not this pool's kind");

    ObjectPool low;
    low.setLiveCap(16);
    for (int i = 0; i < 20; ++i) low.spawn(spec, Origin::Music);
    check(low.live() == 16 && low.evicted() == 4, "a lower detail's live cap holds the same way");

    // The same spawns give the same instances and the same evictions.
    ObjectPool a, b;
    bool same = true;
    for (int i = 0; i < 100; ++i) {
        const Origin o = (i % 7 == 0) ? Origin::User : Origin::Music;
        const SpawnResult ra = a.spawn(spec, o);
        const SpawnResult rb = b.spawn(spec, o);
        same = same && ra.instance == rb.instance && ra.evicted == rb.evicted && ra.placement == rb.placement;
    }
    check(same, "the pool decides the same way every time");
}

// ---- Lifecycle --------------------------------------------------------------------------------------

LifeLook lookAt(ObjectSystem& sys, std::uint32_t id) { return sys.look(id); }

void testLifecycleFade() {
    ObjectSystem sys;
    sys.reset(3);
    ObjectSpec s = plainShape();
    s.birth = 0.5f;
    s.fadeIn = 0.5f;
    s.lifetime = 2.0f;
    s.fadeOut = 1.0f;
    s.death = Death::Time;
    s.entry = Exit::Fade;
    s.exit = Exit::Fade;
    const std::uint32_t id = sys.place(s);
    Runner run{sys, 60.0f, {}};

    run.runTo(0.25);
    check(sys.pool().find(id)->life.phase == Phase::Waiting, "before its birth an object waits");
    check(lookAt(sys, id).opacity == 0.0f, "and is drawn at nothing");
    ShapeGpu g[4];
    check(sys.packShapes(g, 4) == 0, "a waiting object is not packed");

    run.runTo(0.75);
    check(sys.pool().find(id)->life.phase == Phase::Entering, "after its birth it enters");
    check(near(lookAt(sys, id).opacity, 0.5f), "halfway through a linear fade in it is half there");
    check(near(sys.pool().find(id)->body.age, 0.75f), "its age is seconds since spawn");

    run.runTo(1.5);
    check(sys.pool().find(id)->life.phase == Phase::Living && lookAt(sys, id).opacity == 1.0f, "then it lives, whole");
    check(sys.packShapes(g, 4) == 1 && near(g[0].life[1], 1.0f), "the shader reads seconds since birth");

    run.runTo(3.0);  // 2.5 s since birth: half a second into a one-second fade out
    check(sys.pool().find(id)->life.phase == Phase::Exiting, "its lifetime ends in its exit");
    check(near(lookAt(sys, id).opacity, 0.5f), "half way through a Fade exit it is half there");

    run.runTo(3.6);
    check(sys.pool().find(id) == nullptr && sys.pool().live() == 0, "when the exit is done its slot is free");
}

void testExits() {
    struct Case {
        Exit exit;
        float opacity, scale, burst, collapse;
        const char* what;
    };
    const Case cases[] = {
        {Exit::Fade, 0.5f, 1.0f, 0.0f, 0.0f, "Fade: opacity falls, scale holds"},
        {Exit::Collapse, 1.0f, 0.5f, 0.0f, 0.5f, "Collapse: scale falls to nothing, the shader implodes by the progress"},
        {Exit::Burst, 0.5f, 1.75f, 0.5f, 0.0f, "Burst: thrown out by its spread while it fades, the shader shatters by the progress"},
    };
    for (const Case& c : cases) {
        ObjectSystem sys;
        sys.reset(4);
        ObjectSpec s = plainShape();
        s.fadeIn = 0.0f;
        s.fadeOut = 1.0f;
        s.burstSpread = 1.5f;
        s.exit = c.exit;
        const std::uint32_t id = sys.place(s);
        Runner run{sys, 60.0f, {}};
        run.runTo(1.0);
        check(sys.kill(id), "kill starts an exit");
        run.runTo(1.5);
        const LifeLook lk = sys.look(id);
        check(near(lk.opacity, c.opacity) && near(lk.scale, c.scale) && near(lk.burst, c.burst) && near(lk.collapse, c.collapse), c.what);
        run.runTo(2.1);
        check(sys.pool().find(id) == nullptr, "every exit ends in a free slot");
    }

    // The entry runs the same kinds backwards: a Collapse entry grows out of nothing.
    ObjectSystem sys;
    sys.reset(5);
    ObjectSpec s = plainShape();
    s.fadeIn = 1.0f;
    s.entry = Exit::Collapse;
    const std::uint32_t id = sys.place(s);
    Runner run{sys, 60.0f, {}};
    run.runTo(0.5);
    check(near(sys.look(id).scale, 0.5f) && near(sys.look(id).opacity, 1.0f), "a Collapse entry grows from nothing");

    check(near(fadeCurve(0.5f, 1.0f), 0.5f) && near(fadeCurve(0.25f, 1.0f), 0.15625f) && near(fadeCurve(0.25f, 0.0f), 0.25f),
          "the fade curve runs linear to smoothstep");
}

void testDeathTriggers() {
    // Beats: the fourth beat after birth ends it.
    {
        ObjectSystem sys;
        sys.reset(6);
        ObjectSpec s = plainShape();
        s.fadeIn = 0.0f;
        s.death = Death::Beats;
        s.deathBeats = 4;
        const std::uint32_t id = sys.place(s);
        Runner run{sys, 60.0f, {{0.5, EventKind::Beat, 1}, {1.0, EventKind::Beat, 1}, {1.5, EventKind::Beat, 1}, {2.0, EventKind::Beat, 1}}};
        run.runTo(1.9);
        check(sys.pool().find(id)->life.phase == Phase::Living, "three beats do not end a four-beat life");
        run.runTo(2.0);
        check(sys.pool().find(id)->life.phase == Phase::Exiting, "the fourth does");
    }
    // Event: a drop ends it; other events do not.
    {
        ObjectSystem sys;
        sys.reset(7);
        ObjectSpec s = plainShape();
        s.fadeIn = 0.0f;
        s.death = Death::Event;
        s.deathEvent = EventKind::Drop;
        const std::uint32_t id = sys.place(s);
        Runner run{sys, 60.0f, {{0.5, EventKind::Beat, 1}, {0.6, EventKind::SectionChange, 1}, {1.0, EventKind::Drop, 1}}};
        run.runTo(0.9);
        check(sys.pool().find(id)->life.phase == Phase::Living, "events that are not its trigger leave it alone");
        run.runTo(1.0);
        check(sys.pool().find(id)->life.phase == Phase::Exiting, "its event ends it");
    }
    // Never: a user's object stays.
    {
        ObjectSystem sys;
        sys.reset(8);
        const std::uint32_t id = sys.place(plainShape());
        Runner run{sys, 30.0f, {}};
        run.runTo(120.0);
        check(sys.pool().find(id) != nullptr && sys.pool().find(id)->life.phase == Phase::Living, "Death::Never lives until killed");
    }
    // A route can end a life early: lifetime is a param.
    {
        ObjectSystem sys;
        sys.reset(9);
        ObjectSpec s = plainShape();
        s.fadeIn = 0.0f;
        s.death = Death::Time;
        s.lifetime = 100.0f;
        const std::uint32_t id = sys.place(s);
        sys.beginFrame();
        sys.pool().modulate(*sys.pool().resolve(id, "life.lifetime"), Combine::Toward, 0.0f, 1.0f);
        FrameIn in;
        in.dt = 1.0f / 60.0f;
        sys.step(in);
        check(sys.pool().find(id) == nullptr || sys.pool().find(id)->life.phase == Phase::Exiting, "a route on life.lifetime ends it");
    }
}

// ---- Spawners ---------------------------------------------------------------------------------------

SpawnRule dropRule() {
    SpawnRule r;
    r.trigger = EventKind::Drop;
    r.count = 3;
    r.lifetime = 1.0f;
    r.lifetimeJitter = 0.2f;
    r.base.fadeIn = 0.1f;
    r.base.fadeOut = 0.4f;
    r.base.exit = Exit::Burst;
    r.shapeMask = (1u << static_cast<int>(ShapeType::Sphere)) | (1u << static_cast<int>(ShapeType::Box)) |
                  (1u << static_cast<int>(ShapeType::Octahedron));
    return r;
}

std::vector<float> snapshot(const ObjectSystem& sys) {
    ShapeGpu g[64];
    const std::size_t n = sys.packShapes(g, 64);
    std::vector<float> out;
    for (std::size_t i = 0; i < n; ++i) {
        float f[sizeof(ShapeGpu) / sizeof(float)];
        std::memcpy(f, &g[i], sizeof f);
        out.insert(out.end(), f, f + sizeof(ShapeGpu) / sizeof(float));
    }
    return out;
}

void testSpawners() {
    const SpawnRule rules[] = {dropRule()};
    Declaration d;
    d.spawners = rules;
    d.spawnerCount = 1;

    ObjectSystem sys;
    sys.declare(d);
    sys.reset(42);
    Runner run{sys, 60.0f, {{0.5, EventKind::Beat, 1}, {0.6, EventKind::SectionChange, 1}, {0.7, EventKind::Onset, 1}}};
    run.runTo(1.0);
    check(sys.fires(0) == 0 && sys.pool().live() == 0, "a drop rule does not fire on beats, sections or onsets");
    run.events.push_back({1.5, EventKind::Drop, 1});
    run.runTo(1.5);
    check(sys.fires(0) == 1 && sys.pool().live() == 3, "it fires on a drop and throws its count");
    bool allMusic = true;
    for (std::size_t i = 0; i < sys.pool().capacity(); ++i) {
        const auto& s = sys.pool().slot(i);
        if (s.used) allMusic = allMusic && s.origin == Origin::Music && s.spawner == 0;
    }
    check(allMusic, "music objects are short-lived and know their rule");
    run.runTo(3.5);
    check(sys.pool().live() == 0, "and leave on their own");

    // everyNth and minStrength.
    SpawnRule picky = dropRule();
    picky.everyNth = 2;
    picky.minStrength = 0.5f;
    const SpawnRule pickyRules[] = {picky};
    Declaration pd;
    pd.spawners = pickyRules;
    pd.spawnerCount = 1;
    ObjectSystem ps;
    ps.declare(pd);
    ps.reset(1);
    Runner pr{ps, 60.0f, {{0.5, EventKind::Drop, 0.2f}, {1.0, EventKind::Drop, 1}, {1.5, EventKind::Drop, 1}, {2.0, EventKind::Drop, 1}}};
    pr.runTo(2.0);
    check(ps.fires(0) == 2, "a weak drop does not count; every second strong one fires");

    // The rule's own cap.
    SpawnRule capped = dropRule();
    capped.count = 10;
    capped.maxAlive = 4;
    capped.lifetime = 50.0f;
    const SpawnRule cappedRules[] = {capped};
    Declaration cd;
    cd.spawners = cappedRules;
    cd.spawnerCount = 1;
    ObjectSystem cs;
    cs.declare(cd);
    cs.reset(1);
    Runner cr{cs, 60.0f, {{0.5, EventKind::Drop, 1}, {1.0, EventKind::Drop, 1}}};
    cr.runTo(1.0);
    check(cs.pool().live() == 4, "a rule never has more than its maxAlive alive");

    // Deterministic per seed.
    auto play = [&](std::uint64_t seed) {
        ObjectSystem s;
        s.declare(d);
        s.reset(seed);
        Runner r{s, 60.0f, {{0.5, EventKind::Drop, 1}, {1.0, EventKind::Drop, 1}}};
        r.runTo(1.2);
        return snapshot(s);
    };
    const auto a = play(7);
    const auto b = play(7);
    const auto c = play(8);
    check(!a.empty() && a == b, "the same seed and the same frames give the same objects, bit for bit");
    check(a != c, "another seed gives other objects");

    // CameraFront: thrown into view from the camera at the firing, then left in the world.
    SpawnRule front = dropRule();
    front.count = 1;
    front.radius = 0.0f;
    front.speed = 0.0f;
    front.speedJitter = 0.0f;
    front.centre = {0.0f, 0.0f, 4.0f};
    const SpawnRule frontRules[] = {front};
    Declaration fd;
    fd.spawners = frontRules;
    fd.spawnerCount = 1;
    ObjectSystem fs;
    fs.declare(fd);
    fs.reset(1);
    Runner fr{fs, 60.0f, {{0.5, EventKind::Drop, 1}}};
    fr.camera.pos = {10.0f, 0.0f, 0.0f};
    fr.camera.orient = axisAngle({0.0f, 1.0f, 0.0f}, kPi * 0.5f);  // facing +X
    fr.runTo(0.5);
    fr.camera.pos = {0.0f, 0.0f, 0.0f};
    fr.runTo(0.7);
    ShapeGpu g[4];
    check(fs.packShapes(g, 4) == 1 && near(g[0].posScale[0], 14.0f) && near(g[0].posScale[2], 0.0f),
          "a CameraFront spawn lands in front of the camera of its firing and stays in the world");
}

void testStartingObjectsAndUsers() {
    ObjectSpec start = plainShape();
    start.shape = ShapeType::Torus;
    const ObjectSpec starting[] = {start, start};
    Declaration d;
    d.starting = starting;
    d.startingCount = 2;
    ObjectSystem sys;
    sys.reset(1);
    const std::uint32_t mine = sys.place(plainShape());
    sys.declare(d);
    check(sys.pool().live() == 3 && sys.pool().find(mine) != nullptr, "a family's starting objects join; the user's stay");
    sys.declare(Declaration{});
    check(sys.pool().live() == 1 && sys.pool().find(mine) != nullptr, "a new family takes its objects away, not the user's");
    sys.declare(d);
    sys.reset(2);
    check(sys.pool().live() == 2, "a new play places the family's starting objects again");
}

// ---- Anchors and camera targets ---------------------------------------------------------------------

void testAnchorsAndTargets() {
    ObjectSystem sys;
    sys.reset(1);
    ObjectSpec rider = plainShape();
    rider.anchor = Anchor::Camera;
    rider.pos = {0.0f, 0.0f, 2.0f};
    const std::uint32_t r = sys.place(rider);
    camera::Pose cam;
    cam.pos = {5.0f, 0.0f, 0.0f};
    sys.beginFrame();
    FrameIn in;
    in.dt = 1.0f / 60.0f;
    in.camera = cam;
    sys.step(in);
    Vec3 p;
    Quat q;
    sys.worldPose(r, p, q);
    check(near(p[0], 5.0f) && near(p[2], 2.0f), "a Camera-anchored object rides in the camera's frame");
    sys.kill(r);

    ObjectSpec subject = plainShape();
    subject.anchor = Anchor::Target;
    subject.pos = {0.0f, 0.0f, 10.0f};
    subject.targetDistance = 3.0f;
    subject.targetHeight = 0.0f;
    subject.fadeIn = 0.0f;
    const TargetMode modes[] = {TargetMode::LookAt, TargetMode::LockOn, TargetMode::Orbit, TargetMode::Follow};
    for (TargetMode m : modes) {
        ObjectSystem ts;
        ts.reset(1);
        subject.target = m;
        subject.vel = {1.0f, 0.0f, 0.0f};
        ts.place(subject);
        stepOnce(ts, 1.0f / 60.0f);
        camera::Pose here;
        here.pos = {0.0f, 0.0f, 0.0f};
        const CameraTarget t = ts.cameraTarget(here);
        check(t.valid && t.mode == m && near(t.weight, 1.0f), "a Target-anchored object offers the camera a subject");
        const Vec3 toSubject = normalize(sub(t.subject, t.pose.pos));
        check(near(dot(fwdOf(t.pose.orient), toSubject), 1.0f), "every target mode looks at its subject");
        check(near(dot(upOf(t.pose.orient), {1.0f, 0.0f, 0.0f}), 0.0f, 1e-2f), "and keeps the camera's up, not a roll");
        const float d = length(sub(t.subject, t.pose.pos));
        if (m == TargetMode::LookAt) {
            check(near(t.pose.pos[2], 0.0f), "LookAt leaves the camera where it flies");
        } else {
            check(near(d, 3.0f, 2e-2f), "LockOn, Orbit and Follow keep target.distance");
        }
        if (m == TargetMode::Follow) check(t.pose.pos[0] < t.subject[0], "Follow trails behind its motion");
    }

    ObjectSystem none;
    none.reset(1);
    none.place(plainShape());
    stepOnce(none, 1.0f / 60.0f);
    check(!none.cameraTarget(camera::Pose{}).valid, "a World-anchored object offers no subject");
}

// ---- 30, 60, 120 fps --------------------------------------------------------------------------------

void testFrameRates() {
    ObjectSpec spinner = plainShape();
    spinner.vel = {0.3f, -0.2f, 0.1f};
    spinner.spin = {0.0f, 1.3f, 0.4f};
    spinner.birth = 0.35f;  // born between frames at every rate: the partial frame is integrated exactly
    spinner.fadeIn = 0.4f;
    const ObjectSpec starting[] = {spinner};
    SpawnRule beatRule = dropRule();
    beatRule.trigger = EventKind::Beat;
    beatRule.count = 2;
    beatRule.stagger = 0.11f;
    const SpawnRule rules[] = {dropRule(), beatRule};
    Declaration d;
    d.starting = starting;
    d.startingCount = 1;
    d.spawners = rules;
    d.spawnerCount = 2;

    std::vector<Timed> events;
    for (int i = 1; i <= 6; ++i) events.push_back({i * 0.5, EventKind::Beat, 1});
    events.push_back({2.0, EventKind::Drop, 1});

    auto play = [&](float fps, double t) {
        ObjectSystem s;
        s.declare(d);
        s.reset(99);
        Runner r{s, fps, events};
        r.runTo(t);
        return snapshot(s);
    };
    for (double t : {1.0, 2.2, 3.0}) {
        const auto a = play(30.0f, t);
        const auto b = play(60.0f, t);
        const auto c = play(120.0f, t);
        bool same = !a.empty() && a.size() == b.size() && a.size() == c.size();
        for (std::size_t i = 0; same && i < a.size(); ++i) same = near(a[i], b[i], 2e-3f) && near(a[i], c[i], 2e-3f);
        check(same, "30, 60 and 120 fps give the same objects at the frames they share");
    }
}

}  // namespace

int main() {
    testSchemaAndAddressing();
    testEveryPropertyIsARouteTarget();
    testPoolCaps();
    testLifecycleFade();
    testExits();
    testDeathTriggers();
    testSpawners();
    testStartingObjectsAndUsers();
    testAnchorsAndTargets();
    testFrameRates();
    if (failures == 0) std::printf("scene_object_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
