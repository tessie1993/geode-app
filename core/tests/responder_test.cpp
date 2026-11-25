// Host checks for the Event Responder bank (core/viz/modulation/Responder, ReactionText's response lines, the
// responder ABI in api/geode_responder_api.cpp): every row starts off, a parameter response fades in, holds and
// fades out through the reaction matrix, every Nth, chance and cooldown, a seeded chance that replays, Beat and Bar
// only with a beat heard, beat-unit envelopes idle at tempo 0 (no tempo is ever assumed), the five new moments,
// the typed hook for later kinds, the compile into the matrix's free slots, the cost budget, and saved text that
// reads back (and old text that still loads). Built and run by CI (native-host-tests.yml); no GL.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "api/geode_responder_api.h"
#include "viz/modulation/ModDocument.hpp"
#include "viz/modulation/ReactionDna.hpp"
#include "viz/modulation/ReactionText.hpp"
#include "viz/modulation/Reactions.hpp"
#include "viz/modulation/Responder.hpp"

using namespace geode::viz;
namespace resp = geode::viz::responder;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// Frame rates that are no tempo and not 120: a display's.
constexpr float kFps = 50.0f;
constexpr float kDt = 1.0f / kFps;
constexpr int kLast = ReactionEngine::kRoutes - 1;

GeodeSoundFrame silent() {
    GeodeSoundFrame f{};
    f.drive = 1.0f;
    return f;
}

// Counts what the bank hands a registered kind.
struct Counter : resp::ResponseKindHandler {
    std::vector<Play> plays;
    std::vector<std::int64_t> frames;
    int released = 0;
    int follows = 0;
    float lastFollow = 0.0f;
    std::int64_t now = 0;
    void fire(const Play& p) override {
        plays.push_back(p);
        frames.push_back(now);
    }
    void follow(int, const resp::Response&, float level) override {
        ++follows;
        lastFollow = level;
    }
    void release(int) override { ++released; }
};

// The matrix, the moments and the bank wired as the resolver wires them.
struct Rig {
    ReactionEngine engine;
    MomentWatch watch;
    resp::Bank bank;
    std::array<ReactRoute, ReactionEngine::kRoutes> hand{};
    Counter counter;
    std::int64_t frame = 0;

    void load(const resp::Config& config) {
        bank.configure(config);
        engine.routes = hand;
        bank.compile(engine.routes);
    }
    void step(const GeodeSoundFrame& f, float dt = kDt) {
        counter.now = frame++;
        engine.stepSignals(f, dt);
        const std::uint32_t moments = watch.step(engine.signals());
        bank.step(f, dt, engine.signals(), moments, engine);
        engine.tickRoutes();
    }
    void restart() {
        engine.reset();
        watch.reset();
        bank.reset();
        frame = 0;
        counter.plays.clear();
        counter.frames.clear();
    }
    float out() const { return engine.output(kLast); }
};

resp::Response param(int row, LfoTarget target, float amount, float fadeIn, float hold, float fadeOut) {
    resp::Response r;
    r.row = row;
    r.target = target;
    r.amount = amount;
    r.fadeIn = fadeIn;
    r.hold = hold;
    r.fadeOut = fadeOut;
    return r;
}

resp::Config oneOn(int row, const resp::Response& r) {
    resp::Config c = resp::newStyleConfig();
    resp::setRowOn(c, row, true);
    resp::addResponse(c, r);
    return c;
}

constexpr int kKick = static_cast<int>(Moment::Kick);

// `seconds` of frames, a kick on the first frame of every `period` seconds (none when 0); `each(i)` after each frame.
template <typename Each>
void run(Rig& rig, float seconds, float period, Each each, float kickStrength = 1.0f) {
    const int frames = static_cast<int>(std::lround(seconds * kFps));
    const int every = period > 0.0f ? static_cast<int>(std::lround(period * kFps)) : 0;
    for (int i = 0; i < frames; ++i) {
        GeodeSoundFrame f = silent();
        if (every > 0 && i % every == 0) f.kick = kickStrength;
        rig.step(f);
        each(i);
    }
}

void runPlain(Rig& rig, float seconds, float period) {
    run(rig, seconds, period, [](int) {});
}

// ── The shape ────────────────────────────────────────────────────────────────────────────────────────────

void everyRowStartsOff() {
    static_assert(kMomentCount == 15 && kPageMoments == 10, "five events appended to the ten of the page");
    static_assert(ReactionEngine::kRoutes >= 200, "a few hundred routes");
    static_assert(StyleDna::kMaxRoutes == 13, "the DNA is dealt for sixteen slots whatever the matrix holds");
    resp::Config c = resp::newStyleConfig();
    bool any = false;
    for (const bool on : c.rowOn) any = any || on;
    check(!any && c.count == 0 && c.muteCount == 0, "a new style has every row off and nothing muted");

    Rig rig;
    resp::addResponse(c, param(kKick, LfoTarget::Zoom, 1.0f, 0.1f, 0.2f, 0.3f));
    rig.load(c);
    check(rig.bank.status().compiled == 0 && !rig.engine.routes[kLast].enabled, "a response on a row that is off compiles to nothing");
    float most = 0.0f;
    run(rig, 2.0f, 0.5f, [&](int) { most = std::fmax(most, std::fabs(rig.out())); });
    check(most == 0.0f, "and the kicks move nothing");
}

void aResponseFadesInHoldsAndFadesOut() {
    Rig rig;
    rig.load(oneOn(kKick, param(kKick, LfoTarget::Zoom, 1.0f, 0.1f, 0.2f, 0.3f)));
    check(rig.bank.status().compiled == 1 && rig.engine.routes[kLast].external && rig.engine.routes[kLast].target == LfoTarget::Zoom,
          "a parameter response compiles into the matrix's last free slot, as an external route");
    std::vector<float> out;
    run(rig, 1.0f, 0.0f, [&](int) { out.push_back(rig.out()); });  // nothing fires
    check(out.back() == 0.0f, "nothing moves before the event");
    rig.restart();
    out.clear();
    run(rig, 1.0f, 2.0f, [&](int) { out.push_back(rig.out()); });  // one kick on the first frame
    check(out[2] > 0.4f && out[2] < 0.8f, "it fades in over its fade in");
    check(out[8] > 0.95f && out[14] > 0.95f, "it holds");
    check(out[22] > 0.05f && out[22] < 0.9f, "it fades out over its fade out");
    check(out[45] < 0.01f && out[49] == 0.0f, "and is gone");
    float last = 2.0f;
    bool falls = true;
    for (size_t i = 16; i < 33; ++i) {
        falls = falls && out[i] <= last + 1e-5f;
        last = out[i];
    }
    check(falls, "the fade out only falls");
}

void theStrengthCurveShapesTheEvent() {
    Rig linear;
    Rig expo;
    resp::Response r = param(kKick, LfoTarget::Zoom, 1.0f, 0.05f, 0.4f, 0.2f);
    linear.load(oneOn(kKick, r));
    r.strength = ModCurve::Exponential;
    expo.load(oneOn(kKick, r));
    float a = 0.0f;
    float b = 0.0f;
    run(linear, 0.5f, 2.0f, [&](int) { a = std::fmax(a, linear.out()); }, 0.2f);
    run(expo, 0.5f, 2.0f, [&](int) { b = std::fmax(b, expo.out()); }, 0.2f);
    check(a > 0.4f && a < 0.8f, "a soft hit plays softer than a hard one");
    check(b < a - 0.05f, "the strength curve reshapes it");
}

void whileHoldsAsLongAsTheEventAndFollowTracksIt() {
    {
        Rig rig;
        resp::Response r = param(kKick, LfoTarget::Zoom, 1.0f, 0.05f, 0.0f, 0.3f);
        r.drive = resp::DriveMode::While;
        rig.load(oneOn(kKick, r));
        std::vector<float> out;
        for (int i = 0; i < 100; ++i) {
            GeodeSoundFrame f = silent();
            if (i == 0) f.kick = 1.0f;
            f.kickEnv = i < 40 ? 1.0f : 0.0f;
            rig.step(f);
            out.push_back(rig.out());
        }
        check(out[10] > 0.95f && out[38] > 0.95f, "a while response stays on as long as its event holds");
        check(out[60] < 0.7f && out[99] < 0.01f, "and lets go when it ends");
    }
    {
        Rig rig;
        resp::Response r = param(kKick, LfoTarget::Zoom, 1.0f, 0.05f, 0.0f, 0.1f);
        r.drive = resp::DriveMode::Follow;
        r.everyNth = 4;
        r.chance = 0.0f;
        rig.load(oneOn(kKick, r));
        float seen = 0.0f;
        for (int i = 0; i < 50; ++i) {
            GeodeSoundFrame f = silent();
            f.kickEnv = 0.6f;
            rig.step(f);
            seen = rig.out();
        }
        check(near(seen, 0.6f, 0.03f), "a follow response tracks the event's level, whatever its Nth and chance say");
    }
}

// ── The gates ────────────────────────────────────────────────────────────────────────────────────────────

resp::Config thingOn(int row, int nth, float chance, float cooldown) {
    resp::Response r;
    r.row = row;
    r.kind = resp::kThing;
    r.everyNth = nth;
    r.chance = chance;
    r.cooldown = cooldown;
    return oneOn(row, r);
}

void everyNthChanceAndCooldown() {
    {
        Rig rig;
        rig.bank.registerKind(resp::kThing, &rig.counter);
        rig.load(thingOn(kKick, 3, 1.0f, 0.0f));
        runPlain(rig, 3.0f, 0.5f);  // six kicks
        check(rig.counter.plays.size() == 2, "every 3rd plays on the 1st hit and the 4th");
    }
    {
        Rig rig;
        rig.bank.registerKind(resp::kThing, &rig.counter);
        rig.load(thingOn(kKick, 1, 1.0f, 0.8f));
        runPlain(rig, 2.2f, 0.5f);  // kicks at 0, 0.5, 1.0, 1.5, 2.0 seconds
        check(rig.counter.plays.size() == 3, "a cooldown in seconds passes over the hits inside it");
    }
    {
        Rig all;
        Rig none;
        all.bank.registerKind(resp::kThing, &all.counter);
        none.bank.registerKind(resp::kThing, &none.counter);
        all.load(thingOn(kKick, 1, 1.0f, 0.0f));
        none.load(thingOn(kKick, 1, 0.0f, 0.0f));
        runPlain(all, 30.0f, 0.5f);
        runPlain(none, 30.0f, 0.5f);
        check(all.counter.plays.size() == 60, "chance 1 plays every hit");
        check(none.counter.plays.empty(), "chance 0 plays none");
    }
}

void chanceReplaysTheSameSequence() {
    Rig a;
    Rig b;
    a.bank.registerKind(resp::kThing, &a.counter);
    b.bank.registerKind(resp::kThing, &b.counter);
    a.load(thingOn(kKick, 1, 0.5f, 0.0f));
    b.load(thingOn(kKick, 1, 0.5f, 0.0f));
    runPlain(a, 30.0f, 0.5f);
    runPlain(b, 30.0f, 0.5f);
    check(a.counter.plays.size() > 10 && a.counter.plays.size() < 50, "a half chance plays some hits and not others");
    check(a.counter.frames == b.counter.frames, "two runs of the same frames hear the same hits");
    const std::vector<std::int64_t> first = a.counter.frames;
    a.restart();
    runPlain(a, 30.0f, 0.5f);
    check(a.counter.frames == first, "and a restart replays them (live and export agree)");
    Rig tenth;
    tenth.bank.registerKind(resp::kThing, &tenth.counter);
    tenth.load(thingOn(kKick, 1, 0.1f, 0.0f));
    runPlain(tenth, 30.0f, 0.5f);
    check(tenth.counter.plays.size() < a.counter.plays.size(), "a lower chance plays fewer");
}

// ── Tempo ────────────────────────────────────────────────────────────────────────────────────────────────

void beatAndBarComeOnlyWithABeatHeard() {
    for (const int row : {static_cast<int>(Moment::Beat), static_cast<int>(Moment::Bar)}) {
        const bool beat = row == static_cast<int>(Moment::Beat);
        Rig deaf;
        Rig heard;
        deaf.bank.registerKind(resp::kThing, &deaf.counter);
        heard.bank.registerKind(resp::kThing, &heard.counter);
        deaf.load(thingOn(row, 1, 1.0f, 0.0f));
        heard.load(thingOn(row, 1, 1.0f, 0.0f));
        GeodeSoundFrame f = silent();
        (beat ? f.beat : f.downbeat) = 1.0f;
        f.bpm = 0.0f;
        deaf.step(f);
        check(deaf.counter.plays.empty() && deaf.bank.status().lit[static_cast<size_t>(row)] == 0.0f,
              "with no tempo heard the event does not come");
        f.bpm = 100.0f;
        heard.step(f);
        check(heard.counter.plays.size() == 1 && heard.bank.status().lit[static_cast<size_t>(row)] == 1.0f,
              "with a beat heard it does, and its light comes on");
    }
}

void beatUnitEnvelopesIdleAtTempoZero() {
    resp::Response r = param(kKick, LfoTarget::Zoom, 1.0f, 1.0f, 1.0f, 1.0f);
    r.unit = resp::TimeUnit::Beats;
    Rig idle;
    idle.load(oneOn(kKick, r));
    float most = 0.0f;
    for (int i = 0; i < 100; ++i) {
        GeodeSoundFrame f = silent();
        if (i % 25 == 0) f.kick = 1.0f;
        f.bpm = 0.0f;
        idle.step(f);
        most = std::fmax(most, std::fabs(idle.out()));
    }
    check(most == 0.0f, "an envelope in beats stays idle while no tempo is heard");

    Rig playing;
    playing.load(oneOn(kKick, r));
    float at = 0.0f;
    for (int i = 0; i < 15; ++i) {  // 0.3 s, half of a 0.6 s beat at 100 bpm
        GeodeSoundFrame f = silent();
        if (i == 0) f.kick = 1.0f;
        f.bpm = 100.0f;
        playing.step(f);
        at = playing.out();
    }
    check(at > 0.35f && at < 0.65f, "with a tempo heard a beat is its length: half a fade in after half a beat");

    Counter c;
    Rig hook;
    hook.bank.registerKind(resp::kThing, &c);
    resp::Response t;
    t.row = kKick;
    t.kind = resp::kThing;
    t.unit = resp::TimeUnit::Beats;
    t.fadeIn = 1.0f;
    t.hold = 2.0f;
    t.fadeOut = 0.5f;
    hook.load(oneOn(kKick, t));
    GeodeSoundFrame f = silent();
    f.kick = 1.0f;
    f.bpm = 100.0f;
    hook.step(f);
    check(c.plays.size() == 1 && near(c.plays[0].fadeInSeconds, 0.6f, 1e-4f) && near(c.plays[0].holdSeconds, 1.2f, 1e-4f) &&
              near(c.plays[0].fadeOutSeconds, 0.3f, 1e-4f),
          "a handler is given the envelope in seconds");
}

// ── The events ───────────────────────────────────────────────────────────────────────────────────────────

void theNewMoments() {
    const auto came = [](GeodeSoundFrame f) {
        ReactSignals s;
        s.step(f, kDt);
        MomentWatch w;
        return w.step(s);
    };
    const auto bit = [](Moment m) { return 1u << static_cast<int>(m); };
    GeodeSoundFrame f = silent();
    f.onset = 1.0f;
    check(came(f) == bit(Moment::Onset), "an onset is a moment");
    f = silent();
    f.beat = 1.0f;
    check(came(f) == bit(Moment::Beat), "a beat is a moment");
    f = silent();
    f.downbeat = 1.0f;
    check(came(f) == bit(Moment::Bar), "a downbeat is a bar");
    f = silent();
    f.surge = 1.0f;
    check(came(f) == bit(Moment::LoudnessPeak), "a surge is a loudness peak");
    check(hand::momentOf(ReactSource::Surge) == static_cast<int>(Moment::Drop) && hand::kAll == 0x400003FFu &&
              hand::trigger(Moment::Kick) == ReactSource::Kick,
          "the page's moments, sources and hand mask are as they were");
}

void aKeyChangeIsAKeyHeldForAMoment() {
    ReactSignals s;
    MomentWatch w;
    int came = 0;
    const auto feed = [&](float hue, float clarity, float seconds) {
        const int frames = static_cast<int>(std::lround(seconds * kFps));
        for (int i = 0; i < frames; ++i) {
            GeodeSoundFrame f = silent();
            f.keyHue = hue;
            f.keyClarity = clarity;
            s.step(f, kDt);
            if ((w.step(s) & (1u << static_cast<int>(Moment::KeyChange))) != 0u) ++came;
        }
    };
    feed(0.0f, 0.8f, 2.0f);
    check(came == 0 && s.keyChange() == 0.0f, "the first key heard is no change");
    feed(1.0f / 12.0f, 0.8f, 0.5f);
    feed(0.0f, 0.8f, 1.0f);
    check(came == 0, "a flicker to another key and back is none");
    feed(0.0f, 0.0f, 1.0f);
    feed(1.0f / 12.0f, 0.0f, 3.0f);
    check(came == 0, "a silent frame moves nothing");
    feed(1.0f / 12.0f, 0.8f, 1.5f);
    check(came == 1, "another key, held for a moment, is a key change");
    check(s.keyChange() > 0.2f, "which is held for a moment");
    feed(1.0f / 12.0f, 0.8f, 6.0f);
    check(came == 1 && s.keyChange() < 0.05f, "once, and fades");
    feed(0.0f, 0.8f, 2.0f);
    check(came == 2, "and the key back again is another");
}

// ── The hook, the matrix, the budget ─────────────────────────────────────────────────────────────────────

void laterKindsRegisterWithoutTouchingTheBank() {
    resp::Bank bank;
    Counter c;
    check(!bank.registerKind(resp::kParam, &c) && !bank.registerKind(resp::kKindCount, &c) && !bank.registerKind(-1, &c),
          "the bank's own kind and kinds outside the range are not for sale");
    check(bank.registerKind(resp::kThing, &c) && bank.registerKind(resp::kFront, &c) && bank.registerKind(resp::kField, &c) &&
              bank.registerKind(resp::kClock, &c) && bank.registerKind(resp::kThing, nullptr),
          "Things, Fronts, Fields and Clock control register, and a handler can be removed");
    Rig rig;
    resp::Response r;
    r.row = kKick;
    r.kind = resp::kField;
    rig.load(oneOn(kKick, r));
    runPlain(rig, 1.0f, 0.5f);
    check(rig.bank.status().compiled == 0, "a kind with no system behind it plays nothing and takes no route");
    Counter whileCounter;
    Rig held;
    held.bank.registerKind(resp::kThing, &whileCounter);
    resp::Response w;
    w.row = kKick;
    w.kind = resp::kThing;
    w.drive = resp::DriveMode::While;
    held.load(oneOn(kKick, w));
    for (int i = 0; i < 60; ++i) {
        GeodeSoundFrame f = silent();
        if (i == 0) f.kick = 1.0f;
        f.kickEnv = i < 20 ? 1.0f : 0.0f;
        held.step(f);
    }
    check(whileCounter.plays.size() == 1 && whileCounter.released == 1, "a while response is released when its event ends");
    Counter followCounter;
    Rig following;
    following.bank.registerKind(resp::kThing, &followCounter);
    resp::Response fo;
    fo.row = kKick;
    fo.kind = resp::kThing;
    fo.drive = resp::DriveMode::Follow;
    following.load(oneOn(kKick, fo));
    for (int i = 0; i < 10; ++i) {
        GeodeSoundFrame f = silent();
        f.kickEnv = 0.5f;
        following.step(f);
    }
    check(followCounter.follows == 10 && near(followCounter.lastFollow, 0.5f, 1e-4f), "a follow response is given the level every frame");
}

void compileFillsTheFreeSlotsFromTheTop() {
    Rig rig;
    for (int i = 0; i < 3; ++i) {
        rig.hand[static_cast<size_t>(i)].enabled = true;
        rig.hand[static_cast<size_t>(i)].source = ReactSource::Mid;  // no row claims the mids
        rig.hand[static_cast<size_t>(i)].target = LfoTarget::Bloom;
    }
    resp::Config c = resp::newStyleConfig();
    resp::setRowOn(c, kKick, true);
    resp::addResponse(c, param(kKick, LfoTarget::Zoom, 0.5f, 0.1f, 0.1f, 0.1f));
    resp::addResponse(c, param(kKick, LfoTarget::Warp, -0.5f, 0.1f, 0.1f, 0.1f));
    resp::Response off = param(kKick, LfoTarget::Twist, 0.5f, 0.1f, 0.1f, 0.1f);
    off.enabled = false;
    resp::addResponse(c, off);
    resp::addResponse(c, param(kKick, LfoTarget::None, 0.5f, 0.1f, 0.1f, 0.1f));
    resp::addResponse(c, param(static_cast<int>(Moment::Snare), LfoTarget::Ripple, 0.5f, 0.1f, 0.1f, 0.1f));
    rig.load(c);
    check(rig.bank.status().compiled == 2 && rig.engine.routes[kLast].target == LfoTarget::Zoom &&
              rig.engine.routes[kLast - 1].target == LfoTarget::Warp && rig.engine.routes[kLast - 1].amount == -0.5f,
          "the rows that are on, the responses that are on and have a target take the slots in order");
    check(rig.engine.routes[0].target == LfoTarget::Bloom && rig.engine.routes[2].enabled && !rig.engine.routes[3].enabled,
          "and the routes the style dealt are untouched");
    check(rig.bank.status().enabledRoutes == 5, "the matrix counts what plays");

    Rig full;
    for (ReactRoute& r : full.hand) {
        r.enabled = true;
        r.source = ReactSource::Mid;
        r.target = LfoTarget::Bloom;
    }
    full.load(c);
    check(full.bank.status().compiled == 0 && full.bank.status().dropped == 2, "a full matrix drops responses and says so");
}

std::string edit(const char* text, int op, int row, int index, int field, float value);
resp::Config read(const std::string& text);

ReactRoute styleRoute(ReactSource source, LfoTarget target, bool style = true) {
    ReactRoute r;
    r.enabled = true;
    r.source = source;
    r.target = target;
    r.styleDefault = style;
    return r;
}

constexpr int kSrcKick = static_cast<int>(ReactSource::Kick);
constexpr int kTgtZoom = static_cast<int>(LfoTarget::Zoom);

void aRowThatIsOffStillPlaysTheStyle() {
    Rig rig;
    rig.hand[0] = styleRoute(ReactSource::Kick, LfoTarget::Zoom);
    rig.hand[1] = styleRoute(ReactSource::Downbeat, LfoTarget::Warp);
    rig.hand[2] = styleRoute(ReactSource::Surge, LfoTarget::Twist);
    rig.load(resp::newStyleConfig());
    check(rig.engine.routes[0].enabled && rig.engine.routes[1].enabled && rig.engine.routes[2].enabled,
          "with every row off the style's own routes still play");
    float seen = 0.0f;
    run(rig, 0.5f, 2.0f, [&](int) { seen = std::fmax(seen, rig.engine.output(0)); });
    check(seen > 0.0f, "and move the picture on their event");
}

void aMutedChipSilencesOnlyThatRoute() {
    Rig rig;
    rig.hand[0] = styleRoute(ReactSource::Kick, LfoTarget::Zoom);
    rig.hand[1] = styleRoute(ReactSource::Kick, LfoTarget::Warp);
    rig.hand[2] = styleRoute(ReactSource::Snare, LfoTarget::Zoom);
    resp::Config c = resp::newStyleConfig();
    check(resp::setMuted(c, kSrcKick, kTgtZoom, true) && c.muteCount == 1, "a style route is muted by its source and target");
    check(resp::setMuted(c, kSrcKick, kTgtZoom, true) && c.muteCount == 1, "twice is once");
    check(!resp::setMuted(c, -1, kTgtZoom, true) && !resp::setMuted(c, kSrcKick, 0, true) && c.muteCount == 1, "a pair that is no route is refused");
    rig.load(c);
    check(!rig.engine.routes[0].enabled && rig.engine.routes[1].enabled && rig.engine.routes[2].enabled,
          "only the muted route is silent: the same target on another source and the same source on another target play");
    float zoom = 0.0f;
    float warp = 0.0f;
    run(rig, 0.5f, 2.0f, [&](int) {
        zoom = std::fmax(zoom, rig.engine.output(0));
        warp = std::fmax(warp, rig.engine.output(1));
    });
    check(zoom == 0.0f && warp > 0.0f, "and it sounds so");
    resp::setMuted(c, kSrcKick, kTgtZoom, false);
    rig.load(c);
    check(c.muteCount == 0 && rig.engine.routes[0].enabled, "unmuting brings it back");
}

void aHandRouteAlwaysPlays() {
    Rig rig;
    rig.hand[0] = styleRoute(ReactSource::Kick, LfoTarget::Zoom, false);
    resp::Config c = resp::newStyleConfig();
    resp::setMuted(c, kSrcKick, kTgtZoom, true);
    rig.load(c);
    check(rig.engine.routes[0].enabled, "a route the user added is never held back, even by a mute of the same pair");

    StyleDna dna;
    dna.routes[0] = styleRoute(ReactSource::Kick, LfoTarget::Warp, false);
    dna.count = 1;
    HandRoutes user;
    user.user = true;
    user.count = 1;
    user.routes[0] = styleRoute(ReactSource::Snare, LfoTarget::Zoom, false);
    user.mask = hand::bit(Moment::Snare);
    RouteArray merged{};
    const int n = mergeHand(user, dna, merged);
    check(n == 2 && !merged[0].styleDefault && merged[1].styleDefault, "the merge marks the style's routes and not the user's");
}

void aSavedLookWithNoResponderTextIsUntouched() {
    const std::string legacy =
        "reactions/2\n"
        "true|KICK|COLOR_SHIFT|JUMP|0.35|LINEAR|0.08|0.35\n"
        "true|SNARE|WARP|FOLLOW|0.6|SMOOTH|0.03|0.45";
    ModDocument doc;
    doc.readReactions(legacy, GEODE_HAND_ALL);
    check(doc.reactionsText() == legacy, "a saved look with no responder text writes back byte for byte");
    resp::Config c;
    reactiontext::decodeResponses(legacy, c);
    Rig rig;
    rig.hand[0] = styleRoute(ReactSource::Kick, LfoTarget::Zoom);
    rig.hand[1] = styleRoute(ReactSource::Snare, LfoTarget::Warp);
    rig.hand[2] = styleRoute(ReactSource::Mid, LfoTarget::Bloom, false);
    rig.load(c);
    bool same = true;
    for (size_t i = 0; i < rig.hand.size(); ++i) {
        const ReactRoute& a = rig.hand[i];
        const ReactRoute& b = rig.engine.routes[i];
        same = same && a.enabled == b.enabled && a.source == b.source && a.target == b.target && a.amount == b.amount && a.external == b.external;
    }
    check(same, "and the matrix it plays is the matrix it was dealt");
    std::vector<char> text(8);
    check(geode_resp_default_text(text.data(), static_cast<int>(text.size())) == 0, "a new style has no responder text");
}

void chipMutesSurviveSaveAndLoad() {
    const std::string legacy = "reactions/2\ntrue|KICK|COLOR_SHIFT|JUMP|0.35|LINEAR|0.08|0.35";
    std::string t = edit(legacy.c_str(), GEODE_RESP_EDIT_STYLE_MUTE, kSrcKick, kTgtZoom, 0, 1.0f);
    t = edit(t.c_str(), GEODE_RESP_EDIT_STYLE_MUTE, static_cast<int>(ReactSource::Snare), static_cast<int>(LfoTarget::Warp), 0, 1.0f);
    resp::Config c = read(t);
    check(c.muteCount == 2 && resp::isMuted(c, kSrcKick, kTgtZoom), "a mute is saved in the reactions text, keyed by source and target");
    ModDocument doc;
    doc.readReactions(t, GEODE_HAND_ALL);
    check(doc.hand.user && doc.hand.count == 1 && doc.reactionsText() == t, "beside the user's routes, which it leaves as they were");
    const std::string again = edit(t.c_str(), GEODE_RESP_EDIT_ROW_ON, 2, 0, 0, 1.0f);
    check(read(again).muteCount == 2 && read(again).rowOn[2], "other edits keep it");
    const std::string off = edit(again.c_str(), GEODE_RESP_EDIT_STYLE_MUTE, kSrcKick, kTgtZoom, 0, 0.0f);
    check(read(off).muteCount == 1 && !resp::isMuted(read(off), kSrcKick, kTgtZoom), "and unmuting takes it out");
    const std::string cleared = edit(again.c_str(), GEODE_RESP_EDIT_CLEAR, 0, 0, 0, 0.0f);
    check(read(cleared).muteCount == 0, "clear puts every style route back on");
}

void aPreviewPlaysOnceNow() {
    Rig rig;
    rig.load(resp::newStyleConfig());
    resp::Response r = param(kKick, LfoTarget::Zoom, 1.0f, 0.1f, 0.2f, 0.3f);
    r.drive = resp::DriveMode::Follow;
    rig.bank.preview(r);
    rig.engine.routes = rig.hand;
    rig.bank.compile(rig.engine.routes);
    check(rig.engine.routes[kLast].external, "a preview takes a route");
    float peak = 0.0f;
    float end = 1.0f;
    for (int i = 0; i < 60; ++i) {
        rig.step(silent());
        peak = std::fmax(peak, rig.out());
        end = rig.out();
    }
    check(peak > 0.95f && end == 0.0f, "it plays its envelope once, whatever the row's switch says, and ends");
    rig.bank.preview(r);
    rig.engine.routes = rig.hand;
    rig.bank.compile(rig.engine.routes);
    rig.step(silent());
    check(rig.out() > 0.1f, "and again when asked");
}

// The status after one frame with `n` follow responses of another kind playing, on a matrix with every slot used.
resp::Status statusWith(int n) {
    Rig rig;
    for (ReactRoute& r : rig.hand) {
        r.enabled = true;
        r.source = ReactSource::Mid;
        r.target = LfoTarget::Bloom;
    }
    resp::Config c = resp::newStyleConfig();
    int made = 0;
    for (int row = 0; row < 6 && made < n; ++row) {
        resp::setRowOn(c, row, true);
        for (int i = 0; i < resp::kMaxPerRow && made < n; ++i) {
            resp::Response r;
            r.row = row;
            r.kind = resp::kThing;
            r.drive = resp::DriveMode::Follow;
            if (resp::addResponse(c, r) >= 0) ++made;
        }
    }
    rig.bank.registerKind(resp::kThing, &rig.counter);
    rig.load(c);
    rig.step(silent());
    return rig.bank.status();
}

void theCostBudgetIsReadable() {
    const resp::Status light = statusWith(10);
    check(light.budget == resp::kCostBudget && light.enabledRoutes == ReactionEngine::kRoutes && light.active == 10 &&
              light.cost == light.enabledRoutes + 2 * light.active,
          "the page reads the cost and the budget");
    check(light.level == 0, "a light style is not flagged");
    check(statusWith(70).level == 1, "a heavy one is");
    const resp::Status over = statusWith(resp::kMaxResponses);
    check(over.level == 2 && over.cost >= resp::kCostBudget, "one over the budget says so, and still plays");
}

// ── What is saved ────────────────────────────────────────────────────────────────────────────────────────

void savedTextReadsBack() {
    resp::Config c = resp::newStyleConfig();
    resp::setRowOn(c, kKick, true);
    resp::setRowOn(c, static_cast<int>(Moment::Beat), true);
    resp::Response a = param(kKick, LfoTarget::Zoom, 0.75f, 0.1f, 0.25f, 0.5f);
    a.strength = ModCurve::Smooth;
    a.unit = resp::TimeUnit::Beats;
    a.drive = resp::DriveMode::Long;
    a.everyNth = 4;
    a.chance = 0.5f;
    a.cooldown = 1.5f;
    a.enabled = false;
    resp::addResponse(c, a);
    resp::Response b;
    b.row = static_cast<int>(Moment::KeyChange);
    b.kind = resp::kFront;
    b.params[2] = 3.5f;
    resp::addResponse(c, b);
    const std::string lines = reactiontext::encodeResponses(c);
    resp::Config back;
    reactiontext::decodeResponses("reactions/2\n" + lines, back);
    check(back.rowOn[kKick] && back.rowOn[static_cast<size_t>(Moment::Beat)] && !back.rowOn[1] && back.count == 2,
          "rows and responses read back");
    const resp::Response& x = back.responses[0];
    check(x.row == kKick && !x.enabled && x.target == LfoTarget::Zoom && x.amount == 0.75f && x.strength == ModCurve::Smooth &&
              x.fadeIn == 0.1f && x.hold == 0.25f && x.fadeOut == 0.5f && x.unit == resp::TimeUnit::Beats &&
              x.drive == resp::DriveMode::Long && x.everyNth == 4 && x.chance == 0.5f && x.cooldown == 1.5f,
          "a response reads back field for field");
    check(back.responses[1].kind == resp::kFront && back.responses[1].params[2] == 3.5f, "a later kind's own parameters read back");
    check(reactiontext::encodeResponses(back) == lines, "and write the same text");
    check(reactiontext::encodeResponses(resp::Config{}).empty(), "no rows and no responses is no lines");
}

void oldTextStillLoads() {
    const std::string legacy =
        "reactions/2\n"
        "true|KICK|COLOR_SHIFT|JUMP|0.35|LINEAR|0.08|0.35\n"
        "false|KICK_4|ROTATION|STEP|-0.2|LINEAR|0.12|0.12";
    resp::Config c;
    reactiontext::decodeResponses(legacy, c);
    check(c.count == 0 && c.muteCount == 0 && !c.rowOn[0], "a list saved before responses has none, and mutes none of the style's routes");
    ModDocument doc;
    doc.readReactions(legacy, GEODE_HAND_ALL);
    check(doc.hand.user && doc.hand.count == 2 && doc.responses.empty() && doc.reactionsText() == legacy, "and its routes read and write as they did");

    const std::string both = legacy + "\ngate|true\nrow|KICK|true\nresponse|KICK|true|0|ZOOM|0.5|LINEAR|0.1|0.2|0.3|SECONDS|ONCE|1|1|0|0|0|0|0|0|0";
    float routes[ReactionEngine::kRoutes * ReactionEngine::kConfigFloats] = {};
    check(reactiontext::decode(both, routes) == 2, "a build that knows no responses drops their lines and keeps the routes");
    ModDocument kept;
    kept.readReactions(both, GEODE_HAND_ALL);
    check(kept.hand.user && kept.hand.count == 2 && !kept.responses.empty(), "this build reads both");
    ModDocument again;
    again.readReactions(kept.reactionsText(), GEODE_HAND_ALL);
    check(kept.reactionsText() == again.reactionsText() && again.hand.count == 2, "and a route edit's text keeps the responses");

    ModDocument none;
    none.readReactions("reactions/2\nroutes|auto\ngate|true\nrow|KICK|true", GEODE_HAND_ALL);
    check(!none.hand.user && none.responses == "row|KICK|true" && none.reactionsText() == "reactions/2\nroutes|auto\nrow|KICK|true",
          "a list of rows alone leaves the style's own routes playing");
    const ModDocument blank;
    check(blank.reactionsText().empty() && reactiontext::isList("reactions/2") && !reactiontext::isList("nonsense"),
          "no routes and no responses is Auto");

    resp::Config bad;
    reactiontext::decodeResponses(
        "reactions/2\n"
        "response|NOPE|true|0|ZOOM|0.5|LINEAR|0.1|0.2|0.3|SECONDS|ONCE|1|1|0|0|0|0|0|0|0\n"
        "response|KICK|true|0|NOT_A_TARGET|0.5|LINEAR|0.1|0.2|0.3|SECONDS|ONCE|1|1|0|0|0|0|0|0|0\n"
        "response|KICK|true|0|ZOOM|99|WOBBLY|x|-4|900|HOURS|SOMETIMES|0|7|-3|0|0|0|0|0|0\n"
        "response|KICK|true|short",
        bad);
    check(bad.count == 1 && bad.responses[0].amount == 2.0f && bad.responses[0].strength == ModCurve::Linear &&
              bad.responses[0].fadeIn == resp::Response{}.fadeIn && bad.responses[0].hold == 0.0f && bad.responses[0].fadeOut == 30.0f &&
              bad.responses[0].unit == resp::TimeUnit::Seconds && bad.responses[0].drive == resp::DriveMode::Once &&
              bad.responses[0].everyNth == 1 && bad.responses[0].chance == 1.0f && bad.responses[0].cooldown == 0.0f,
          "a line that names no row or target is dropped, the rest forgive: defaults and clamps");
}

std::string edit(const char* text, int op, int row, int index, int field, float value) {
    std::vector<char> out(16384);
    const int need = geode_resp_edit(text, op, row, index, field, value, out.data(), static_cast<int>(out.size()));
    if (need >= static_cast<int>(out.size())) return "too long";
    return std::string(out.data());
}

resp::Config read(const std::string& text) {
    resp::Config c;
    reactiontext::decodeResponses(text, c);
    return c;
}

void theNativeInterfaceEditsTheText() {
    std::vector<char> text(256);
    geode_resp_default_text(text.data(), static_cast<int>(text.size()));
    const resp::Config fresh = read(text.data());
    bool any = false;
    for (const bool on : fresh.rowOn) any = any || on;
    check(!any && fresh.count == 0 && fresh.muteCount == 0 && text[0] == '\0', "a new style's text is no text: every row off, the style plays");
    check(std::string(geode_resp_row_name(0)) == "KICK" && std::string(geode_resp_row_name(GEODE_RESP_ROWS - 1)) == "KEY_CHANGE" &&
              geode_resp_row_name(GEODE_RESP_ROWS) == nullptr && geode_resp_row_name(-1) == nullptr,
          "rows are named");

    const int zoom = static_cast<int>(LfoTarget::Zoom);
    std::string t = edit(nullptr, GEODE_RESP_EDIT_ROW_ON, 0, 0, 0, 1.0f);
    check(read(t).rowOn[0], "a row is switched on");
    t = edit(t.c_str(), GEODE_RESP_EDIT_ADD, 0, 0, 0, static_cast<float>(zoom));
    t = edit(t.c_str(), GEODE_RESP_EDIT_ADD, 0, 0, 0, static_cast<float>(static_cast<int>(LfoTarget::Warp)));
    check(read(t).count == 2 && read(t).responses[0].target == LfoTarget::Zoom && read(t).responses[1].target == LfoTarget::Warp,
          "responses are added in order");
    t = edit(t.c_str(), GEODE_RESP_EDIT_SET, 0, 1, GEODE_RESP_DRIVE, 2.0f);
    t = edit(t.c_str(), GEODE_RESP_EDIT_SET, 0, 1, GEODE_RESP_FADE_OUT, 99.0f);
    t = edit(t.c_str(), GEODE_RESP_EDIT_SET, 0, 0, GEODE_RESP_EVERY_NTH, 4.0f);
    const resp::Config set = read(t);
    check(set.responses[1].drive == resp::DriveMode::Follow && set.responses[1].fadeOut == 30.0f && set.responses[0].everyNth == 4,
          "a field is set and lands inside its range");
    const std::string before = t;
    t = edit(t.c_str(), 99, 0, 0, 0, 0.0f);
    t = edit(t.c_str(), GEODE_RESP_EDIT_SET, 99, 0, GEODE_RESP_DRIVE, 1.0f);
    t = edit(t.c_str(), GEODE_RESP_EDIT_SET, 0, 7, GEODE_RESP_DRIVE, 1.0f);
    t = edit(t.c_str(), GEODE_RESP_EDIT_SET, 0, 0, 99, 1.0f);
    t = edit(t.c_str(), GEODE_RESP_EDIT_SET, 0, 0, GEODE_RESP_DRIVE, 17.0f);
    t = edit(t.c_str(), GEODE_RESP_EDIT_ADD, 0, 0, 0, 0.0f);
    check(t == before, "an edit that names nothing hands back what came in");
    t = edit(t.c_str(), GEODE_RESP_EDIT_REMOVE, 0, 0, 0, 0.0f);
    check(read(t).count == 1 && read(t).responses[0].target == LfoTarget::Warp, "a response is removed and the others keep their order");
    t = edit(t.c_str(), GEODE_RESP_EDIT_SET, 0, 0, GEODE_RESP_ROW, 3.0f);
    check(read(t).responses[0].row == 3 && read(t).rowOn[0], "a response moves to another row");
    t = edit(t.c_str(), GEODE_RESP_EDIT_CLEAR, 0, 0, 0, 0.0f);
    const resp::Config cleared = read(t);
    check(cleared.count == 0 && !cleared.rowOn[0], "clear is a new style again");

    ModDocument doc;
    doc.readReactions(
        "reactions/2\ntrue|KICK|COLOR_SHIFT|JUMP|0.35|LINEAR|0.08|0.35",
        GEODE_HAND_ALL);
    const std::string routed = edit(doc.reactionsText().c_str(), GEODE_RESP_EDIT_ROW_ON, 1, 0, 0, 1.0f);
    ModDocument after;
    after.readReactions(routed, GEODE_HAND_ALL);
    check(after.hand.user && after.hand.count == 1 && read(routed).rowOn[1], "editing a row keeps the user's routes");
}

// ── What the review asked for ────────────────────────────────────────────────────────────────────────────

void aRuntimeStaysOnlyWithTheSameResponse() {
    resp::Response a;
    a.row = kKick;
    a.kind = resp::kThing;
    a.everyNth = 2;
    a.drive = resp::DriveMode::Once;
    resp::Response b = a;
    b.drive = resp::DriveMode::Long;
    resp::Config both = resp::newStyleConfig();
    resp::setRowOn(both, kKick, true);
    resp::addResponse(both, a);
    resp::addResponse(both, b);
    Rig rig;
    rig.bank.registerKind(resp::kThing, &rig.counter);
    rig.load(both);
    runPlain(rig, 0.5f, 0.5f);  // the 1st hit: both play, and each has counted one
    check(rig.counter.plays.size() == 2, "both play the first hit");
    resp::Config onlyB = resp::newStyleConfig();
    resp::setRowOn(onlyB, kKick, true);
    resp::addResponse(onlyB, b);
    rig.load(onlyB);
    runPlain(rig, 0.5f, 0.5f);
    check(rig.counter.plays.size() == 3, "a removal does not hand the first response's count to the one that moved up");
    resp::Config louder = onlyB;
    louder.responses[0].amount = 1.0f;
    rig.load(louder);
    runPlain(rig, 0.5f, 0.5f);
    check(rig.counter.plays.size() == 3, "an edit of the same response keeps its count (the 2nd hit is passed over)");
}

void theSeededSequenceRestartsWithThePlay() {
    Rig rig;
    rig.bank.registerKind(resp::kThing, &rig.counter);
    rig.load(thingOn(kKick, 1, 0.5f, 0.0f));
    runPlain(rig, 30.0f, 0.5f);
    const std::vector<std::int64_t> first = rig.counter.frames;
    rig.bank.reset();  // the song starts over: nothing else is reset
    rig.counter.frames.clear();
    rig.frame = 0;
    runPlain(rig, 30.0f, 0.5f);
    check(!first.empty() && rig.counter.frames == first, "the same song from a restart gives the same fires");
}

void anOlderReaderTakesAutoWithResponsesForNoRoutes() {
    float routes[ReactionEngine::kRoutes * ReactionEngine::kConfigFloats] = {};
    check(reactiontext::decode("reactions/2\nroutes|auto\ngate|true\nrow|KICK|true", routes) == 0,
          "a build that knows no responses reads Auto with responses as a list of no routes (documented, not migrated)");
}

void aPreviewGivesItsRouteBack() {
    Rig rig;
    rig.load(resp::newStyleConfig());
    rig.bank.preview(param(kKick, LfoTarget::Zoom, 1.0f, 0.05f, 0.05f, 0.1f));
    rig.engine.routes = rig.hand;
    rig.bank.compile(rig.engine.routes);
    for (int i = 0; i < 40; ++i) rig.step(silent());
    check(!rig.engine.routes[kLast].enabled, "a preview that has played out frees its route");
}

void aPreviewPlaysOnTheWallClock() {
    Rig rig;
    rig.load(resp::newStyleConfig());
    rig.bank.preview(param(kKick, LfoTarget::Zoom, 1.0f, 0.1f, 0.1f, 0.1f));
    rig.engine.routes = rig.hand;
    rig.bank.compile(rig.engine.routes);
    for (int i = 0; i < 5; ++i) {
        const GeodeSoundFrame f = silent();
        rig.engine.stepSignals(f, 0.0f);  // the frame clock has stopped (silence)
        rig.bank.step(f, 0.0f, rig.engine.signals(), rig.watch.step(rig.engine.signals()), rig.engine, kDt);
        rig.engine.tickRoutes();
    }
    check(rig.out() > 0.5f, "a preview tapped in silence plays on the wall step");
}

// How many frames a kick of this strength keeps the output up.
int framesUp(float strength, float fadeIn, float hold, float fadeOut, std::vector<float>* trace = nullptr) {
    Rig rig;
    rig.load(oneOn(kKick, param(kKick, LfoTarget::Zoom, 1.0f, fadeIn, hold, fadeOut)));
    int up = 0;
    run(rig, 3.0f, 10.0f, [&](int) {
        if (rig.out() > 0.01f) ++up;
        if (trace) trace->push_back(rig.out());
    }, strength);
    return up;
}

void theEnvelopeKeepsItsTimes() {
    const int hard = framesUp(1.0f, 0.05f, 0.0f, 0.5f);
    const int soft = framesUp(0.2f, 0.05f, 0.0f, 0.5f);
    check(std::abs(hard - soft) <= 3, "the fade out lasts as long at any strength");
    std::vector<float> trace;
    framesUp(1.0f, 0.0f, 0.0f, 0.3f, &trace);
    check(trace[0] < 1.0f && trace[1] < trace[0], "with no fade in and no hold a hit lets go at once");
}

void aLostTempoLetsTheEnvelopeGo() {
    resp::Response r = param(kKick, LfoTarget::Zoom, 1.0f, 0.2f, 4.0f, 1.0f);
    r.unit = resp::TimeUnit::Beats;
    Rig rig;
    rig.load(oneOn(kKick, r));
    for (int i = 0; i < 15; ++i) {
        GeodeSoundFrame f = silent();
        if (i == 0) f.kick = 1.0f;
        f.bpm = 100.0f;
        rig.step(f);
    }
    check(rig.out() > 0.95f, "holding");
    rig.step(silent());  // no tempo any more: the sustain gives way to the release on this frame
    check(rig.out() > 0.5f, "the tempo going does not cut it");
    for (int i = 0; i < 5; ++i) rig.step(silent());
    const float later = rig.out();
    check(later > 0.5f && later < 1.0f, "it falls over its fade out");
    for (int i = 0; i < 100; ++i) rig.step(silent());
    check(rig.out() == 0.0f, "and it ends");
}

void aWhileResponseIsNotPulledBackByAPassedOverHit() {
    resp::Response r;
    r.row = kKick;
    r.kind = resp::kThing;
    r.drive = resp::DriveMode::While;
    r.everyNth = 2;
    Rig rig;
    rig.bank.registerKind(resp::kThing, &rig.counter);
    rig.load(oneOn(kKick, r));
    for (int i = 0; i < 60; ++i) {
        GeodeSoundFrame f = silent();
        if (i == 0 || i == 30) f.kick = 1.0f;
        f.kickEnv = (i < 10 || (i >= 30 && i < 40)) ? 1.0f : 0.0f;
        rig.step(f);
    }
    check(rig.counter.plays.size() == 1 && rig.counter.released == 1, "a hit the Nth passed over does not reopen a released response");
}

void aKeyNeedsClarity() {
    ReactSignals s;
    int came = 0;
    MomentWatch w;
    for (int i = 0; i < 400; ++i) {
        GeodeSoundFrame f = silent();
        f.keyHue = i < 100 ? 0.0f : 1.0f / 12.0f;
        f.keyClarity = i < 100 ? 0.8f : 0.1f;
        s.step(f, kDt);
        if ((w.step(s) & (1u << static_cast<int>(Moment::KeyChange))) != 0u) ++came;
    }
    check(came == 0, "an unclear key does not change");
}

}  // namespace

int main() {
    everyRowStartsOff();
    aResponseFadesInHoldsAndFadesOut();
    theStrengthCurveShapesTheEvent();
    whileHoldsAsLongAsTheEventAndFollowTracksIt();
    everyNthChanceAndCooldown();
    chanceReplaysTheSameSequence();
    beatAndBarComeOnlyWithABeatHeard();
    beatUnitEnvelopesIdleAtTempoZero();
    theNewMoments();
    aKeyChangeIsAKeyHeldForAMoment();
    laterKindsRegisterWithoutTouchingTheBank();
    compileFillsTheFreeSlotsFromTheTop();
    aRowThatIsOffStillPlaysTheStyle();
    aMutedChipSilencesOnlyThatRoute();
    aHandRouteAlwaysPlays();
    aSavedLookWithNoResponderTextIsUntouched();
    chipMutesSurviveSaveAndLoad();
    aPreviewPlaysOnceNow();
    theCostBudgetIsReadable();
    savedTextReadsBack();
    oldTextStillLoads();
    theNativeInterfaceEditsTheText();
    aRuntimeStaysOnlyWithTheSameResponse();
    theSeededSequenceRestartsWithThePlay();
    anOlderReaderTakesAutoWithResponsesForNoRoutes();
    aPreviewGivesItsRouteBack();
    aPreviewPlaysOnTheWallClock();
    theEnvelopeKeepsItsTimes();
    aLostTempoLetsTheEnvelopeGo();
    aWhileResponseIsNotPulledBackByAPassedOverHit();
    aKeyNeedsClarity();
    if (failures != 0) {
        std::printf("responder_test: %d failed\n", failures);
        return 1;
    }
    std::printf("responder_test: all passed\n");
    return 0;
}
