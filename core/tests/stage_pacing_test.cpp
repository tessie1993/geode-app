// Host-side checks for core/stage/StagePacing: one stage's pacer on the vsync thread (docs/agents/native-stage.md).
//
// What is pinned: driven through StagePacing, every panel (60, 90, 120 Hz) and cap (free, 30, 60, 90, 120, Follow
// device) under every thermal step paces exactly as the bare Pacer stepped by geode_pacer_on_vsync's rule does,
// vsync for vsync; the numbers pacer_test expects (120 Hz capped at 60 divides by 2, 90 Hz capped at 60 draws 45,
// 90 Hz capped at 30 divides by 3); a callback that re-posts while running and lapses once stopped; two starts
// posting once; a stop and a start between two vsyncs posting nothing new; a resume that forgets the paused gap;
// the paced rate and its fallback to the cap; and the frame-rate hint restated on the first vsync, on a new cap
// and on a thermal step, 0 for Follow device.
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "stage/StagePacing.hpp"

using namespace geode::stage;
namespace frame = geode::viz::frame;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

int64_t at(double hz, int i, int64_t t0 = 1'000'000'000) { return t0 + static_cast<int64_t>(std::llround(1e9 / hz * i)); }

// The pacer as the Kotlin pacer drove it through geode_pacer_on_vsync: the thermal step applied to the cap, then
// Pacer::onVsync on the caller's state.
double reference(frame::PacerState& s, int64_t t, float cap, int rungsDown) {
    const float paced = frame::pacerCapFps(s, cap, rungsDown);
    return frame::Pacer::onVsync(s, t, paced);
}

void sameAsThePacer() {
    const double panels[] = {60.0, 90.0, 120.0};
    const float caps[] = {0.0f, 30.0f, 60.0f, 90.0f, 120.0f, static_cast<float>(frame::kLiveFpsFollow)};
    int mismatches = 0;
    for (const double hz : panels) {
        for (const float cap : caps) {
            for (int rungs = 0; rungs <= 3; ++rungs) {
                StagePacing p(cap);
                frame::PacerState ref;
                p.start();
                for (int i = 0; i < 600; ++i) {
                    const int64_t t = at(hz, i);
                    if (!p.vsyncArrived()) {
                        ++mismatches;
                        break;
                    }
                    const StagePacing::Tick tick = p.step(t, rungs);
                    const double want = reference(ref, t, cap, rungs);
                    if (tick.dt != want || tick.draw != (want > 0.0)) ++mismatches;
                    if (p.state().divisor != ref.divisor || p.state().pacedMilliHz != ref.pacedMilliHz) ++mismatches;
                }
            }
        }
    }
    check(mismatches == 0, "every panel, cap and thermal step paces vsync for vsync as the bare pacer does");
}

void knownNumbers() {
    struct Case {
        double hz;
        float cap;
        int64_t divisor;
        int64_t pacedMilliHz;
    };
    const Case cases[] = {{60, 60, 1, 60000}, {90, 60, 2, 45000}, {90, 30, 3, 30000}, {120, 60, 2, 60000}, {120, 30, 4, 30000}, {120, 120, 1, 120000}};
    for (const Case& c : cases) {
        StagePacing p(c.cap);
        p.start();
        double lastDt = 0.0;
        for (int i = 0; i < 600; ++i) {
            p.vsyncArrived();
            const StagePacing::Tick tick = p.step(at(c.hz, i), 0);
            if (tick.draw) lastDt = tick.dt;
        }
        char what[112];
        std::snprintf(what, sizeof what, "%.0f Hz capped at %.0f divides by %lld", c.hz, c.cap, static_cast<long long>(c.divisor));
        check(p.state().divisor == c.divisor, what);
        std::snprintf(what, sizeof what, "%.0f Hz capped at %.0f draws dt = divisor periods", c.hz, c.cap);
        check(near(lastDt, static_cast<double>(c.divisor) / c.hz, 2e-4), what);
        std::snprintf(what, sizeof what, "%.0f Hz capped at %.0f hands the frame %.1f fps", c.hz, c.cap, static_cast<double>(c.pacedMilliHz) / 1000.0);
        check(near(p.fps(), static_cast<double>(c.pacedMilliHz) / 1000.0, 1e-3), what);
    }
    // A hot phone steps a 120 cap to 90: the hint asks the panel for 90 and a 120 Hz panel draws every 2nd vsync.
    StagePacing hot(120.0f);
    hot.start();
    for (int i = 0; i < 600; ++i) {
        hot.vsyncArrived();
        hot.step(at(120, i), 1);
    }
    check(hot.state().divisor == 2 && near(StagePacing::frameRateFor(120.0f, 1), 90.0, 1e-6), "120 Hz under one thermal step paces as 90 asks");
}

void startAndStop() {
    StagePacing p(60.0f);
    check(!p.running() && !p.posted(), "a new pacer waits for its start");
    check(p.start(), "the first start posts a callback");
    check(!p.start(), "a second start posts nothing: two callbacks would halve every dt");
    check(p.vsyncArrived(), "a running pacer's callback re-posts");
    p.step(at(60, 0), 0);
    p.stop();
    check(!p.vsyncArrived() && !p.posted(), "a stopped pacer's callback lapses and nothing is pending");
    check(p.start(), "a start after the lapse posts again");

    // Stop and start between two vsyncs: the pending callback carries on, nothing new is posted.
    p.stop();
    check(!p.start(), "a start while the old callback is pending posts nothing");
    check(p.vsyncArrived(), "the pending callback re-posts for the restarted pacer");
}

void resumeForgetsTheGap() {
    StagePacing p(60.0f);
    p.start();
    for (int i = 0; i < 200; ++i) {
        p.vsyncArrived();
        p.step(at(60, i), 0);
    }
    const int64_t period = p.state().periodNs;
    p.stop();
    p.vsyncArrived();
    // Ten minutes later.
    p.start();
    p.vsyncArrived();
    const StagePacing::Tick first = p.step(at(60, 0, 700'000'000'000), 0);
    check(first.draw && near(first.dt, frame::kFirstDtSeconds, 1e-12), "the first frame after a resume is nominal, not a 600 s step");
    check(p.state().periodNs == period, "and the panel's period survives the pause");
}

void pacedRate() {
    StagePacing p(60.0f);
    check(near(p.fps(), 60.0, 1e-6) && near(p.capFps(), 60.0, 1e-6), "before a vsync the frame is told the cap");
    p.start();
    for (int i = 0; i < 300; ++i) {
        p.vsyncArrived();
        p.step(at(90, i), 0);
    }
    check(near(p.fps(), 45.0, 1e-3), "after it, the rate the pacer really draws at");
    p.setCap(30.0f);
    check(near(p.fps(), 30.0, 1e-6), "a new cap answers at once, until a vsync says");
    p.vsyncArrived();
    p.step(at(90, 300), 0);
    check(near(p.fps(), 30.0, 1e-3), "and the next vsync paces to it");
    StagePacing uncapped(0.0f);
    uncapped.start();
    uncapped.vsyncArrived();
    uncapped.step(at(120, 0), 0);
    check(uncapped.fps() == 0.0f, "a free-running surface is told 0, every vsync");
}

void frameRateHint() {
    StagePacing p(60.0f);
    p.start();
    p.vsyncArrived();
    StagePacing::Tick t = p.step(at(120, 0), 0);
    check(t.restateRate && near(t.rateFps, 60.0, 1e-6), "the first vsync states the cap on the window");
    p.vsyncArrived();
    t = p.step(at(120, 1), 0);
    check(!t.restateRate, "an unchanged cap is not restated");
    p.vsyncArrived();
    t = p.step(at(120, 2), 3);
    check(t.restateRate && near(t.rateFps, 30.0, 1e-6), "a thermal step restates the stepped cap");
    p.setCap(120.0f);
    p.vsyncArrived();
    t = p.step(at(120, 3), 0);
    check(t.restateRate && near(t.rateFps, 120.0, 1e-6), "a new cap is restated");
    check(StagePacing::frameRateFor(static_cast<float>(frame::kLiveFpsFollow), 0) == 0.0f, "Follow device states no rate");
    check(StagePacing::frameRateFor(static_cast<float>(frame::kLiveFpsFollow), 2) == 0.0f, "even hot");
    check(StagePacing::frameRateFor(0.0f, 0) == 0.0f, "a free-running cool surface states no rate");
    check(StagePacing::frameRateFor(0.0f, 1) == frame::kThermalFloorFps, "a free-running warm one the floor");
    check(StagePacing::frameRateFor(15.0f, 3) == 15.0f, "a cap under the floor stays");
}

}  // namespace

int main() {
    sameAsThePacer();
    knownNumbers();
    startAndStop();
    resumeForgetsTheGap();
    pacedRate();
    frameRateHint();
    if (failures) {
        std::fprintf(stderr, "%d stage pacing check(s) failed\n", failures);
        return 1;
    }
    std::printf("stage pacing: all checks passed\n");
    return 0;
}
