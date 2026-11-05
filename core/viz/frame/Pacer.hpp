#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// The frame clock every surface shares (ADR 0006, ADR 0008): which vsyncs a surface draws on, and
// the dt each drawn frame steps the engine by. Pure, with no GL and no Android, so the host can test
// it (core/tests/pacer_test.cpp).
//
// Android keeps only the Choreographer callback (render/FramePacer.kt): each vsync it hands
// `frameTimeNanos` and the surface's cap in, and draws when the answer is a dt above zero. The state
// is plain integers in a Kotlin-owned array, so stepping it allocates nothing and needs no handle to
// free (geode_pacer_on_vsync).
namespace geode::viz::frame {

// One dt rule for every clock (#436). Before, Kotlin clamped to 0.001..1/15 s and the renderer to
// 0.001..0.1 s, so the same stall stepped the pacer and the engine by different amounts. 0.1 s keeps
// the sound engine and the musical layer honest down to 10 fps. The field sims still clamp their own
// integrator step to 1/15 s, where the integrators were tuned.
inline constexpr double kMinDtSeconds = 0.001;
inline constexpr double kMaxDtSeconds = 0.1;
// The first frame of a run has no interval to measure. A nominal step beats zero, which freezes every
// envelope, and beats the clamp, which jumps the scene on resume.
inline constexpr double kFirstDtSeconds = 1.0 / 60.0;

inline double clampDt(double elapsedSeconds) { return std::clamp(elapsedSeconds, kMinDtSeconds, kMaxDtSeconds); }

// The cap a surface actually runs at (ADR 0008): its own (Settings › Performance, the wallpaper's or the
// dream's choice) under the thermal one (ThermalState::capFor). Either <= 0, or not a number, means none.
inline float effectiveCapFps(float surfaceCapFps, float thermalCapFps) {
    const bool surface = std::isfinite(surfaceCapFps) && surfaceCapFps > 0.0f;
    const bool thermal = std::isfinite(thermalCapFps) && thermalCapFps > 0.0f;
    if (surface && thermal) return std::min(surfaceCapFps, thermalCapFps);
    if (surface) return surfaceCapFps;
    return thermal ? thermalCapFps : 0.0f;
}

struct PacerState {
    int64_t lastDrawnNs = 0;
    int64_t lastVsyncNs = 0;
    int64_t periodNs = 0;  // smoothed vsync period, 0 until measured
    int64_t samples = 0;   // period samples taken, capped at kSettleFrames
    int64_t sinceDrawn = 0;
    int64_t divisor = 1;
    // The cap the last vsync paced to, in millihertz (0 = none). The shell compares it each vsync and
    // re-asks Surface.setFrameRate when the thermal cap moves it, without a second call across JNI.
    int64_t capMilliHz = 0;
};

// The wire layout for the ABI: seven int64s in this order.
inline constexpr int kPacerStateLongs = 7;
inline constexpr int kPacerCapSlot = 6;

class Pacer {
public:
    static constexpr int64_t kMinVsyncNs = 4'000'000;   // 250 Hz
    static constexpr int64_t kMaxVsyncNs = 22'000'000;  // 45 Hz
    static constexpr int64_t kSmoothing = 8;
    static constexpr int64_t kSettleFrames = 8;
    static constexpr double kDivisorEpsilon = 0.05;
    // The harshest sane case, a 24 fps cap on a 165 Hz panel, divides by 6, and 8 leaves room. Past
    // this the estimate is broken, and a fast visual is a better failure than a frozen one.
    static constexpr int64_t kMaxDivisor = 8;

    // A resume does not inherit the gap it was paused across. The period estimate survives: the panel
    // did not change while the surface was hidden.
    static void resume(PacerState& s) {
        s.lastDrawnNs = 0;
        s.lastVsyncNs = 0;
        s.sinceDrawn = 0;
    }

    // One vsync. `capFps` <= 0 means draw on every vsync (the panel's own rate). Returns the dt to draw
    // with, or 0 to skip this vsync.
    static double onVsync(PacerState& s, int64_t frameTimeNs, float capFps) {
        trackPeriod(s, frameTimeNs);
        s.capMilliHz = capFps > 0.0f && std::isfinite(capFps) ? std::llround(static_cast<double>(capFps) * 1000.0) : 0;
        s.divisor = divisorFor(s, capFps);
        if (++s.sinceDrawn < s.divisor) return 0.0;
        s.sinceDrawn = 0;
        const int64_t previous = s.lastDrawnNs;
        s.lastDrawnNs = frameTimeNs;
        if (previous == 0) return kFirstDtSeconds;
        return clampDt(static_cast<double>(frameTimeNs - previous) / 1e9);
    }

    // Draws are whole-number divides of the vsync cadence, so they stay evenly spaced. A fractional
    // divide (90 Hz to 60 by alternating 11 and 22 ms) averages out on paper and reads as judder, so a
    // cap that doesn't divide stays at the panel rate and leaves the rest to Surface.setFrameRate.
    static int64_t divisorFor(const PacerState& s, float capFps) {
        // Until the estimate settles, draw every vsync: guessing 2 on a 60 Hz panel would open on judder.
        if (capFps <= 0.0f || s.periodNs <= 0 || s.samples < kSettleFrames) return 1;
        const double panelHz = 1e9 / static_cast<double>(s.periodNs);
        // The epsilon absorbs noise: a 120 Hz panel measuring 119.6 Hz still divides by 2.
        const auto d = static_cast<int64_t>(panelHz / capFps + kDivisorEpsilon);
        return std::clamp<int64_t>(d, 1, kMaxDivisor);
    }

    static double panelHz(const PacerState& s) { return s.periodNs > 0 ? 1e9 / static_cast<double>(s.periodNs) : 0.0; }

private:
    // Callbacks come every vsync whether or not we drew, so the period is measured without a Display
    // handle, and follows the panel when a setFrameRate hint changes its mode.
    static void trackPeriod(PacerState& s, int64_t frameTimeNs) {
        const int64_t previous = s.lastVsyncNs;
        s.lastVsyncNs = frameTimeNs;
        if (previous == 0) return;
        const int64_t delta = frameTimeNs - previous;
        // Outside any real panel's range this is a pacer-thread hitch, not a period.
        if (delta < kMinVsyncNs || delta > kMaxVsyncNs) return;
        s.periodNs = s.samples == 0 ? delta : s.periodNs + (delta - s.periodNs) / kSmoothing;
        if (s.samples < kSettleFrames) s.samples++;
    }
};

// Wire helpers for the ABI.
inline PacerState unpack(const int64_t* w) { return PacerState{w[0], w[1], w[2], w[3], w[4], w[5], w[6]}; }
inline void pack(const PacerState& s, int64_t* w) {
    w[0] = s.lastDrawnNs;
    w[1] = s.lastVsyncNs;
    w[2] = s.periodNs;
    w[3] = s.samples;
    w[4] = s.sinceDrawn;
    w[5] = s.divisor;
    w[6] = s.capMilliHz;
}

}  // namespace geode::viz::frame
