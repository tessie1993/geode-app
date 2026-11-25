// Host checks for Clock control (core/viz/clock/SceneClock, ClockResponse): a freeze eases the scene clock's scale to 0
// and back at any frame rate, a stutter loops the last L seconds R times and resumes where real time is, a beat-length
// stutter does nothing while no tempo is heard (never a stand-in tempo), a reverse runs the clock back and eases home,
// the last response fired wins, reset starts over, two runs of the same responses give the same scene times, and a
// Response of kind Clock played by the bank drives the clock. Built and run by CI (native-host-tests.yml); no GL.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "viz/clock/ClockResponse.hpp"
#include "viz/modulation/Reactions.hpp"
#include "viz/modulation/Responder.hpp"

using namespace geode::viz;
namespace resp = geode::viz::responder;
using sceneclock::ClockKind;
using sceneclock::SceneClock;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// Display rates, none of them a tempo.
constexpr int kRates[] = {30, 60, 90, 144};
constexpr int kKick = static_cast<int>(Moment::Kick);

int frames(float seconds, int fps) { return static_cast<int>(std::lround(seconds * static_cast<float>(fps))); }

// ── The clock alone ──────────────────────────────────────────────────────────────────────────────────────

void atRestTheStepIsTheFramesOwn() {
    SceneClock c;
    bool same = true;
    for (int fps : kRates) {
        const float dt = 1.0f / static_cast<float>(fps);
        same = same && c.step(dt) == dt && c.rate() == 1.0f && !c.holds() && !c.active();
    }
    check(same, "with nothing fired the scene step is the frame's own, bit for bit");
    check(c.step(0.0f) == 0.0f && c.step(-1.0f) == 0.0f && c.step(NAN) == 0.0f, "a step that is not positive and finite moves nothing");
}

void aFreezeEasesToZeroAndBackAtEveryRate() {
    for (int fps : kRates) {
        const float dt = 1.0f / static_cast<float>(fps);
        SceneClock c;
        for (int i = 0; i < fps; ++i) c.step(dt);
        const double start = c.value();
        c.freeze(0.2f, 0.5f, 0.3f);
        double lowest = 1.0;
        float atHalfEase = -1.0f;
        float inHold = -1.0f;
        float after = -1.0f;
        const int total = frames(2.0f, fps);
        for (int i = 1; i <= total; ++i) {
            c.step(dt);
            const float t = static_cast<float>(i) * dt;
            lowest = std::min(lowest, static_cast<double>(c.rate()));
            if (i == frames(0.1f, fps)) atHalfEase = c.rate();
            if (i == frames(0.45f, fps)) inHold = c.rate();
            if (i == frames(1.5f, fps)) after = c.rate();
            check(c.rate() >= 0.0f && c.rate() <= 1.0f, "a freeze never leaves 0..1 at " + std::to_string(fps) + " fps (t " + std::to_string(t) + ")");
        }
        const std::string at = " at " + std::to_string(fps) + " fps";
        check(near(atHalfEase, 0.5f, 0.1f), "halfway through the fade in the scale is about half" + at);
        check(inHold == 0.0f && lowest == 0.0, "the scale stands at 0 for the hold" + at);
        check(after == 1.0f && !c.active(), "after the fade out the clock is at rest" + at);
        // 2 s of clock less what the freeze took: 0.2 * 0.5 + 0.5 + 0.3 * 0.5 = 0.75.
        check(near(c.value() - start, 1.25, 0.03), "the clock advanced by what the envelope leaves" + at);
    }
}

void holdLastFrameOnlyAtZero() {
    SceneClock c;
    c.freeze(0.1f, 0.5f, 0.1f, true);
    bool early = false;
    bool held = false;
    for (int i = 0; i < 60; ++i) {
        c.step(1.0f / 60.0f);
        if (i == 2) early = c.holds();
        if (i == 20) held = c.holds();
    }
    check(!early && held, "a frozen frame holds only once the scale stands at 0");
    for (int i = 0; i < 120; ++i) c.step(1.0f / 60.0f);
    check(!c.holds(), "and not after the freeze ends");
    SceneClock plain;
    plain.freeze(0.0f, 1.0f, 0.0f, false);
    plain.step(1.0f / 60.0f);
    check(plain.rate() == 0.0f && !plain.holds(), "with the mode off a frozen frame still draws");
}

void aStutterLoopsThenResumesWhereRealTimeIs() {
    for (int fps : kRates) {
        const float dt = 1.0f / static_cast<float>(fps);
        SceneClock c;
        for (int i = 0; i < fps; ++i) c.step(dt);
        const double anchor = c.value();
        c.stutter(0.25f, 3);
        const std::string at = " at " + std::to_string(fps) + " fps";
        bool inside = true;
        bool wentBack = false;
        const int loopFrames = frames(0.75f, fps) - 1;
        for (int i = 1; i <= loopFrames; ++i) {
            const float d = c.step(dt);
            wentBack = wentBack || d < 0.0f;
            inside = inside && c.value() >= anchor - 0.25 - 1e-4 && c.value() <= anchor + 1e-4;
        }
        check(inside && wentBack, "the clock stays inside the last 0.25 s and jumps back at each loop" + at);
        for (int i = 0; i < frames(0.5f, fps); ++i) c.step(dt);
        const double elapsed = static_cast<double>(loopFrames + frames(0.5f, fps)) * static_cast<double>(dt);
        check(near(c.value(), anchor + elapsed, 1e-3), "after the loops the clock is where real time is" + at);
        check(c.rate() == 1.0f && !c.active(), "and runs at its own rate again" + at);
    }
}

void aStutterWithNothingToLoopDoesNothing() {
    SceneClock c;
    c.stutter(0.5f, 2);
    check(!c.active() && c.step(0.02f) == 0.02f, "no history, no loop");
    for (int i = 0; i < 100; ++i) c.step(0.02f);
    c.stutter(0.0f, 2);
    c.stutter(0.5f, 0);
    check(!c.active(), "no length or no repeats, no loop");
    c.freeze(0.0f, 10.0f, 0.0f);
    for (int i = 0; i < 100; ++i) c.step(0.02f);
    c.stutter(0.5f, 2);
    check(c.rate() == 0.0f, "a clock that has not moved has nothing to loop, and the freeze plays on");
}

void aReverseGoesNegativeThenEasesBack() {
    SceneClock c;
    for (int i = 0; i < 60; ++i) c.step(1.0f / 60.0f);
    const double before = c.value();
    c.reverse(1.0f, 0.1f, 0.5f, 0.2f);
    float lowest = 1.0f;
    float negativeStep = 0.0f;
    for (int i = 0; i < 120; ++i) {
        const float d = c.step(1.0f / 60.0f);
        lowest = std::min(lowest, c.rate());
        negativeStep = std::min(negativeStep, d);
    }
    check(lowest == -1.0f && negativeStep < 0.0f, "the reverse runs the clock at -1x");
    check(c.rate() == 1.0f && !c.active(), "and eases back to 1x");
    check(c.value() < before + 2.0 - 0.9, "it gave back the time it ran backwards");
    SceneClock fast;
    fast.reverse(0.0f, 0.0f, 1.0f, 0.0f);
    fast.step(0.1f);
    check(fast.rate() == -1.0f, "a rate of 0 reads as 1x backwards");
}

void theLastFiredWins() {
    SceneClock c;
    c.freeze(0.0f, 10.0f, 0.0f);
    c.step(0.02f);
    check(c.rate() == 0.0f, "a freeze stands");
    c.reverse(1.0f, 0.0f, 10.0f, 0.0f);
    c.step(0.02f);
    check(c.rate() == -1.0f, "a reverse fired after it takes the clock");
    c.freeze(0.0f, 10.0f, 0.0f);
    c.step(0.02f);
    check(c.rate() == 0.0f, "and a freeze fired after that takes it back");
    // A reverse fired into a freeze's ease starts from where the scale is, never from 1.
    SceneClock e;
    e.freeze(1.0f, 10.0f, 0.0f);
    for (int i = 0; i < 25; ++i) e.step(0.02f);
    const float mid = e.rate();
    e.reverse(1.0f, 1.0f, 10.0f, 0.0f);
    e.step(0.0001f);
    check(mid < 1.0f && mid > 0.0f && near(e.rate(), mid, 0.01f), "an ease starts from the scale it finds");
}

void followMapsALevelToAScaleInMinusOneToOne() {
    SceneClock c;
    c.follow(0.5f, 0.0f);
    c.step(0.02f);
    check(near(c.rate(), 0.5f, 1e-6f), "half a level of a freeze is half the scale");
    c.follow(1.0f, -3.0f);
    c.step(0.02f);
    check(c.rate() == -1.0f, "the scale is held to -1");
    c.step(0.02f);
    check(c.rate() > -1.0f && c.rate() < 1.0f, "a Follow that lets go eases back, it does not snap");
    for (int i = 0; i < 20; ++i) c.step(0.02f);
    check(c.rate() == 1.0f && !c.active(), "and the clock is at rest after the short ease");
    c.follow(0.0f, 0.0f);
    c.step(0.02f);
    check(c.rate() == 1.0f, "a level of 0 claims nothing");
    SceneClock d;
    d.follow(1.0f, 0.0f);
    d.step(0.02f);
    d.reverse(1.0f, 0.0f, 10.0f, 0.0f);
    d.follow(1.0f, 0.0f);
    d.step(0.02f);
    check(d.rate() == -1.0f, "a response fired after a follow began wins over it");
}

void resetStartsOver() {
    SceneClock c;
    for (int i = 0; i < 30; ++i) c.step(0.02f);
    c.freeze(0.1f, 10.0f, 0.1f, true);
    for (int i = 0; i < 30; ++i) c.step(0.02f);
    check(c.holds(), "frozen before the reset");
    c.reset();
    check(c.rate() == 1.0f && !c.holds() && !c.active() && c.value() == 0.0, "a reset is a clock at rest at 0");
    check(c.step(0.02f) == 0.02f, "and it steps by the frame's own");
    c.stutter(0.2f, 2);
    check(!c.active(), "and holds no history to loop");
}

std::vector<double> aRun(int fps) {
    SceneClock c;
    std::vector<double> out;
    const float dt = 1.0f / static_cast<float>(fps);
    for (int i = 0; i < fps * 6; ++i) {
        if (i == fps) c.freeze(0.1f, 0.4f, 0.2f);
        if (i == 2 * fps) c.stutter(0.3f, 2);
        if (i == 4 * fps) c.reverse(1.0f, 0.1f, 0.3f, 0.1f);
        c.step(dt);
        out.push_back(c.value());
    }
    return out;
}

void twoRunsGiveTheSameSceneTimes() {
    for (int fps : kRates) check(aRun(fps) == aRun(fps), "an export replays the same scene times at " + std::to_string(fps) + " fps");
}

// ── The kind, played by the bank ─────────────────────────────────────────────────────────────────────────

// The matrix, the moments and the bank wired as the resolver wires them, with the clock kind registered.
struct Rig {
    ReactionEngine engine;
    MomentWatch watch;
    resp::Bank bank;
    SceneClock clock;
    ClockKind kind{clock};
    std::array<ReactRoute, ReactionEngine::kRoutes> hand{};
    float dt = 1.0f / 60.0f;
    float bpm = 0.0f;
    float delta = 0.0f;

    Rig() { bank.registerKind(resp::kClock, &kind); }

    void load(const resp::Response& r) {
        resp::Config c = resp::newStyleConfig();
        resp::setRowOn(c, kKick, true);
        resp::addResponse(c, r);
        bank.configure(c);
        engine.routes = hand;
        bank.compile(engine.routes);
    }
    void step(bool kick, float level = 0.0f) {
        GeodeSoundFrame f{};
        f.drive = 1.0f;
        f.bpm = bpm;
        if (kick) f.kick = 1.0f;
        f.kickEnv = level;
        engine.stepSignals(f, dt);
        const std::uint32_t moments = watch.step(engine.signals());
        bank.step(f, dt, engine.signals(), moments, engine);
        engine.tickRoutes();
        delta = clock.step(dt);
    }
    void run(int n) {
        for (int i = 0; i < n; ++i) step(false);
    }
};

resp::Response clockResponse(ClockKind::Mode mode) {
    resp::Response r;
    r.row = kKick;
    r.kind = resp::kClock;
    r.fadeIn = 0.1f;
    r.hold = 0.3f;
    r.fadeOut = 0.1f;
    r.params[ClockKind::kMode] = static_cast<float>(mode);
    return r;
}

void aFiredFreezeResponseDrivesTheClock() {
    Rig rig;
    resp::Response r = clockResponse(ClockKind::kFreeze);
    r.params[ClockKind::kHoldFrame] = 1.0f;
    rig.load(r);
    rig.run(30);
    check(rig.clock.rate() == 1.0f, "nothing moves the clock until a response fires");
    rig.step(true);
    rig.run(12);
    check(rig.clock.rate() == 0.0f && rig.clock.holds(), "the fired response freezes the scene clock, holding the frame");
    rig.run(60);
    check(rig.clock.rate() == 1.0f && !rig.clock.holds(), "and gives it back after its hold and fade out");
}

void aWhileFreezeLastsAsLongAsItsEvent() {
    Rig rig;
    resp::Response r = clockResponse(ClockKind::kFreeze);
    r.drive = resp::DriveMode::While;
    rig.load(r);
    rig.step(true, 1.0f);
    for (int i = 0; i < 90; ++i) rig.step(false, 1.0f);
    check(rig.clock.rate() == 0.0f, "a while freeze outlasts its hold while the event holds");
    for (int i = 0; i < 40; ++i) rig.step(false, 0.0f);
    check(rig.clock.rate() == 1.0f, "and eases back once the event ends");
}

void aWhileFreezeDroppedFromTheBankLetsGo() {
    Rig rig;
    resp::Response r = clockResponse(ClockKind::kFreeze);
    r.drive = resp::DriveMode::While;
    r.params[ClockKind::kHoldFrame] = 1.0f;
    rig.load(r);
    rig.step(true, 1.0f);
    for (int i = 0; i < 30; ++i) rig.step(false, 1.0f);
    check(rig.clock.rate() == 0.0f && rig.clock.holds(), "a while freeze stands while its event holds");
    rig.bank.configure(resp::newStyleConfig());
    rig.run(30);
    check(rig.clock.rate() == 1.0f && !rig.clock.holds(), "removing the response lets the clock run again");
    Rig again;
    again.load(r);
    again.step(true, 1.0f);
    for (int i = 0; i < 30; ++i) again.step(false, 1.0f);
    again.bank.reset();
    again.run(30);
    check(again.clock.rate() == 1.0f, "so does a restart of the bank");
}

void aFollowResponseMapsTheLevelToTheScale() {
    Rig rig;
    resp::Response r = clockResponse(ClockKind::kReverse);
    r.drive = resp::DriveMode::Follow;
    r.params[ClockKind::kRate] = 1.0f;
    rig.load(r);
    for (int i = 0; i < 30; ++i) rig.step(false, 0.5f);
    check(near(rig.clock.rate(), 0.0f, 0.02), "half a level of a reverse stands the clock still");
    for (int i = 0; i < 30; ++i) rig.step(false, 1.0f);
    check(near(rig.clock.rate(), -1.0f, 0.02) && rig.delta < 0.0f, "a full level runs it back");
    for (int i = 0; i < 30; ++i) rig.step(false, 0.0f);
    check(near(rig.clock.rate(), 1.0f, 0.02), "no level, the clock is at rest");
}

void aBeatLengthStutterIdlesWithoutATempo() {
    resp::Response r = clockResponse(ClockKind::kStutter);
    r.params[ClockKind::kLoop] = 1.0f;
    r.params[ClockKind::kLoopUnit] = 1.0f;
    r.params[ClockKind::kRepeats] = 3.0f;
    Rig silent;
    silent.load(r);
    silent.run(120);
    silent.step(true);
    bool steady = silent.delta > 0.0f;
    for (int i = 0; i < 120; ++i) {
        silent.step(false);
        steady = steady && silent.delta == silent.dt;
    }
    check(steady && !silent.clock.active(), "at tempo 0 a stutter set in beats does nothing");
    Rig heard;
    heard.bpm = 100.0f;
    heard.load(r);
    heard.run(240);
    const double anchor = heard.clock.value();
    heard.step(true);
    bool wentBack = false;
    double deepest = 0.0;
    for (int i = 0; i < 120; ++i) {
        heard.step(false);
        wentBack = wentBack || heard.delta < 0.0f;
        deepest = std::max(deepest, anchor - heard.clock.value());
    }
    check(wentBack && near(deepest, 0.6, 0.05), "with a tempo heard the same response loops one beat (0.6 s)");
    check(near(heard.clock.value(), anchor + 121.0 / 60.0, 0.02), "and resumes where real time is");
}

void aSecondsStutterLoopsTheKindParams() {
    resp::Response r = clockResponse(ClockKind::kStutter);
    r.params[ClockKind::kLoop] = 0.2f;
    r.params[ClockKind::kRepeats] = 2.0f;
    Rig rig;
    rig.load(r);
    rig.run(120);
    const double anchor = rig.clock.value();
    rig.step(true);
    int jumps = 0;
    for (int i = 0; i < 40; ++i) {
        rig.step(false);
        if (rig.delta < 0.0f) ++jumps;
    }
    check(jumps >= 2, "a stutter of 0.2 s twice jumps back at its first step and at the loop");
    check(near(rig.clock.value(), anchor + 41.0 / 60.0, 0.02), "and resumes where real time is");
}

void aRunOfTheKindIsDeterministic() {
    const auto go = [] {
        Rig rig;
        resp::Response r = clockResponse(ClockKind::kStutter);
        r.params[ClockKind::kLoop] = 0.25f;
        r.params[ClockKind::kRepeats] = 3.0f;
        r.cooldown = 0.0f;
        rig.load(r);
        std::vector<double> out;
        for (int i = 0; i < 600; ++i) {
            rig.step(i % 90 == 0);
            out.push_back(rig.clock.value());
        }
        return out;
    };
    check(go() == go(), "the same responses at the same times give the same scene times");
}

}  // namespace

int main() {
    atRestTheStepIsTheFramesOwn();
    aFreezeEasesToZeroAndBackAtEveryRate();
    holdLastFrameOnlyAtZero();
    aStutterLoopsThenResumesWhereRealTimeIs();
    aStutterWithNothingToLoopDoesNothing();
    aReverseGoesNegativeThenEasesBack();
    theLastFiredWins();
    followMapsALevelToAScaleInMinusOneToOne();
    resetStartsOver();
    twoRunsGiveTheSameSceneTimes();
    aFiredFreezeResponseDrivesTheClock();
    aWhileFreezeLastsAsLongAsItsEvent();
    aWhileFreezeDroppedFromTheBankLetsGo();
    aFollowResponseMapsTheLevelToTheScale();
    aBeatLengthStutterIdlesWithoutATempo();
    aSecondsStutterLoopsTheKindParams();
    aRunOfTheKindIsDeterministic();
    if (failures == 0) std::printf("scene_clock_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
