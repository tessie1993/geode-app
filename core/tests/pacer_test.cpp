// Host-side checks for core/viz/frame/Pacer (#436): the frame clock every surface shares. Built and
// run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: whole-number divides of the panel rate (60/90/120 Hz panels capped at 30 and 60),
// noise tolerance (a 119.6 Hz panel still halves), no divide until the estimate settles, hitches
// left out of the estimate, a resume that forgets the gap but keeps the period, a panel that drops
// below 45 Hz (#452), a NaN or tiny cap (#452), and the one dt clamp the renderer shares.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

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
    check(s.periodNs == period && s.samples == 2 * Pacer::kSettleFrames, "a resume keeps the panel's period");
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

// #452: the band used to stop at 45 Hz, so a panel that dropped to 30 or 24 Hz froze a 120 Hz estimate
// and the old divide drew a quarter or a fifth of what was asked.
void followsADropBelow45Hz() {
    struct Case {
        double hz;
        float cap;
        int64_t divisor;
    };
    const Case cases[] = {{30, 30, 1}, {24, 24, 1}, {30, 60, 1}, {10, 30, 1}, {30, 15, 2}};
    for (const auto& c : cases) {
        PacerState s;
        runPanel(s, 120, c.cap, 300);
        const int64_t t0 = s.lastVsyncNs + std::llround(1e9 / c.hz);
        const Run r = runPanel(s, c.hz, c.cap, 120, t0);
        char what[96];
        std::snprintf(what, sizeof what, "120 Hz dropping to %.0f Hz, capped at %.0f, divides by %lld", c.hz, c.cap, static_cast<long long>(c.divisor));
        check(s.divisor == c.divisor, what);
        std::snprintf(what, sizeof what, "and the estimate follows to %.0f Hz", c.hz);
        check(near(Pacer::panelHz(s), c.hz, 0.5), what);
        std::snprintf(what, sizeof what, "and draws dt = divisor periods at %.0f Hz", c.hz);
        check(near(r.lastDt, std::min(static_cast<double>(c.divisor) / c.hz, kMaxDtSeconds), 2e-4), what);
    }
}

void loneSlowIntervalKeepsTheDivide() {
    PacerState s;
    runPanel(s, 120, 60.0f, 300);
    const int64_t settled = s.periodNs;
    // One 33 ms interval (a pacer-thread hitch, now inside the band) moves nothing.
    Pacer::onVsync(s, s.lastVsyncNs + 33'333'333, 60.0f);
    check(s.periodNs == settled, "a lone 33 ms interval is not a 30 Hz panel");
    check(s.divisor == 2, "and the 120 Hz divide holds");
}

void nanAndTinyCaps() {
    PacerState s;
    runPanel(s, 120, 60.0f, 300);
    Pacer::onVsync(s, s.lastVsyncNs + 8'333'333, std::numeric_limits<float>::quiet_NaN());
    check(s.divisor == 1, "a NaN cap means no cap, never a float-to-int cast of NaN");
    Pacer::onVsync(s, s.lastVsyncNs + 8'333'333, 1e-30f);
    check(s.divisor == Pacer::kMaxDivisor, "a tiny cap clamps before the cast instead of overflowing");
    Pacer::onVsync(s, s.lastVsyncNs + 8'333'333, std::numeric_limits<float>::infinity());
    check(s.divisor == 1, "an infinite cap draws every vsync");
}

void wireRoundTrip() {
    PacerState s{1, 2, 3, 4, 5, 6};
    int64_t wire[kPacerStateLongs] = {};
    pack(s, wire);
    const PacerState back = unpack(wire);
    check(back.lastDrawnNs == 1 && back.lastVsyncNs == 2 && back.periodNs == 3 && back.samples == 4 && back.sinceDrawn == 5 && back.divisor == 6,
          "the six-long wire layout round-trips");
}

}  // namespace

int main() {
    divides();
    settlesBeforeDividing();
    firstFrameAndClamp();
    hitchesLeftOut();
    resumeKeepsPeriod();
    followsAModeChange();
    followsADropBelow45Hz();
    loneSlowIntervalKeepsTheDivide();
    nanAndTinyCaps();
    wireRoundTrip();
    if (failures == 0) std::printf("pacer_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
