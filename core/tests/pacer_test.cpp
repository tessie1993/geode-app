// Host-side checks for core/viz/frame/Pacer (#436): the frame clock every surface shares. Built and
// run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: whole-number divides of the panel rate (60/90/120 Hz panels capped at 30 and 60),
// noise tolerance (a 119.6 Hz panel still halves), no divide until the estimate settles, hitches
// left out of the estimate, a resume that forgets the gap but keeps the period, and the one dt clamp
// the renderer shares.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "viz/frame/Pacer.hpp"

using namespace geode::viz::frame;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

// Runs `vsyncs` callbacks at `hz` from t0 and returns how many were drawn after the settle window.
struct Run {
    int drawn = 0;
    int settledDrawn = 0;
    double lastDt = 0.0;
};

Run runPanel(PacerState& s, double hz, float cap, int vsyncs, int64_t t0 = 1'000'000'000) {
    Run r;
    const double period = 1e9 / hz;
    for (int i = 0; i < vsyncs; ++i) {
        const auto t = t0 + static_cast<int64_t>(std::llround(period * i));
        const double dt = Pacer::onVsync(s, t, cap);
        if (dt > 0.0) {
            r.drawn++;
            if (i >= 2 * Pacer::kSettleFrames) r.settledDrawn++;
            r.lastDt = dt;
        }
    }
    return r;
}

void divides() {
    struct Case {
        double hz;
        float cap;
        int64_t divisor;
    };
    const Case cases[] = {{60, 60, 1}, {60, 30, 2}, {90, 30, 3}, {90, 60, 1}, {120, 60, 2}, {120, 30, 4}, {119.6, 60, 2}, {144, 60, 2}, {165, 24, 6}};
    for (const auto& c : cases) {
        PacerState s;
        const Run r = runPanel(s, c.hz, c.cap, 600);
        char what[96];
        std::snprintf(what, sizeof what, "%.1f Hz capped at %.0f divides by %lld", c.hz, c.cap, static_cast<long long>(c.divisor));
        check(s.divisor == c.divisor, what);
        std::snprintf(what, sizeof what, "%.1f Hz capped at %.0f draws dt = divisor periods", c.hz, c.cap);
        check(near(r.lastDt, static_cast<double>(c.divisor) / c.hz, 2e-4), what);
    }
    PacerState uncapped;
    runPanel(uncapped, 120, 0.0f, 100);
    check(uncapped.divisor == 1, "no cap draws every vsync");
}

void settlesBeforeDividing() {
    PacerState s;
    int drawnEarly = 0;
    const int64_t period = 8'333'333;  // 120 Hz
    for (int i = 0; i < Pacer::kSettleFrames; ++i) {
        if (Pacer::onVsync(s, 1'000'000'000 + period * i, 60.0f) > 0.0) drawnEarly++;
    }
    check(drawnEarly == Pacer::kSettleFrames, "every vsync draws until the period estimate settles");
}

void firstFrameAndClamp() {
    PacerState s;
    check(near(Pacer::onVsync(s, 5'000'000'000, 0.0f), kFirstDtSeconds, 1e-12), "the first frame of a run steps a nominal 1/60 s");
    // A one-second stall: clamped, and left out of the period estimate.
    const int64_t before = s.periodNs;
    check(near(Pacer::onVsync(s, 6'000'000'000, 0.0f), kMaxDtSeconds, 1e-12), "a stall steps at most the shared maximum");
    check(s.periodNs == before, "a stall is not a refresh period");
    check(near(clampDt(0.0), kMinDtSeconds, 1e-12) && near(clampDt(5.0), kMaxDtSeconds, 1e-12), "the dt clamp's two ends");
    check(near(clampDt(0.02), 0.02, 1e-12), "a normal frame passes the clamp untouched");
}

void hitchesLeftOut() {
    PacerState s;
    runPanel(s, 60, 30.0f, 100);
    const int64_t settled = s.periodNs;
    // Two callbacks 2 ms apart (a pacer-thread hitch): faster than any panel, ignored.
    Pacer::onVsync(s, s.lastVsyncNs + 2'000'000, 30.0f);
    check(s.periodNs == settled, "a 2 ms gap is a hitch, not a 500 Hz panel");
    check(s.divisor == 2, "and the divide holds");
}

void resumeKeepsPeriod() {
    PacerState s;
    runPanel(s, 120, 60.0f, 200);
    const int64_t period = s.periodNs;
    Pacer::resume(s);
    check(s.periodNs == period && s.samples == Pacer::kSettleFrames, "a resume keeps the panel's period");
    check(s.lastDrawnNs == 0 && s.lastVsyncNs == 0 && s.sinceDrawn == 0, "and forgets the gap");
    // Ten minutes later: the first drawn frame is nominal, not a 600 s step or a clamp-sized jump. The
    // divide of 2 still holds, so the first vsync after the resume may be skipped.
    double dt = Pacer::onVsync(s, 700'000'000'000, 60.0f);
    if (dt == 0.0) dt = Pacer::onVsync(s, 700'000'000'000 + period, 60.0f);
    check(near(dt, kFirstDtSeconds, 1e-12), "the first frame after a resume is nominal");
}

void followsAModeChange() {
    PacerState s;
    runPanel(s, 120, 60.0f, 300);
    check(s.divisor == 2, "120 Hz halves to 60");
    // setFrameRate lands and the panel drops to 60 Hz: the estimate follows, the divide becomes 1.
    runPanel(s, 60, 60.0f, 300, s.lastVsyncNs + 16'666'667);
    check(s.divisor == 1, "after the panel drops to 60 Hz every vsync draws");
}

void wireRoundTrip() {
    PacerState s{1, 2, 3, 4, 5, 6};
    int64_t wire[kPacerStateLongs] = {};
    pack(s, wire);
    const PacerState back = unpack(wire);
    check(back.lastDrawnNs == 1 && back.lastVsyncNs == 2 && back.periodNs == 3 && back.samples == 4 && back.sinceDrawn == 5 && back.divisor == 6,
          "the six-long wire layout round-trips");
}

// ---- #452: what a surface sees, through the public seam only (Pacer::onVsync, Pacer::resume,
// clampDt): which vsyncs draw and the dt each draw steps. Time is the one boundary injected: the
// test owns every frameTimeNanos, with seeded jitter. Expected values are hand-worked literals or the
// wall clock the test itself generated, never recomputed the way the pacer does it.

// One callback and the pacer's answer.
struct Tick {
    int vsync;    // which panel vsync this callback was for
    int64_t t;    // its frameTimeNanos
    double dt;    // the answer: 0 = skip, else draw with this dt
};

// Feeds `vsyncs` vsyncs of a `hz` panel to the pacer at `cap`, from `t0`, labelled from
// `firstVsync`. Each timestamp gets seeded jitter of up to +-`jitterNs`. With `dropPercent` > 0 a
// callback is sometimes missed (never two in a row), as a late pacer thread misses one.
std::vector<Tick> feed(PacerState& s, double hz, float cap, int vsyncs, int64_t jitterNs = 0, uint64_t seed = 1,
                       int dropPercent = 0, int64_t t0 = 1'000'000'000, int firstVsync = 0) {
    std::vector<Tick> out;
    uint64_t x = seed;
    auto next01 = [&x]() {
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>(x >> 11) * (1.0 / 9007199254740992.0);
    };
    bool droppedLast = false;
    const double period = 1e9 / hz;
    for (int i = firstVsync; i < firstVsync + vsyncs; ++i) {
        const auto jitter = static_cast<int64_t>(std::llround((2.0 * next01() - 1.0) * static_cast<double>(jitterNs)));
        const int64_t t = t0 + static_cast<int64_t>(std::llround(period * (i - firstVsync))) + jitter;
        const bool drop = dropPercent > 0 && !droppedLast && i > firstVsync && next01() * 100.0 < dropPercent;
        droppedLast = drop;
        if (drop) continue;
        out.push_back({i, t, Pacer::onVsync(s, t, cap)});
    }
    return out;
}

std::vector<Tick> drawnOf(const std::vector<Tick>& ticks) {
    std::vector<Tick> d;
    for (const auto& k : ticks)
        if (k.dt > 0.0) d.push_back(k);
    return d;
}

// Red against: kMaxDtSeconds back at the deleted Kotlin's 1/15 s (a 12 fps frame loses 17 ms) or
// raised (a stall steps more than 100 ms), kMinDtSeconds at 0 (a zero interval answers 0, which the
// shell reads as "skip") or raised past 4 ms (a 250 Hz frame is stretched), clampDt's arguments
// swapped (a 250 ms hitch passes whole).
void dtRuleSpansTenToTwoFiftyFps() {
    check(near(clampDt(1.0 / 12.0), 0.0833333333, 1e-9), "a 12 fps frame steps its whole 83.3 ms");
    check(near(clampDt(1.0 / 10.0), 0.1, 1e-12), "a 10 fps frame steps its whole 100 ms");
    check(near(clampDt(0.25), 0.1, 1e-12), "a 250 ms hitch steps 100 ms");
    check(near(clampDt(3600.0), 0.1, 1e-12), "an hour-long stall steps 100 ms");
    check(near(clampDt(0.004), 0.004, 1e-15), "a 250 Hz frame steps its whole 4 ms");
    check(clampDt(0.0) >= 0.001 && clampDt(0.0) <= 0.002, "a zero interval still steps about 1 ms, never 0");
    check(clampDt(-0.5) >= 0.001 && clampDt(-0.5) <= 0.002, "a clock that steps back still steps about 1 ms forward");
}

// Red against: kFirstDtSeconds changed (0 freezes every envelope), `previous == 0` negated (the first
// dt becomes the clamp, 100 ms).
void firstFrameOfARunIsOneSixtieth() {
    PacerState s;
    check(near(Pacer::onVsync(s, 7'000'000'000, 30.0f), 0.0166666667, 1e-9),
          "a cold start draws at once with 16.7 ms, even under a 30 fps cap");
}

// Red against: dt taken from the period estimate instead of the real interval (the sum drifts from
// wall time), a sign flip in `frameTimeNs - previous` or a /1e6 unit slip (every dt pinned to a
// clamp end), `sinceDrawn = 0` removed (every callback draws), `<` -> `<=` in the divide (one
// callback too many between draws), the smoothing step's `/` -> `*` (the estimate runs away and the
// divide breaks), a hitch window that swallows the 33 ms gap of a missed callback at 60 Hz.
void longJitteredRunNeitherSkipsNorDoublesAndKeepsWallTime() {
    struct Case {
        double hz;
        float cap;
        int every;         // draw every Nth callback, worked by hand from hz / cap
        int64_t jitterNs;  // small enough that the measured rate stays inside the divide's band
        int dropPercent;
    };
    // Bands, by hand: 60/30 divides by 2 for any period up to 17.09 ms (0.43 ms of room), 60/15 by 4 up
    // to 16.88 ms (0.21), 120/60 by 2 up to 8.55 ms (0.21), 120/30 by 4 up to 8.44 ms (0.11), 90/30 by 3
    // up to 11.30 ms (0.19). The estimate is a weighted mean of measured intervals, so it is never
    // further than twice the timestamp jitter from the true period.
    const Case cases[] = {
        {60, 30, 2, 200'000, 0}, {60, 15, 4, 100'000, 0}, {120, 60, 2, 100'000, 0},
        {120, 30, 4, 50'000, 0}, {90, 30, 3, 90'000, 0},  {60, 30, 2, 200'000, 3},
    };
    uint64_t seed = 0x9E3779B97F4A7C15ULL;
    for (const auto& c : cases) {
        PacerState s;
        const auto ticks = feed(s, c.hz, c.cap, 36'000, c.jitterNs, seed++, c.dropPercent);  // 5 to 10 minutes
        const double frame = c.every / c.hz;
        const double slack = 2.0 * static_cast<double>(c.jitterNs) / 1e9 + 1e-9;
        // A missed callback can stretch one draw's gap, at most to twice the cap's frame.
        const double longest = c.dropPercent > 0 ? 2.0 * frame : frame;
        size_t callbacks = 0;
        size_t settledDraws = 0;
        bool even = true;
        bool inBand = true;
        double dtSum = 0.0;
        int64_t firstT = 0;
        int64_t lastT = 0;
        for (size_t i = 0; i < ticks.size(); ++i) {
            ++callbacks;
            if (ticks[i].dt <= 0.0) continue;
            if (i >= 32) {  // well past the settle window
                if (settledDraws == 0) {
                    firstT = ticks[i].t;
                } else {
                    even = even && callbacks == static_cast<size_t>(c.every);
                    inBand = inBand && ticks[i].dt >= frame - slack && ticks[i].dt <= longest + slack;
                    dtSum += ticks[i].dt;
                    lastT = ticks[i].t;
                }
                settledDraws++;
            }
            callbacks = 0;
        }
        char what[192];
        std::snprintf(what, sizeof what, "%.0f Hz capped at %.0f (jitter %lld us, %d%% missed): a draw every %d callbacks, none skipped or doubled",
                      c.hz, c.cap, static_cast<long long>(c.jitterNs / 1000), c.dropPercent, c.every);
        check(even && settledDraws > 1000, what);
        std::snprintf(what, sizeof what, "%.0f Hz capped at %.0f: the dts sum to the wall time between the first and last draw", c.hz, c.cap);
        check(near(dtSum, static_cast<double>(lastT - firstT) / 1e9, 1e-6), what);
        std::snprintf(what, sizeof what, "%.0f Hz capped at %.0f: every dt is the cap's frame, give or take the jitter", c.hz, c.cap);
        check(inBand, what);
    }
}

// Red against: the divide's upper clamp removed (a 10 fps cap on 120 Hz waits 12 vsyncs, a 0.5 fps
// cap 240: the header promises a fast visual over a frozen one, at most 8 vsyncs between draws).
// A cap above the panel rate draws every vsync at the panel's own dt.
void capsOutsideThePanelStayWholeAndNeverFreeze() {
    {
        PacerState s;
        bool every = true;
        for (const auto& k : feed(s, 60, 240.0f, 200)) every = every && near(k.dt, 0.0166666667, 1e-6);
        check(every, "a 240 fps cap on a 60 Hz panel draws every vsync at 16.7 ms");
    }
    for (const float cap : {10.0f, 0.5f}) {
        PacerState s;
        const auto ticks = feed(s, 120, cap, 400);
        int longestGap = 0;
        int gap = 0;
        double lastDt = 0.0;
        for (size_t i = 0; i < ticks.size(); ++i) {
            ++gap;
            if (ticks[i].dt <= 0.0) continue;
            if (i >= 32) {
                if (gap > longestGap) longestGap = gap;
                lastDt = ticks[i].dt;
            }
            gap = 0;
        }
        char what[128];
        std::snprintf(what, sizeof what, "a %.1f fps cap on a 120 Hz panel draws every 8th vsync (66.7 ms), no slower", cap);
        check(longestGap == 8 && near(lastDt, 0.0666666667, 1e-6), what);
    }
}

// Red against: a divide worked out once and kept (a cap change never lands). The shipping caps move
// 60 -> 30 (Settings › Performance › Saver) -> 15 -> 30 -> 60 on a settled 60 Hz panel, each change
// right after a draw: the next draw comes exactly one new cap frame later, and so does every one after.
void aCapChangeLandsOnTheNextVsync() {
    PacerState s;
    const double hz = 60.0;
    const auto warm = feed(s, hz, 60.0f, 100);
    int v = 100;
    int64_t t0 = warm.back().t + 16'666'667;
    struct Step {
        float cap;
        int every;
        double dt;
    };
    // By hand: 60 Hz / 30 = every 2nd vsync, 33.3 ms; / 15 = every 4th, 66.7 ms.
    const Step steps[] = {{30, 2, 0.0333333333}, {15, 4, 0.0666666667}, {30, 2, 0.0333333333}, {60, 1, 0.0166666667}};
    for (const auto& st : steps) {
        const auto phase = feed(s, hz, st.cap, 40, 0, 1, 0, t0, v);
        int gap = 0;
        bool ok = true;
        for (const auto& k : phase) {
            ++gap;
            if (k.dt <= 0.0) continue;
            ok = ok && gap == st.every && near(k.dt, st.dt, 1e-6);
            gap = 0;
        }
        char what[96];
        std::snprintf(what, sizeof what, "a cap change to %.0f fps draws every %d vsyncs from the next vsync on", st.cap, st.every);
        check(ok && gap == 0, what);
        v += 40;
        t0 = phase.back().t + 16'666'667;
    }
}

// Red against: the stall folded into the period estimate (`||` -> `&&` in the hitch window, or its
// upper bound removed: the divide collapses to 1 and the surface draws at 60), the stalled dt left
// unclamped (1.5 s in one step). A 30 fps surface on a 60 Hz panel stalls 1.5 s: one draw steps
// 100 ms, then it is straight back to 33.3 ms every second vsync.
void aStallMidRunStepsOneClampedFrameThenResumesTheCadence() {
    PacerState s;
    const auto before = feed(s, 60, 30.0f, 100);  // vsyncs 0..99; by hand 99 is a draw (9, 11, ... 99)
    const auto after = feed(s, 60, 30.0f, 60, 0, 1, 0, before.back().t + 1'500'000'000, 100);
    const auto drawn = drawnOf(after);
    check(before.back().dt > 0.0 && !drawn.empty() && drawn[0].vsync - 100 < 2 && near(drawn[0].dt, 0.1, 1e-12),
          "the first draw after a 1.5 s stall comes within two vsyncs and steps 100 ms");
    bool cadence = drawn.size() >= 20;
    for (size_t i = 1; i < drawn.size(); ++i)
        cadence = cadence && drawn[i].vsync - drawn[i - 1].vsync == 2 && near(drawn[i].dt, 0.0333333333, 1e-6);
    check(cadence, "and the 30 fps cadence picks up at once, 33.3 ms every second vsync");
}

// Red against: resume() keeping the last draw time (the first dt is the 10-minute pause, clamped to
// 100 ms), or wiping the period (a settle window of drawing at the panel rate after every resume).
// A 30 fps surface on a 120 Hz panel is hidden for 10 minutes.
void aResumeDrawsANominalFrameThenTheCapCadence() {
    PacerState s;
    feed(s, 120, 30.0f, 300);
    Pacer::resume(s);
    const auto drawn = drawnOf(feed(s, 120, 30.0f, 40, 0, 1, 0, 700'000'000'000));
    check(drawn.size() == 10, "after a resume a 30 fps cap on 120 Hz draws 10 of 40 vsyncs, no burst at the panel rate");
    check(!drawn.empty() && near(drawn[0].dt, 0.0166666667, 1e-9), "the first frame after a resume steps 16.7 ms, not the pause");
    bool rest = drawn.size() > 1;
    for (size_t i = 1; i < drawn.size(); ++i) rest = rest && near(drawn[i].dt, 0.0333333333, 1e-6);
    check(rest, "and every later frame steps 33.3 ms");
}

// Red against: `samples == 0 ? delta : ...` -> always smoothing from 0 (the estimate opens at 66% of
// the period: 120 Hz reads as 183 Hz, and 120/30 opens with a 5-vsync gap of 41.7 ms), a settle window
// of 0 or 1 interval (one noisy interval picks the divide, so the opening vsyncs skip). A 120 Hz panel
// capped at 30: the first four vsyncs draw while the panel is measured, and from then on every gap is
// either 1 (still measuring) or exactly 4, never anything between or past.
void theDivideNeverOpensOnAnUnevenGap() {
    PacerState s;
    const auto ticks = feed(s, 120, 30.0f, 80);
    bool firstFour = ticks.size() >= 4;
    for (size_t i = 0; i < 4 && i < ticks.size(); ++i) firstFour = firstFour && ticks[i].dt > 0.0;
    check(firstFour, "the first four vsyncs all draw while the panel is being measured");
    const auto drawn = drawnOf(ticks);
    bool dividing = false;
    bool clean = drawn.size() > 15;
    for (size_t i = 1; i < drawn.size(); ++i) {
        const int gap = drawn[i].vsync - drawn[i - 1].vsync;
        if (gap != 1) dividing = true;
        if (dividing) clean = clean && gap == 4 && near(drawn[i].dt, 0.0333333333, 1e-6);
        else clean = clean && near(drawn[i].dt, 0.0083333333, 1e-6);
    }
    check(clean && dividing, "120 Hz capped at 30 goes from every vsync straight to every 4th, 33.3 ms each");
}

// Red against: `<` -> `<=` on the 4 ms end or `>` -> `>=` on the 22 ms end of the hitch window (the
// deleted Kotlin's MIN_VSYNC_NANOS = 4_000_000 and MAX_VSYNC_NANOS = 22_000_000, both inclusive),
// and a window narrowed so 240 Hz or 48 Hz panels go unmeasured. A measured panel divides by 2 at
// these caps (by hand: 250/125, 240/120, 48/24, 45.45/20); an unmeasured one draws every vsync.
void panelsFrom4To22MsAreMeasured() {
    struct Case {
        int64_t periodNs;
        float cap;
        bool measured;
        const char* what;
    };
    const Case cases[] = {
        {4'000'000, 125.0f, true, "a 4.000 ms (250 Hz) panel is measured and divides"},
        {3'999'999, 125.0f, false, "a 3.999999 ms interval is a hitch: no divide"},
        {4'166'667, 120.0f, true, "a 240 Hz panel is measured and divides"},
        {20'833'333, 24.0f, true, "a 48 Hz panel is measured and divides"},
        {22'000'000, 20.0f, true, "a 22.000 ms panel is measured and divides"},
        {22'000'001, 20.0f, false, "a 22.000001 ms interval is a hitch: no divide"},
    };
    for (const auto& c : cases) {
        PacerState s;
        int drawn = 0;
        for (int i = 0; i < 64; ++i)
            if (Pacer::onVsync(s, 1'000'000'000 + c.periodNs * i, c.cap) > 0.0) drawn++;
        // 64 vsyncs: all 64 draw if never measured; about half (36) once a divide by 2 starts.
        check(c.measured ? (drawn > 30 && drawn < 40) : drawn == 64, c.what);
    }
}

// Export steps the engine by a fixed 1/fps (Renderer::beginFrame through clampDt) and live draws
// through the pacer; #436 asks for one dt rule. Red against: a clamp that bends an export rate
// (LoopSpec renders 24..60 fps; 10 and 15 fps are the slowest the rule promises to keep whole), or a
// live dt that differs from export's at the same frame rate.
void exportAndLiveStepTheSameDtAtTheSameRate() {
    struct Case {
        int fps;
        double hz;  // a panel the cap divides
        double dt;  // 1 / fps, by hand
    };
    const Case cases[] = {{60, 60, 0.0166666667}, {30, 60, 0.0333333333}, {24, 120, 0.0416666667},
                          {15, 60, 0.0666666667}, {10, 60, 0.1}};
    for (const auto& c : cases) {
        const double exportDt = clampDt(1.0 / c.fps);
        PacerState s;
        const auto drawn = drawnOf(feed(s, c.hz, static_cast<float>(c.fps), 400));
        char what[96];
        std::snprintf(what, sizeof what, "export at %d fps steps %.4f s, and live at %d fps does too", c.fps, c.dt, c.fps);
        check(near(exportDt, c.dt, 1e-9) && !drawn.empty() && near(drawn.back().dt, c.dt, 1e-6), what);
    }
}

// PRODUCTION BUG, expected to FAIL on #452 as written. The Saver cap asks Surface.setFrameRate(30),
// and a panel with a 30 Hz mode switches to it. Its 33.3 ms intervals fall outside the 4..22 ms
// window, so the estimate stays at the old 120 Hz period and the divide stays at 4: the surface
// draws every 4th 30 Hz vsync (7.5 fps), and each 133 ms interval is clamped to 100 ms, so the engine
// also runs at 75% speed. The header promises the estimate follows the panel when a setFrameRate hint
// changes its mode. The same holds for 60 Hz capped at 30 dropping to a 30 Hz mode (15 fps).
void aCapThatDropsThePanelTo30HzStillDraws30Fps() {
    PacerState s;
    const auto warm = feed(s, 120, 30.0f, 300);
    const auto drawn = drawnOf(feed(s, 30, 30.0f, 120, 0, 1, 0, warm.back().t + 33'333'333, 300));
    int tail = 0;
    for (const auto& k : drawn)
        if (k.vsync >= 360) tail++;
    check(tail == 60, "after the panel drops to 30 Hz under a 30 fps cap, every vsync draws (#452 draws 1 in 4)");
}

}  // namespace

int main() {
    divides();
    settlesBeforeDividing();
    firstFrameAndClamp();
    hitchesLeftOut();
    resumeKeepsPeriod();
    followsAModeChange();
    wireRoundTrip();
    dtRuleSpansTenToTwoFiftyFps();
    firstFrameOfARunIsOneSixtieth();
    longJitteredRunNeitherSkipsNorDoublesAndKeepsWallTime();
    capsOutsideThePanelStayWholeAndNeverFreeze();
    aCapChangeLandsOnTheNextVsync();
    aStallMidRunStepsOneClampedFrameThenResumesTheCadence();
    aResumeDrawsANominalFrameThenTheCapCadence();
    theDivideNeverOpensOnAnUnevenGap();
    panelsFrom4To22MsAreMeasured();
    exportAndLiveStepTheSameDtAtTheSameRate();
    aCapThatDropsThePanelTo30HzStillDraws30Fps();
    if (failures == 0) std::printf("pacer_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
