#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>

// The frame clock every surface shares (ADR 0006, ADR 0008): which vsyncs a surface draws on, and
// the dt each drawn frame steps the engine by. Pure, with no GL and no Android, so the host can test
// it (core/tests/pacer_test.cpp).
//
// The stage's vsync thread (core/stage/StagePacing) hands each vsync's `frameTimeNanos` and the
// surface's cap in, and draws when the answer is a dt above zero. The state is plain integers, so
// stepping it allocates nothing.
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

struct PacerState {
    int64_t lastDrawnNs = 0;
    int64_t lastVsyncNs = 0;
    int64_t periodNs = 0;  // smoothed vsync period, 0 until measured
    int64_t samples = 0;   // confidence in periodNs: +1 per matching interval, -1 per stranger, 0..2*kSettleFrames
    int64_t sinceDrawn = 0;
    int64_t divisor = 1;
    // The cap the last vsync paced to, in millihertz (0 = none). The shell compares it each vsync and
    // re-asks Surface.setFrameRate when the cap moves, without a second call across JNI.
    int64_t capMilliHz = 0;
    // The rate the last vsync really paced to, in millihertz (0 = every vsync): the panel rate over the
    // divide, which is under the cap whenever the panel does not divide it (45 fps for a 60 fps cap on a
    // 90 Hz panel). The shell hands it to the renderer as the surface's paced rate, so the frame-time
    // monitors never read a rate the pacer chose as a phone that cannot keep up.
    int64_t pacedMilliHz = 0;
    // Settings › Performance's "Follow device" (frame::followCapFps in LiveFps.hpp): the panel rate it last
    // followed, in millihertz (0 = none yet).
    int64_t followedMilliHz = 0;
};

// The wire layout for the ABI: nine int64s in this order.
inline constexpr int kPacerStateLongs = 9;
inline constexpr int kPacerCapSlot = 6;
inline constexpr int kPacerPacedSlot = 7;
inline constexpr int kPacerFollowedSlot = 8;

class Pacer {
public:
    static constexpr int64_t kMinVsyncNs = 4'000'000;    // 250 Hz
    // 10 Hz, the slowest an LTPO panel paces an app at, and where the dt clamp stops anyway. It used
    // to be 45 Hz, which froze the estimate when the panel dropped to 30 or 24 Hz: a 120 Hz period
    // with a cap of 30 kept dividing by 4 and drew 7.5 fps on a 30 Hz panel (#452).
    static constexpr int64_t kMaxVsyncNs = 100'000'000;
    static constexpr int64_t kSmoothing = 8;
    static constexpr int64_t kSettleFrames = 8;
    static constexpr double kDivisorEpsilon = 0.05;
    // The harshest sane case, a 24 fps cap on a 165 Hz panel, divides by 7, and 8 leaves room. Past
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
        s.pacedMilliHz = pacedMilliHzFor(s);
        if (++s.sinceDrawn < s.divisor) return 0.0;
        s.sinceDrawn = 0;
        const int64_t previous = s.lastDrawnNs;
        s.lastDrawnNs = frameTimeNs;
        if (previous == 0) return kFirstDtSeconds;
        return clampDt(static_cast<double>(frameTimeNs - previous) / 1e9);
    }

    // Draws are whole-number divides of the vsync cadence, so they stay evenly spaced. A fractional
    // divide (90 Hz to 60 by alternating 11 and 22 ms) averages out on paper and reads as judder.
    //
    // The cap is a ceiling: the divide is the smallest whole number that brings the panel rate to the
    // cap or under it. A cap the panel does not divide (60 on a 90 Hz panel) so takes the next rate
    // down (45), not the panel rate. It used to stay at the panel rate and leave the rest to
    // Surface.setFrameRate, which is a hint the platform may ignore: on a 90 Hz phone a 60 fps cap
    // drew 90 frames a second, the GPU never idled and the phone reached thermal status 3. When the
    // platform does honour the hint the panel drops to 60 Hz, the estimate follows and the divide is 1.
    static int64_t divisorFor(const PacerState& s, float capFps) {
        // Until the estimate settles, draw every vsync: guessing 2 on a 60 Hz panel would open on judder.
        // Written as !(cap > 0) so a NaN cap means no cap as well.
        if (!(capFps > 0.0f) || s.periodNs <= 0 || s.samples < kSettleFrames) return 1;
        const double panelHz = 1e9 / static_cast<double>(s.periodNs);
        // The epsilon absorbs noise before rounding up: a 60 Hz panel measuring 60.4 Hz under a 60 cap
        // still draws every vsync, and a 120 Hz one measuring 119.6 or 120.4 Hz still divides by 2.
        // Clamped as a double before the cast, since a tiny cap's ratio would overflow int64_t (UB).
        const double ratio = panelHz / static_cast<double>(capFps) - kDivisorEpsilon;
        return static_cast<int64_t>(std::clamp(std::ceil(ratio), 1.0, static_cast<double>(kMaxDivisor)));
    }

    static double panelHz(const PacerState& s) { return s.periodNs > 0 ? 1e9 / static_cast<double>(s.periodNs) : 0.0; }

    // The rate the divide gives, never above the cap. In half-hertz steps, so the estimate's noise (a
    // 90 Hz panel measuring 89.7 to 90.3) does not move it: a frame-time monitor restarts its count
    // whenever its target changes. Until the estimate settles the cap is all there is to say.
    static int64_t pacedMilliHzFor(const PacerState& s) {
        if (s.capMilliHz <= 0) return 0;
        if (s.periodNs <= 0 || s.samples < kSettleFrames) return s.capMilliHz;
        const double hz = panelHz(s) / static_cast<double>(s.divisor);
        return std::min(s.capMilliHz, static_cast<int64_t>(std::llround(hz * 2.0)) * 500);
    }

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
        if (s.samples == 0) {
            s.periodNs = delta;
            s.samples = 1;
            return;
        }
        // Within a quarter of the estimate: noise, or a nearby mode (165 to 144 Hz) the average follows.
        if (std::abs(delta - s.periodNs) * 4 <= s.periodNs) {
            s.periodNs += (delta - s.periodNs) / kSmoothing;
            if (s.samples < 2 * kSettleFrames) s.samples++;
            return;
        }
        // A stranger: a dropped vsync, or the panel changed mode (120 to 60, 30 or 24 Hz). Each one
        // costs a sample, so a lone hitch leaves a settled divide alone, while a run of them unsettles
        // it (every vsync draws meanwhile) and at zero the next interval re-seeds the estimate.
        s.samples--;
    }
};

// Wire helpers for the ABI. Copied as bytes: the shell's array is jlong / long long, which is not the
// same type as int64_t (long on LP64), so reading it through int64_t would break strict aliasing.
static_assert(std::is_trivially_copyable_v<PacerState> && sizeof(PacerState) == kPacerStateLongs * sizeof(int64_t));
static_assert(kPacerCapSlot == 6 && kPacerPacedSlot == 7 && kPacerFollowedSlot == kPacerStateLongs - 1,
              "the cap and the paced rate keep their slots; the followed rate is the last");
// One slot at a time, each a memcpy of one int64_t, in the wire order.
inline int64_t loadSlot(const int64_t* w, int i) {
    int64_t v;
    std::memcpy(&v, w + i, sizeof v);
    return v;
}
inline void storeSlot(int64_t* w, int i, int64_t v) { std::memcpy(w + i, &v, sizeof v); }
inline PacerState unpack(const int64_t* w) {
    return PacerState{loadSlot(w, 0), loadSlot(w, 1), loadSlot(w, 2), loadSlot(w, 3),
                      loadSlot(w, 4), loadSlot(w, 5), loadSlot(w, kPacerCapSlot), loadSlot(w, kPacerPacedSlot),
                      loadSlot(w, kPacerFollowedSlot)};
}
inline void pack(const PacerState& s, int64_t* w) {
    storeSlot(w, 0, s.lastDrawnNs);
    storeSlot(w, 1, s.lastVsyncNs);
    storeSlot(w, 2, s.periodNs);
    storeSlot(w, 3, s.samples);
    storeSlot(w, 4, s.sinceDrawn);
    storeSlot(w, 5, s.divisor);
    storeSlot(w, kPacerCapSlot, s.capMilliHz);
    storeSlot(w, kPacerPacedSlot, s.pacedMilliHz);
    storeSlot(w, kPacerFollowedSlot, s.followedMilliHz);
}

}  // namespace geode::viz::frame
