// Host checks for the Event Responder's picture kinds (core/viz/things/ThingResponse, fronts/FrontResponse,
// fields/FieldResponse) played by the bank: each kind firing makes exactly one item, or N for a burst, and nothing
// before its event or on a row that is off; the six kind-params read as documented; a seeded origin repeats across
// two runs and the middle stands in for a pitch that is not heard; Follow spawns at a rate set by the level and
// capped; a freeze holds Things, Fronts and Fields in place and a reverse never runs them back; Collide events
// come back to the bank as the new, last Moment, off by default, without feeding themselves; the flow clocks follow
// the scene clock's step. Built and run by CI (native-host-tests.yml); no GL.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "viz/clock/ClockResponse.hpp"
#include "viz/fields/FieldResponse.hpp"
#include "viz/fronts/FrontResponse.hpp"
#include "viz/modulation/Reactions.hpp"
#include "viz/modulation/Responder.hpp"
#include "viz/modulation/SpeedClock.hpp"
#include "viz/things/ThingResponse.hpp"

using namespace geode::viz;
namespace resp = geode::viz::responder;
using fields::FieldKind;
using fields::FieldSet;
using fronts::FrontField;
using fronts::FrontKind;
using sceneclock::SceneClock;
using things::ThingKind;
using things::ThingPool;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

constexpr int kKick = static_cast<int>(Moment::Kick);
constexpr int kSnare = static_cast<int>(Moment::Snare);
constexpr int kCollide = static_cast<int>(Moment::Collide);
constexpr float kDt = 1.0f / 60.0f;

// The matrix, the moments and the bank wired as the resolver wires them, with every picture kind registered over its
// own system, and the systems stepped as the renderer steps them: by the scene clock's sim step.
struct Rig {
    ReactionEngine engine;
    MomentWatch watch;
    resp::Bank bank;
    FrontField fronts;
    ThingPool things;
    FieldSet fields;
    SceneClock clock;
    FrontKind frontKind{fronts};
    ThingKind thingKind{things};
    FieldKind fieldKind{fields};
    std::array<ReactRoute, ReactionEngine::kRoutes> hand{};
    std::uint32_t seen = 0;

    Rig() {
        bank.registerKind(resp::kThing, &thingKind);
        bank.registerKind(resp::kFront, &frontKind);
        bank.registerKind(resp::kField, &fieldKind);
    }

    void load(const resp::Config& config) {
        bank.configure(config);
        engine.routes = hand;
        bank.compile(engine.routes);
    }
    void step(const GeodeSoundFrame& f) {
        engine.stepSignals(f, kDt);
        const std::uint32_t moments = watch.step(engine.signals());
        seen |= moments;
        bank.step(f, kDt, engine.signals(), moments, engine);
        engine.tickRoutes();
        clock.step(kDt);
        const float sim = clock.sim(kDt);
        fronts.step(sim);
        things.step(sim, &fields);
        fields.step(sim);
    }
    // A quiet frame, or one with a kick (and the kick's level) or a snare.
    static GeodeSoundFrame frame(bool kick = false, float level = 0.0f, bool snare = false) {
        GeodeSoundFrame f{};
        f.drive = 1.0f;
        if (kick) f.kick = 1.0f;
        if (snare) f.snare = 1.0f;
        f.kickEnv = level;
        return f;
    }
    void kick() { step(frame(true)); }
    void snare() { step(frame(false, 0.0f, true)); }
    void run(int n, float level = 0.0f) {
        for (int i = 0; i < n; ++i) step(frame(false, level));
    }
};

resp::Response make(int kind, int row = kKick) {
    resp::Response r;
    r.row = row;
    r.kind = kind;
    r.fadeIn = 0.05f;
    r.hold = 0.1f;
    r.fadeOut = 0.3f;
    return r;
}

resp::Config rowsOn(std::initializer_list<int> rows) {
    resp::Config c = resp::newStyleConfig();
    for (const int row : rows) resp::setRowOn(c, row, true);
    return c;
}

resp::Config oneOn(const resp::Response& r) {
    resp::Config c = rowsOn({r.row});
    resp::addResponse(c, r);
    return c;
}

// ── What a firing makes ──────────────────────────────────────────────────────────────────────────────────

void eachKindMakesExactlyOneItem() {
    {
        Rig rig;
        rig.load(oneOn(make(resp::kFront)));
        rig.run(20);
        check(rig.fronts.count() == 0, "a Fronts response makes nothing before its event");
        rig.kick();
        check(rig.fronts.count() == 1, "a Fronts response makes exactly one front when its event comes");
        rig.run(5);
        check(rig.fronts.count() == 1, "and no more while the event is over");
    }
    {
        Rig rig;
        rig.load(oneOn(make(resp::kThing)));
        rig.run(20);
        check(rig.things.count() == 0, "a Things response makes nothing before its event");
        rig.kick();
        check(rig.things.count() == 1, "a Things response makes exactly one Thing when its event comes");
        rig.run(5);
        check(rig.things.count() == 1, "and no more while the event is over");
    }
    {
        Rig rig;
        rig.load(oneOn(make(resp::kField)));
        rig.run(20);
        check(rig.fields.count() == 0, "a Fields response makes nothing before its event");
        rig.kick();
        check(rig.fields.count() == 1, "a Fields response makes exactly one Field when its event comes");
        rig.run(5);
        check(rig.fields.count() == 1, "and no more while the event is over");
    }
}

void aRowThatIsOffMakesNothing() {
    Rig rig;
    resp::Config c = resp::newStyleConfig();
    resp::addResponse(c, make(resp::kFront));
    resp::addResponse(c, make(resp::kThing));
    resp::addResponse(c, make(resp::kField));
    rig.load(c);
    for (int i = 0; i < 3; ++i) {
        rig.kick();
        rig.run(10);
    }
    check(rig.fronts.count() == 0 && rig.things.count() == 0 && rig.fields.count() == 0,
          "no row is on in a new style, so no kick spawns anything");
}

void aBurstMakesN() {
    Rig rig;
    resp::Response f = make(resp::kFront);
    f.params[FrontKind::kCount] = 3.0f;
    resp::Response d = make(resp::kField);
    d.params[FieldKind::kCount] = 4.0f;
    resp::Config c = rowsOn({kKick});
    resp::addResponse(c, f);
    resp::addResponse(c, d);
    rig.load(c);
    rig.kick();
    check(rig.fronts.count() == 3, "a Fronts count of 3 sends three fronts");
    check(rig.fields.count() == 4, "a Fields count of 4 makes four Fields");

    Rig many;
    resp::Response big = make(resp::kFront);
    big.params[FrontKind::kCount] = 99.0f;
    resp::Response bigF = make(resp::kField);
    bigF.params[FieldKind::kCount] = 99.0f;
    resp::Config c2 = rowsOn({kKick});
    resp::addResponse(c2, big);
    resp::addResponse(c2, bigF);
    many.load(c2);
    many.kick();
    check(many.fronts.count() == fronts::kMaxFronts && many.fields.count() == fields::kMaxFields, "a count past the room is held to the room");
}

// ── The kind-params ──────────────────────────────────────────────────────────────────────────────────────

void frontParamsMapToTheSpawn() {
    Rig rig;
    resp::Response r = make(resp::kFront);
    r.params[FrontKind::kShape] = 2.0f;                                   // Wedge
    r.params[FrontKind::kEffect] = 4.0f;                                  // Reveal
    r.params[FrontKind::kOrigin] = static_cast<float>(resp::Place::Top);  // from the top edge
    r.params[FrontKind::kSpeed] = 0.9f;
    r.params[FrontKind::kSize] = 0.12f;
    rig.load(oneOn(r));
    rig.kick();
    const fronts::Packed p = rig.fronts.pack();
    check(p.count == 1, "one front");
    check(p.data[4] == 2.0f && p.data[5] == 4.0f, "shape and effect");
    check(near(p.data[0], 0.5f, 1e-5) && near(p.data[1], 1.0f, 1e-5) && near(p.data[3], -1.0f, 1e-5), "from the top edge, travelling down");
    check(near(p.data[7], 0.12f, 1e-6), "the size is the band's half width");
    check(near(rig.fronts.distance(0) / rig.fronts.age(0), 0.9f, 1e-3), "the speed is frame heights a second");
    rig.run(20);
    check(rig.fronts.alive(0), "it lives through its fade in and hold");
    rig.run(20);
    check(!rig.fronts.alive(0), "and ends at fade in + hold + fade out");

    Rig plain;
    plain.load(oneOn(make(resp::kFront)));
    plain.kick();
    const fronts::Packed q = plain.fronts.pack();
    check(near(q.data[0], 0.5f, 1e-5) && near(q.data[1], 0.5f, 1e-5), "with no origin set a front starts in the middle");
    check(near(q.data[7], FrontKind::kDefaultWidth, 1e-6) && near(plain.fronts.distance(0) / plain.fronts.age(0), FrontKind::kDefaultSpeed, 1e-3),
          "a size and a speed of 0 read as the defaults");
}

void thingParamsMapToTheSpawn() {
    Rig rig;
    resp::Response r = make(resp::kThing);
    r.params[ThingKind::kShape] = 1.0f;       // Ring
    r.params[ThingKind::kBehaviour] = 2.0f;   // Gravity
    r.params[ThingKind::kOrigin] = static_cast<float>(resp::Place::Top);
    r.params[ThingKind::kSize] = 0.1f;
    r.params[ThingKind::kChain] = 2.0f;
    rig.load(oneOn(r));
    rig.kick();
    const things::View v = rig.things.view(0);
    check(v.alive && v.shape == things::Shape::Ring, "the shape");
    check(v.y > 1.0f && v.vy < 0.0f, "from the top edge, crossing down");
    check(v.size > 0.05f - 1e-6f && v.size <= 0.1f + 1e-6f, "the size is the radius, scaled by the strength from half to full");
    check(v.chain == 2, "the chain depth");

    Rig soft;
    Rig hard;
    resp::Response s = make(resp::kThing);
    s.params[ThingKind::kSize] = 0.2f;
    soft.load(oneOn(s));
    hard.load(oneOn(s));
    GeodeSoundFrame weak = Rig::frame(true);
    weak.kick = 0.3f;
    soft.step(weak);
    hard.kick();
    check(soft.things.count() == 1 && soft.things.view(0).size < hard.things.view(0).size, "a harder hit makes a bigger Thing");
}

void morphAndMeltActOnTheLatestOrTheOldest() {
    Rig rig;
    resp::Response born = make(resp::kThing);
    born.hold = 10.0f;
    resp::Response morph = make(resp::kThing, kSnare);
    morph.params[ThingKind::kBehaviour] = static_cast<float>(ThingKind::kMorph);
    morph.params[ThingKind::kShape] = 3.0f;  // into a Blob
    morph.hold = 1.0f;
    resp::Config c = rowsOn({kKick, kSnare});
    resp::addResponse(c, born);
    resp::addResponse(c, morph);
    rig.load(c);
    rig.kick();
    rig.run(5);
    rig.kick();
    check(rig.things.count() == 2, "two Things born");
    rig.snare();
    check(rig.things.count() == 2, "a Morph response makes nothing");
    check(rig.things.view(1).shape == things::Shape::Blob && rig.things.view(0).shape == things::Shape::Dot,
          "it acts on the Thing a response of the kind made last");

    Rig oldest;
    oldest.load(oneOn(morph));
    things::Spawn a;
    a.life_s = 10.0f;
    oldest.things.spawn(a);
    oldest.run(6);
    oldest.things.spawn(a);
    oldest.snare();
    check(oldest.things.view(0).shape == things::Shape::Blob && oldest.things.view(1).shape == things::Shape::Dot,
          "with none made by a response it acts on the oldest live one");

    Rig melt;
    resp::Response m = make(resp::kThing, kSnare);
    m.params[ThingKind::kBehaviour] = static_cast<float>(ThingKind::kMelt);
    m.hold = 0.2f;
    melt.load(oneOn(m));
    things::Spawn b;
    b.life_s = 10.0f;
    melt.things.spawn(b);
    melt.snare();
    check(melt.things.remaining(0) < 1.0f && melt.things.count() == 1, "a Melt brings the Thing's end forward and makes nothing");
    melt.run(5);
    check(melt.things.meltProgress(0) > 0.0f, "and it melts");
}

void fieldParamsMapToTheSpawn() {
    Rig rig;
    resp::Response r = make(resp::kField);
    r.params[FieldKind::kKind] = 2.0f;  // Swirl
    r.params[FieldKind::kTurn] = 1.0f;  // clockwise
    r.params[FieldKind::kRadius] = 0.25f;
    r.params[FieldKind::kOrigin] = static_cast<float>(resp::Place::Left);
    rig.load(oneOn(r));
    rig.kick();
    rig.run(8);
    const fields::View v = rig.fields.view(0);
    check(v.alive && v.kind == fields::Kind::Swirl && near(v.radius, 0.25f, 1e-6), "the kind and the radius");
    check(v.strength < 0.0f, "a clockwise Swirl is negative");
    check(near(v.x, 0.0f, 1e-4) && near(v.y, 0.5f, 1e-6), "from the left edge");

    Rig attract;
    resp::Response a = make(resp::kField);
    attract.load(oneOn(a));
    attract.kick();
    attract.run(8);
    const fields::View w = attract.fields.view(0);
    check(w.kind == fields::Kind::Attract && w.strength > 0.0f && w.strength <= fields::kMaxStrength && near(w.radius, FieldKind::kDefaultRadius, 1e-6),
          "an Attract Field pulls, at its default radius when the radius is 0");
}

// The places the sound gives: played directly, so the signals' own smoothing is not in question.
void aPitchAndAPanAreWhereTheyPutThings() {
    FieldSet fs;
    FieldKind kind{fs};
    resp::Response byPitch = make(resp::kField);
    byPitch.params[FieldKind::kOrigin] = static_cast<float>(resp::Place::Pitch);
    resp::Response byPan = make(resp::kField);
    byPan.params[FieldKind::kOrigin] = static_cast<float>(resp::Place::Pan);
    kind.tick(kDt, 0.8f, 0.2f, 0.0f);
    kind.fire(resp::ResponseKindHandler::Play{0, &byPitch, 1.0f, 0.05f, 0.1f, 0.3f, 7u, 0.0f});
    kind.fire(resp::ResponseKindHandler::Play{1, &byPan, 1.0f, 0.05f, 0.1f, 0.3f, 7u, 0.0f});
    check(near(fs.view(0).x, 0.5f, 1e-6) && near(fs.view(0).y, 0.8f, 1e-6), "the pitch is the height, across the middle");
    check(near(fs.view(1).x, 0.2f, 1e-6) && near(fs.view(1).y, 0.5f, 1e-6), "the pan is the place across, halfway up");
}

void noPitchHeardIsTheMiddle() {
    Rig rig;
    resp::Response r = make(resp::kField);
    r.params[FieldKind::kOrigin] = static_cast<float>(resp::Place::Pitch);
    rig.load(oneOn(r));
    GeodeSoundFrame f = Rig::frame(true);
    f.pitchHeight = 0.9f;
    f.pitchConfidence = 0.0f;
    rig.step(f);
    check(near(rig.fields.view(0).y, 0.5f, 1e-6), "a pitch height with no pitch heard is not used: the middle");
}

// ── Seeded places ────────────────────────────────────────────────────────────────────────────────────────

struct Trace {
    std::vector<std::array<float, fields::kMaxFields * fields::kFloatsPerField>> fieldData;
    std::vector<std::array<float, fronts::kMaxFronts * fronts::kFloatsPerFront>> frontData;
};

Trace randomRun(int firstIndex) {
    Rig rig;
    resp::Response f = make(resp::kField);
    f.params[FieldKind::kOrigin] = static_cast<float>(resp::Place::Random);
    f.params[FieldKind::kCount] = 2.0f;
    f.hold = 5.0f;
    resp::Response d = make(resp::kFront);
    d.params[FrontKind::kOrigin] = static_cast<float>(resp::Place::Random);
    d.params[FrontKind::kShape] = 1.0f;
    d.hold = 5.0f;
    resp::Config c = rowsOn({kKick, kSnare});
    // Padding in front moves the pair to another place in the config, which is another seed.
    for (int i = 0; i < firstIndex; ++i) resp::addResponse(c, make(resp::kParam, kSnare));
    resp::addResponse(c, f);
    resp::addResponse(c, d);
    rig.load(c);
    Trace t;
    for (int i = 0; i < 100; ++i) {
        if (i % 25 == 0) {
            rig.kick();
        } else {
            rig.run(1);
        }
        t.fieldData.push_back(rig.fields.pack().data);
        t.frontData.push_back(rig.fronts.pack().data);
    }
    return t;
}

void seededPlacesMatchAcrossTwoRuns() {
    const Trace a = randomRun(0);
    const Trace b = randomRun(0);
    check(a.fieldData == b.fieldData && a.frontData == b.frontData, "two runs of the same frames put every Field and front in the same place");
    const Trace c = randomRun(3);
    check(a.fieldData != c.fieldData, "another place in the config is another seed");
    const auto& end = a.fieldData.back();
    check(end[0] != end[fields::kFloatsPerField] || end[1] != end[fields::kFloatsPerField + 1], "a burst draws each Field its own point");
    check(a.fieldData[0][0] != a.fieldData[26][2 * fields::kFloatsPerField] || a.fieldData[0][1] != a.fieldData[26][2 * fields::kFloatsPerField + 1],
          "and the next fire draws new ones");
}

// ── Follow ───────────────────────────────────────────────────────────────────────────────────────────────

int thingsMadeFollowing(float level, int frames) {
    Rig rig;
    resp::Response r = make(resp::kThing);
    r.drive = resp::DriveMode::Follow;
    r.hold = 20.0f;  // none ends while it is counted
    rig.load(oneOn(r));
    rig.run(frames, level);
    return rig.things.count();
}

void followSpawnsAtTheLevelsRateUpToTheCap() {
    const int seconds = 2;
    const int frames = seconds * 60;
    const int none = thingsMadeFollowing(0.0f, frames);
    const int low = thingsMadeFollowing(0.25f, frames);
    const int full = thingsMadeFollowing(1.0f, frames);
    const int cap = static_cast<int>(resp::kFollowMaxPerSecond) * seconds;
    check(none == 0, "a level of 0 spawns nothing");
    check(low > 0 && low < full, "a lower level spawns slower");
    check(full <= cap + 1 && full >= cap - 2, "the highest level spawns at the cap and no faster");
}

void followKeepsOneFieldAtTheLevel() {
    Rig loud;
    Rig soft;
    resp::Response r = make(resp::kField);
    r.drive = resp::DriveMode::Follow;
    loud.load(oneOn(r));
    soft.load(oneOn(r));
    loud.run(60, 1.0f);
    soft.run(60, 0.5f);
    check(loud.fields.count() == 1 && soft.fields.count() == 1, "a followed Field is one Field");
    const float a = loud.fields.view(0).strength;
    const float b = soft.fields.view(0).strength;
    check(a > b && b > 0.0f, "its strength follows the level");
    loud.run(30, 0.0f);
    check(loud.fields.view(0).strength < 1e-3f, "and lets go when the level does");
    loud.run(200, 1.0f);
    float most = 0.0f;
    for (int i = 0; i < fields::kMaxFields; ++i) most = std::max(most, loud.fields.view(i).strength);
    check(loud.fields.count() >= 1 && loud.fields.count() <= 2 && most > 0.9f * fields::kMaxStrength,
          "it is handed on before it ends, never piling up, and the level carries on");
}

// ── The clock ────────────────────────────────────────────────────────────────────────────────────────────

// One of each, moving, and the clock stepped as the renderer does: the scene step, then the simulations by sim().
struct Sim {
    SceneClock clock;
    FrontField fronts;
    ThingPool things;
    FieldSet fields;
    Sim() {
        fronts::Spawn f;
        f.speed = 0.4f;
        f.life_s = 20.0f;
        fronts.spawnFront(f);
        things::Spawn t;
        t.vx = 0.1f;
        t.life_s = 20.0f;
        t.x = 0.2f;
        things.spawn(t);
        fields::Spawn d;
        d.vx = 0.05f;
        d.life_s = 20.0f;
        d.x = 0.8f;
        d.y = 0.9f;
        fields.spawn(d);
    }
    void step() {
        clock.step(kDt);
        const float sim = clock.sim(kDt);
        fronts.step(sim);
        things.step(sim, &fields);
        fields.step(sim);
    }
    void run(int n) {
        for (int i = 0; i < n; ++i) step();
    }
    std::array<float, 4> where() const { return {fronts.distance(0), fronts.age(0), things.view(0).x, fields.view(0).x}; }
};

void aFreezeHoldsThingsFrontsAndFields() {
    Sim s;
    s.run(30);
    const auto moving = s.where();
    s.clock.freeze(0.0f, -1.0f, 0.0f);
    s.step();
    const auto held = s.where();
    s.run(60);
    check(s.where() == held, "a frozen clock holds a front, a Thing and a Field exactly where they are");
    check(held[0] > 0.0f && held[2] > 0.2f && held[3] > 0.8f && moving[0] > 0.0f, "(they had been moving)");
    s.clock.release();
    s.run(60);
    check(s.where()[0] > held[0] && s.where()[2] > held[2] && s.where()[3] > held[3], "and they carry on when it lets go");
}

void aReverseNeverRunsThemBackwards() {
    Sim s;
    s.run(30);
    s.clock.reverse(1.0f, 0.0f, -1.0f, 0.0f);
    s.step();
    const auto first = s.where();
    const double clockThen = s.clock.value();
    bool backwards = false;
    for (int i = 0; i < 60; ++i) {
        const auto before = s.where();
        s.step();
        const auto after = s.where();
        for (size_t k = 0; k < before.size(); ++k) backwards = backwards || after[k] < before[k];
    }
    check(!backwards && s.where() == first, "a reversed clock never moves a front, a Thing or a Field backwards: they hold");
    check(s.clock.value() < clockThen, "while the clock itself did run back");
}

void theFlowClocksFollowTheSceneStep() {
    speedclock::FlowClock c;
    SceneClock sc;
    for (int i = 0; i < 60; ++i) c.step(1.0f, sc.step(kDt));
    const double run = c.time;
    check(run > 0.0 && c.time == c.phase, "at rest the flow clocks run at their base rate");
    sc.freeze(0.0f, -1.0f, 0.0f);
    for (int i = 0; i < 30; ++i) c.step(1.0f, sc.step(kDt));
    const double held = c.time;
    check(held - run <= static_cast<double>(kDt) * speedclock::FlowClock::kBaseHz, "a freeze stops them within a frame");
    const double stopped = c.time;
    for (int i = 0; i < 30; ++i) c.step(1.0f, sc.step(kDt));
    check(c.time == stopped && c.phase >= 0.0, "and holds them");
    sc.release();
    sc.reverse(1.0f, 0.0f, -1.0f, 0.0f);
    for (int i = 0; i < 30; ++i) c.step(1.0f, sc.step(kDt));
    check(c.time < stopped && c.time >= 0.0 && c.phase < stopped + 1e-9, "a reverse runs them back, inside their wrap");
    speedclock::FlowClock zero;
    zero.step(1.0f, -1.0f);
    check(zero.time > 0.0 && zero.time < speedclock::FlowClock::kWrapSeconds && zero.phase > 0.0, "a step back past the start wraps round, never negative");
}

// The flow clock is the scene clock's value at the flow rate, whatever the clock does: reversed it runs back, in a
// stutter it loops with it, and at the stutter's end it takes the same jump.
void theFlowClockTracksReverseAndStutter() {
    const double rate = speedclock::FlowClock::kBaseHz;
    {
        SceneClock sc;
        speedclock::FlowClock c;
        for (int i = 0; i < 120; ++i) c.step(1.0f, sc.step(kDt));
        const double before = c.time;
        sc.reverse(1.0f, 0.0f, -1.0f, 0.0f);
        for (int i = 0; i < 60; ++i) c.step(1.0f, sc.step(kDt));
        check(c.time < before - 0.5 * rate && near(c.time, sc.value() * rate, 1e-6), "a reversed clock runs the flow clock back with it");
    }
    {
        SceneClock sc;
        speedclock::FlowClock c;
        for (int i = 0; i < 120; ++i) c.step(1.0f, sc.step(kDt));
        sc.stutter(0.5f, 3);
        bool looped = false;
        bool tracked = true;
        double high = c.time;
        for (int i = 0; i < 240; ++i) {
            c.step(1.0f, sc.step(kDt));
            looped = looped || c.time < high - 0.1 * rate;
            high = std::max(high, c.time);
            tracked = tracked && near(c.time, sc.value() * rate, 1e-6);
        }
        check(looped, "a stutter loops the flow clock back");
        check(tracked, "and it stays with the scene clock through the loops and the jump at their end");
    }
}

// ── Collisions ───────────────────────────────────────────────────────────────────────────────────────────

void collideIsTheLastMomentAndTheSoundNeverRaisesIt() {
    static_assert(Moment::Collide == static_cast<Moment>(kMomentCount - 1) && static_cast<int>(Moment::KeyChange) == kMomentCount - 2,
                  "Collide is appended to the Moments");
    static_assert(resp::kRowNames[kCollide] == "COLLIDE", "and has a row");
    Rig rig;
    for (int i = 0; i < 200; ++i) {
        GeodeSoundFrame f = Rig::frame(i % 10 == 0, i % 10 == 0 ? 1.0f : 0.0f, i % 7 == 0);
        f.rms = 1.0f;
        rig.step(f);
    }
    check((rig.seen & (1u << kCollide)) == 0u, "no sound ever raises the Collide moment");
}

void aCollisionReachesTheBankAsTheMoment() {
    struct Counter : resp::ResponseKindHandler {
        std::vector<Play> plays;
        void fire(const Play& p) override { plays.push_back(p); }
    } counter;
    constexpr int kTest = 5;
    resp::Bank bank;
    bank.registerKind(kTest, &counter);
    ReactionEngine engine;
    MomentWatch watch;
    resp::Config c = rowsOn({kCollide});
    resp::addResponse(c, make(kTest, kCollide));
    bank.configure(c);
    std::array<ReactRoute, ReactionEngine::kRoutes> hand{};
    engine.routes = hand;
    bank.compile(engine.routes);
    const auto step = [&] {
        const GeodeSoundFrame f = Rig::frame();
        engine.stepSignals(f, kDt);
        bank.step(f, kDt, engine.signals(), watch.step(engine.signals()), engine);
    };
    step();
    check(counter.plays.empty(), "a Collide response waits for a collision");
    bank.hear(Moment::Collide, 0.25f);
    check(counter.plays.empty(), "it is heard on the next step");
    step();
    check(counter.plays.size() == 1 && near(counter.plays[0].strength, 0.25, 1e-5), "a collision plays the row's responses at the collision's strength");
    step();
    check(counter.plays.size() == 1, "once");
    bank.hear(Moment::Collide, 0.2f);
    bank.hear(Moment::Collide, 0.6f);
    step();
    check(counter.plays.size() == 2 && near(counter.plays[1].strength, 0.6, 1e-5), "several in one frame are one, at the strongest");

    resp::Bank off;
    off.registerKind(kTest, &counter);
    resp::Config c2 = resp::newStyleConfig();
    resp::addResponse(c2, make(kTest, kCollide));
    off.configure(c2);
    engine.routes = hand;
    off.compile(engine.routes);
    off.hear(Moment::Collide, 1.0f);
    const GeodeSoundFrame f = Rig::frame();
    engine.stepSignals(f, kDt);
    off.step(f, kDt, engine.signals(), 0u, engine);
    check(counter.plays.size() == 2, "the Collide row is off by default like every row: a collision plays nothing");
}

// Two Things on top of each other: one contact, one Collide event.
void touching(ThingPool& pool, float x, float y, float life = 10.0f) {
    things::Spawn s;
    s.x = x;
    s.y = y;
    s.size = 0.05f;
    s.life_s = life;
    pool.spawn(s);
    s.x = x + 0.01f;
    pool.spawn(s);
}

void collisionsBecomeASongEventThatSpawnsAResponse() {
    Rig rig;
    resp::Response r = make(resp::kField, kCollide);
    rig.load(oneOn(r));
    touching(rig.things, 0.3f, 0.3f);
    rig.run(1);
    const float hit = rig.thingKind.drainCollisions();
    check(near(hit, 1.0f / ThingKind::kMaxCollisionsPerFrame, 1e-6), "one contact is one share of the strength");
    check(rig.fields.count() == 0, "nothing came of it until it is heard");
    rig.bank.hear(Moment::Collide, hit);
    rig.run(1);
    check(rig.fields.count() == 1, "a collision between Things makes the Collide row's Field");
    check(rig.thingKind.drainCollisions() == 0.0f, "the events were drained, and are heard once");
}

void aFrameOfCollisionsIsCapped() {
    ThingPool pool;
    ThingKind kind{pool};
    for (int i = 0; i < 6; ++i) touching(pool, 0.1f + 0.15f * static_cast<float>(i), 0.8f);
    pool.step(kDt);
    check(kind.drainCollisions() == 1.0f, "six contacts in a frame count as the most a frame counts, no more");
}

void aCollisionDoesNotFeedItself() {
    Rig rig;
    resp::Response r = make(resp::kThing, kCollide);
    r.hold = 20.0f;
    r.params[ThingKind::kChain] = 3.0f;
    resp::Config c = rowsOn({kCollide});
    resp::addResponse(c, r);
    rig.load(c);
    touching(rig.things, 0.5f, 0.5f);
    for (int i = 0; i < 180; ++i) {
        rig.step(Rig::frame());
        const float hit = rig.thingKind.drainCollisions();
        if (hit > 0.0f) rig.bank.hear(Moment::Collide, hit);
    }
    check(rig.things.count() == 3, "the pair's collision makes one Thing on the middle, and what it made touching them makes no more");
    int madeChain = -1;
    for (int i = 2; i < things::kMaxThings; ++i) {
        if (rig.things.alive(i)) madeChain = rig.things.view(i).chain;
    }
    check(madeChain == 0, "a Thing a Collide response makes has its chain depth cut to 0 whatever the response asks");

    Rig plainRig;
    resp::Response k = make(resp::kThing);
    k.params[ThingKind::kChain] = 3.0f;
    plainRig.load(oneOn(k));
    plainRig.kick();
    check(plainRig.things.view(0).chain == 3, "on any other row the chain depth is the response's");
}

}  // namespace

int main() {
    eachKindMakesExactlyOneItem();
    aRowThatIsOffMakesNothing();
    aBurstMakesN();
    frontParamsMapToTheSpawn();
    thingParamsMapToTheSpawn();
    morphAndMeltActOnTheLatestOrTheOldest();
    fieldParamsMapToTheSpawn();
    aPitchAndAPanAreWhereTheyPutThings();
    noPitchHeardIsTheMiddle();
    seededPlacesMatchAcrossTwoRuns();
    followSpawnsAtTheLevelsRateUpToTheCap();
    followKeepsOneFieldAtTheLevel();
    aFreezeHoldsThingsFrontsAndFields();
    aReverseNeverRunsThemBackwards();
    theFlowClocksFollowTheSceneStep();
    theFlowClockTracksReverseAndStutter();
    collideIsTheLastMomentAndTheSoundNeverRaisesIt();
    aCollisionReachesTheBankAsTheMoment();
    collisionsBecomeASongEventThatSpawnsAResponse();
    aFrameOfCollisionsIsCapped();
    aCollisionDoesNotFeedItself();
    if (failures == 0) std::printf("response_kinds_test: ok\n");
    return failures == 0 ? 0 : 1;
}
