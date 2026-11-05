// Host-side checks for keyframe evaluation and clip speed ramps (core/signal/timeline: Keyframes,
// SpeedRamp) and their C ABI. Built and run by CI (.github/workflows/android.yml, "Native export planning
// tests"). The behaviour pinned is the Kotlin editor/Keyframes.kt (BezierCurve, Interpolation,
// KeyframeTrack.valueAt) and export/SpeedRamp.kt, which these replace on the export and preview paths.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>

#include "api/geode_api.h"
#include "signal/timeline/Keyframes.hpp"
#include "signal/timeline/SpeedRamp.hpp"

using namespace geode::signal::timeline;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

Key scalarKey(int64_t at, float v, Interp interp = Interp::linear()) {
    Key k;
    k.atMs = at;
    k.value.kind = ValueKind::Scalar;
    k.value.v[0] = v;
    k.interp = interp;
    return k;
}

void bezier() {
    const BezierCurve straight(0.0f, 0.0f, 1.0f, 1.0f);
    check(near(straight.eval(0.3f), 0.3f, 1e-4f), "handles on the corners are a straight line");
    const BezierCurve in = easeCurve(Ease::In);
    const BezierCurve out = easeCurve(Ease::Out);
    const BezierCurve inOut = easeCurve(Ease::InOut);
    check(in.eval(0.0f) == 0.0f && near(in.eval(1.0f), 1.0f, 1e-5f), "ends are pinned");
    // CSS ease-in at 50 % is 0.3153; ease-out is its point reflection.
    check(near(in.eval(0.5f), 0.3153f, 0.002f), "ease-in at half time");
    check(near(out.eval(0.5f), 1.0f - in.eval(0.5f), 1e-4f), "ease-out mirrors ease-in");
    check(near(inOut.eval(0.5f), 0.5f, 1e-4f), "ease-in-out is symmetric");
    check(in.eval(-1.0f) == in.eval(0.0f) && in.eval(2.0f) == in.eval(1.0f), "x is clamped to 0..1");
    check(in.eval(NAN) == 0.0f, "NaN time reads as the start, not NaN");
    bool monotonic = true;
    const BezierCurve smooth = easeCurve(Ease::Smooth);
    float last = -1.0f;
    for (int i = 0; i <= 100; ++i) {
        const float y = smooth.eval(static_cast<float>(i) / 100.0f);
        if (y < last - 1e-4f) monotonic = false;
        last = y;
    }
    check(monotonic, "the smooth preset never runs backwards");
    const BezierCurve wild(-2.0f, 0.0f, 3.0f, 1.0f);
    check(near(wild.eval(0.5f), BezierCurve(0.0f, 0.0f, 1.0f, 1.0f).eval(0.5f), 1e-5f), "x handles are clamped to 0..1");
    check(near(geode_bezier_eval(0.42f, 0.0f, 0.58f, 1.0f, 0.5f), 0.5f, 1e-4f), "ABI bezier");
}

void tracks() {
    Track empty;
    Value v;
    check(!empty.valueAt(0, &v), "an empty track has no value");

    Track t;
    t.add(scalarKey(2000, 10.0f));
    t.add(scalarKey(1000, 0.0f));  // out of order: kept sorted
    check(t.keys().front().atMs == 1000, "keys are kept in time order");
    check(t.valueAt(500, &v) && v.v[0] == 0.0f, "before the first key holds it");
    check(t.valueAt(1500, &v) && near(v.v[0], 5.0f, 1e-5f), "halfway is a lerp");
    check(t.valueAt(2000, &v) && v.v[0] == 10.0f, "on the last key");
    check(t.valueAt(9000, &v) && v.v[0] == 10.0f, "after the last key holds it");
    t.add(scalarKey(1000, 4.0f));
    check(t.keys().size() == 2 && t.valueAt(0, &v) && v.v[0] == 4.0f, "a key on an existing instant replaces it");

    Track hold;
    hold.add(scalarKey(0, 1.0f, Interp::hold()));
    hold.add(scalarKey(1000, 9.0f));
    check(hold.valueAt(999, &v) && v.v[0] == 1.0f, "hold stays put");
    check(hold.valueAt(1000, &v) && v.v[0] == 9.0f, "and jumps on the next key");

    Track eased;
    eased.add(scalarKey(0, 0.0f, Interp::eased(Ease::In)));
    eased.add(scalarKey(1000, 1.0f));
    check(eased.valueAt(500, &v) && near(v.v[0], easeCurve(Ease::In).eval(0.5f), 1e-6f), "the leaving key's curve");

    Track colour;
    Key a;
    a.atMs = 0;
    a.value.kind = ValueKind::Colour;
    a.value.v[0] = 0.0f;
    a.value.v[3] = 1.0f;
    Key b = a;
    b.atMs = 100;
    b.value.v[0] = 1.0f;
    b.value.v[3] = 0.0f;
    colour.add(a);
    colour.add(b);
    check(colour.valueAt(50, &v) && near(v.v[0], 0.5f, 1e-6f) && near(v.v[3], 0.5f, 1e-6f), "colour blends every channel");

    Track toggle;
    Key on;
    on.atMs = 0;
    on.value.kind = ValueKind::Toggle;
    on.value.v[0] = 1.0f;
    Key off = on;
    off.atMs = 100;
    off.value.v[0] = 0.0f;
    toggle.add(on);
    toggle.add(off);
    check(toggle.valueAt(99, &v) && v.v[0] == 1.0f && v.kind == ValueKind::Toggle, "discrete values hold");

    Track mixed;
    mixed.add(scalarKey(0, 2.0f));
    Key vec;
    vec.atMs = 100;
    vec.value.kind = ValueKind::Vector2;
    vec.value.v[0] = 8.0f;
    mixed.add(vec);
    check(mixed.valueAt(50, &v) && v.v[0] == 2.0f, "mismatched kinds hold the earlier value");

    geode_track* abi = geode_track_create();
    const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float ten[4] = {10.0f, 0.0f, 0.0f, 0.0f};
    geode_track_add_key(abi, 0, 0, zero, 1, 0.0f, 0.0f, 0.0f, 0.0f);
    geode_track_add_key(abi, 1000, 0, ten, 1, 0.0f, 0.0f, 0.0f, 0.0f);
    int kind = -1;
    float out4[4] = {};
    check(geode_track_value_at(abi, 250, &kind, out4) == 1 && kind == 0 && near(out4[0], 2.5f, 1e-5f), "ABI track");
    geode_track_clear(abi);
    check(geode_track_value_at(abi, 250, &kind, out4) == 0, "ABI cleared track");
    geode_track_destroy(abi);
}

void speedRamp() {
    Track constant;
    constant.add(scalarKey(0, 2.0f));
    const SpeedRamp twice = SpeedRamp::fromTrack(constant, 0, 1000);
    check(twice.speeds().size() == 1 && twice.speedAt(123'456) == 2.0f, "one speed is one segment");
    check(twice.sourceSpanUs() == 2'000'000, "a second at 2x reads two seconds of source");
    check(twice.nextChangeUs(0) == -1, "no change after the only segment");

    Track step;
    step.add(scalarKey(0, 1.0f, Interp::hold()));
    step.add(scalarKey(500, 2.0f));
    const SpeedRamp ramp = SpeedRamp::fromTrack(step, 0, 1000);
    // Steps at 0..480 ms read 1x (13 steps), 520..960 ms read 2x (12 steps).
    check(ramp.startsUs().size() == 2 && ramp.startsUs()[1] == 520'000, "where the 2x segment starts in the source");
    check(ramp.sourceSpanUs() == 520'000 + 960'000, "integrated source span");
    check(ramp.speedAt(519'999) == 1.0f && ramp.speedAt(520'000) == 2.0f, "speed either side of the change");
    check(ramp.nextChangeUs(0) == 520'000 && ramp.nextChangeUs(520'000) == -1, "next change");
    check(ramp.speedAt(-5) == 1.0f, "before the clip reads the first segment");

    Track fast;
    fast.add(scalarKey(0, 10.0f));
    check(SpeedRamp::fromTrack(fast, 0, 100).speedAt(0) == SpeedRamp::kMaxSpeed, "speeds are clamped to 4x");
    check(SpeedRamp::fromTrack(Track{}, 0, 1000).empty(), "no keys, no ramp");

    // Clip-scoped keys sit on timeline time: a clip starting at 10 s reads the key at 10.5 s as its 500 ms.
    Track late;
    late.add(scalarKey(10'000, 1.0f, Interp::hold()));
    late.add(scalarKey(10'500, 2.0f));
    check(SpeedRamp::fromTrack(late, 10'000, 1000).startsUs()[1] == 520'000, "the clip's start offsets the track");

    check(sanitizeClipSpeed(NAN) == 1.0f && sanitizeClipSpeed(0.0f) == 1.0f && sanitizeClipSpeed(-2.0f) == 1.0f,
          "a broken saved speed is 1x");
    check(sanitizeClipSpeed(INFINITY) == 1.0f, "an infinite speed is 1x");
    check(sanitizeClipSpeed(0.1f) == 0.25f && sanitizeClipSpeed(10.0f) == 4.0f && sanitizeClipSpeed(1.5f) == 1.5f,
          "speeds are clamped to 0.25..4");

    geode_track* abi = geode_track_create();
    const float two[4] = {2.0f, 0.0f, 0.0f, 0.0f};
    geode_track_add_key(abi, 0, 0, two, 1, 0.0f, 0.0f, 0.0f, 0.0f);
    geode_speed_ramp* r = geode_speed_ramp_create(abi, 0, 1000, 0);
    check(r && geode_speed_ramp_source_span_us(r) == 2'000'000 && geode_speed_ramp_speed_at(r, 0) == 2.0f, "ABI ramp");
    int64_t startsOut[2] = {-1, -1};
    float speedsOut[2] = {0.0f, 0.0f};
    check(geode_speed_ramp_segments(r, startsOut, speedsOut, 2) == 1 && startsOut[0] == 0 && speedsOut[0] == 2.0f,
          "ABI ramp segments");
    geode_speed_ramp_destroy(r);
    geode_track_clear(abi);
    check(geode_speed_ramp_create(abi, 0, 1000, 40) == nullptr, "ABI: an empty track has no ramp");
    geode_track_destroy(abi);
}

// ---- Mutation-killing checks (#415 review). Each names the mutant(s) of Keyframes / SpeedRamp it kills. ----

Key keyOf(int64_t at, ValueKind kind, float v0, float v1 = 0.0f) {
    Key k;
    k.atMs = at;
    k.value.kind = kind;
    k.value.v[0] = v0;
    k.value.v[1] = v1;
    return k;
}

// Expected values are worked by hand from the Bernstein form y(s) = 3(1-s)^2 s c1y + 3(1-s) s^2 c2y + s^3,
// with the x handles at 1/3 and 2/3 so that x(s) = s.
void bezierBeyondThePresets() {
    // Kills: the y coefficients' c1y / c2y terms (yC = 3*c1y, yB = 3*(c2y - c1y) - yC, yA = 1 - yC - yB) mutated
    // in sign or operand: every preset has c1y = 0 and c2y = 1, so the checks above never saw those terms.
    const BezierCurve anticipate(1.0f / 3.0f, 1.2f, 2.0f / 3.0f, -0.2f);
    check(near(anticipate.eval(0.25f), 0.49375f, 1e-4f), "a custom curve reads its own y handles");
    // Kills: a clamp added to y, or yA / yB mixed up: a back-out handle overshoots the target value.
    const BezierCurve overshoot(1.0f / 3.0f, 0.0f, 2.0f / 3.0f, 1.6f);
    check(near(overshoot.eval(0.75f), 1.096875f, 1e-4f), "a handle above 1 overshoots the target, unclamped");

    // Kills: no bisection fallback (Newton's s returned always), the fallback test flipped (< to >=), the
    // bisection's direction (axisX(s) < x to >) or its halves swapped. x handles (1, 0) flatten x(s) at s = 1/2:
    // Newton cannot move off the zero slope there, so only bisection finds the instant. x(s) = 4(s - 1/2)^3 + 1/2
    // and y(s) = s, so eval(x) = 1/2 + cbrt((x - 1/2) / 4).
    const BezierCurve flat(1.0f, 1.0f / 3.0f, 0.0f, 2.0f / 3.0f);
    check(near(flat.eval(0.5001f), 0.52924f, 1e-3f), "a flat spot in time still finds the right instant (after)");
    check(near(flat.eval(0.4999f), 0.47076f, 1e-3f), "a flat spot in time still finds the right instant (before)");

    // Kills: the Smooth preset's handles (0.4, 0, 0.2, 1) mutated; before, only its monotonicity was pinned.
    // Smooth is cubic-bezier(0.4, 0, 0.2, 1), Material's fast-out-slow-in; values solved by bisection in double.
    const BezierCurve smooth = easeCurve(Ease::Smooth);
    check(near(smooth.eval(0.25f), 0.23659f, 1e-3f), "smooth at a quarter");
    check(near(smooth.eval(0.5f), 0.77556f, 1e-3f), "smooth at half time");
    check(near(smooth.eval(0.75f), 0.95937f, 1e-3f), "smooth at three quarters");

    // Kills: Interp::fraction's Linear clamp removed (reachable through the public Interp, not through a Track).
    check(Interp::linear().fraction(1.5f) == 1.0f && Interp::linear().fraction(-0.5f) == 0.0f,
          "linear progress stays inside the two keys");
    check(Interp::linear().fraction(NAN) == 0.0f, "linear NaN progress reads as the leaving key");
    // Kills: Hold returning the progress or 1.
    check(Interp::hold().fraction(0.9f) == 0.0f, "hold never leaves its key");
}

void tracksBetweenKeys() {
    Value v;
    // Kills: lerp's t replaced by 1 - t, and progress measured from the far key (to.atMs - timeMs): both are
    // invisible at the midpoint every check above used. Keys on the line v = t / 1000 must read that line.
    Track line;
    line.add(scalarKey(1000, 1.0f));
    line.add(scalarKey(5000, 5.0f));
    check(line.valueAt(2000, &v) && near(v.v[0], 2.0f, 1e-5f), "a quarter of the way is a quarter of the value");
    check(line.valueAt(4500, &v) && near(v.v[0], 4.5f, 1e-5f), "seven eighths of the way");
    // Kills: indexAtOrBefore off by one at the first key.
    check(line.valueAt(1000, &v) && v.v[0] == 1.0f, "exactly on the first key");
    check(line.valueAt(999, &v) && v.v[0] == 1.0f, "a millisecond before the first key holds it");

    // Kills: Value::components(Vector2) returning 1 (y left at the leaving key).
    Track vec;
    vec.add(keyOf(0, ValueKind::Vector2, 0.0f, 10.0f));
    vec.add(keyOf(1000, ValueKind::Vector2, 4.0f, 30.0f));
    check(vec.valueAt(250, &v) && v.kind == ValueKind::Vector2 && near(v.v[0], 1.0f, 1e-5f) && near(v.v[1], 15.0f, 1e-5f),
          "a Vector2 blends both axes");

    // Kills: the Choice case dropped from blend's discrete list (a choice index would lerp to 0.9 * 3).
    Track choice;
    choice.add(keyOf(0, ValueKind::Choice, 0.0f));
    choice.add(keyOf(1000, ValueKind::Choice, 3.0f));
    check(choice.valueAt(900, &v) && v.kind == ValueKind::Choice && v.v[0] == 0.0f, "a choice holds until its next key");
    check(choice.valueAt(1000, &v) && v.v[0] == 3.0f, "and switches on it");

    // Kills: Track::add's ordering (append, lower_bound with <=) and replace-on-same-instant, plus any blend
    // mutant, over many shuffled inserts (seeded, raw mt19937 output): keys on v = t / 1000 read the line.
    std::mt19937 rng(415u);
    for (int trial = 0; trial < 50; ++trial) {
        Track t;
        const int n = 2 + static_cast<int>(rng() % 30u);
        for (int i = 0; i < n; ++i) {
            const int64_t at = static_cast<int64_t>(rng() % 100'001u);
            t.add(scalarKey(at, static_cast<float>(at) / 1000.0f));
            if (rng() % 4u == 0u) t.add(scalarKey(at, static_cast<float>(at) / 1000.0f));  // the same instant again
        }
        bool ordered = true;
        for (size_t i = 1; i < t.keys().size(); ++i) ordered = ordered && t.keys()[i - 1].atMs < t.keys()[i].atMs;
        check(ordered, "shuffled keys end up strictly in time order, one per instant");
        const int64_t first = t.keys().front().atMs;
        const int64_t last = t.keys().back().atMs;
        for (int q = 0; q < 20; ++q) {
            const int64_t at = static_cast<int64_t>(rng() % 100'001u);
            const float want = static_cast<float>(std::clamp(at, first, last)) / 1000.0f;
            if (!(t.valueAt(at, &v) && near(v.v[0], want, 2e-3f))) {
                check(false, "collinear keys read their line wherever the playhead is");
                break;
            }
        }
    }
    // Kills: a later key on an instant failing to replace the earlier one once other keys went in between.
    Track again;
    again.add(scalarKey(500, 1.0f));
    again.add(scalarKey(0, 0.0f));
    again.add(scalarKey(1000, 2.0f));
    again.add(scalarKey(500, 7.0f));
    check(again.keys().size() == 3 && again.valueAt(500, &v) && v.v[0] == 7.0f, "the last key set on an instant wins");

    // Kills: the ABI reading interp 2 as linear (handles ignored) and kindOf dropping Choice (4 -> scalar).
    geode_track* abi = geode_track_create();
    const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float one[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    geode_track_add_key(abi, 0, 0, zero, 2, 0.42f, 0.0f, 1.0f, 1.0f);  // CSS ease-in: 0.3153 at half time
    geode_track_add_key(abi, 1000, 0, one, 1, 0.0f, 0.0f, 0.0f, 0.0f);
    int kind = -1;
    float out4[4] = {};
    check(geode_track_value_at(abi, 500, &kind, out4) == 1 && near(out4[0], 0.3153f, 0.002f), "ABI bezier key eases");
    geode_track_clear(abi);
    const float three[4] = {3.0f, 0.0f, 0.0f, 0.0f};
    geode_track_add_key(abi, 0, 4, zero, 1, 0.0f, 0.0f, 0.0f, 0.0f);
    geode_track_add_key(abi, 1000, 4, three, 1, 0.0f, 0.0f, 0.0f, 0.0f);
    check(geode_track_value_at(abi, 900, &kind, out4) == 1 && kind == 4 && out4[0] == 0.0f, "ABI choice key holds");
    geode_track_destroy(abi);
    check(geode_track_value_at(nullptr, 0, &kind, out4) == 0, "ABI: no track, no value");
}

void speedRampsOnTrackTime() {
    Track twice;
    twice.add(scalarKey(0, 2.0f));
    // Kills: the last step not shortened to what is left of the clip (min(step, duration - outMs) -> step).
    // 1.01 s of output at 2x reads 2.02 s of source.
    check(SpeedRamp::fromTrack(twice, 0, 1010).sourceSpanUs() == 2'020'000, "a clip that is not a whole number of steps");
    // Kills: the source clock held in 32 bits, or ms and µs mixed up: three hours at 1x is three hours of source.
    Track unity;
    unity.add(scalarKey(0, 1.0f));
    check(SpeedRamp::fromTrack(unity, 0, 3LL * 3600 * 1000).sourceSpanUs() == 3LL * 3600 * 1000 * 1000,
          "three hours of clip at 1x is three hours of source, in microseconds");
    // Kills: a zero-length clip still sampling a step.
    const SpeedRamp none = SpeedRamp::fromTrack(twice, 0, 0);
    check(none.empty() && none.sourceSpanUs() == 0 && none.speedAt(0) == 1.0f, "a clip of no length has no ramp");

    // Kills: the sampling loop running to outMs <= duration (an extra zero-length step reading the key on the
    // clip's last instant, adding a segment no source time ever plays).
    Track endKey;
    endKey.add(scalarKey(0, 1.0f, Interp::hold()));
    endKey.add(scalarKey(1000, 2.0f));
    const SpeedRamp toEnd = SpeedRamp::fromTrack(endKey, 0, 1000);
    check(toEnd.speeds().size() == 1 && toEnd.sourceSpanUs() == 1'000'000, "a key on the clip's last instant plays no part");

    // Kills: speedOf sampling clip-relative time instead of timeline time (clipStartMs dropped or subtracted).
    Track keys;
    keys.add(scalarKey(2000, 0.5f, Interp::hold()));
    keys.add(scalarKey(5000, 3.0f));
    check(SpeedRamp::fromTrack(keys, 6000, 500).sourceSpanUs() == 1'500'000, "a clip after the last key reads it");
    check(SpeedRamp::fromTrack(keys, 0, 1000).sourceSpanUs() == 500'000, "a clip before the first key reads it");
    // Adjacent clips on one track meet on the key: 4..5 s at 0.5x, then 5..6 s at 3x, neither borrowing a step.
    const SpeedRamp a = SpeedRamp::fromTrack(keys, 4000, 1000);
    const SpeedRamp b = SpeedRamp::fromTrack(keys, 5000, 1000);
    check(a.sourceSpanUs() == 500'000 && b.sourceSpanUs() == 3'000'000 && a.speeds().size() == 1 && b.speeds().size() == 1,
          "adjacent clips split on the key");

    // Kills: the non-scalar guard and the NaN guard in speedOf (std::clamp hands NaN back: cppreference).
    Track vec;
    vec.add(keyOf(0, ValueKind::Vector2, 3.0f, 3.0f));
    check(SpeedRamp::fromTrack(vec, 0, 1000).sourceSpanUs() == 1'000'000, "a non-scalar speed key reads 1x");
    Track nanKey;
    nanKey.add(scalarKey(0, NAN));
    const SpeedRamp nanRamp = SpeedRamp::fromTrack(nanKey, 0, 1000);
    check(nanRamp.speedAt(0) == 1.0f && nanRamp.sourceSpanUs() == 1'000'000, "a NaN speed key reads 1x");
    // Kills: the 0.25x floor dropped from the ramp's sampling (the 4x ceiling is pinned above).
    Track slow;
    slow.add(scalarKey(0, 0.1f));
    check(SpeedRamp::fromTrack(slow, 0, 1000).sourceSpanUs() == 250'000, "speeds are floored at 0.25x");

    // Kills: kSameSpeed's tolerance dropped (> 0) or widened past a real change.
    Track drift;
    drift.add(scalarKey(0, 1.0f));
    drift.add(scalarKey(1000, 1.00005f));
    check(SpeedRamp::fromTrack(drift, 0, 1000).speeds().size() == 1, "a drift under 1e-4 is one segment");
    Track ramp;
    ramp.add(scalarKey(0, 1.0f));
    ramp.add(scalarKey(1000, 3.0f));
    const SpeedRamp up = SpeedRamp::fromTrack(ramp, 0, 1000);
    // 25 steps of 40 ms read at 1 + 2t: 40 ms * (25 + 0.002 * (0 + 40 + ... + 960)) = 1960 ms, each step truncated.
    check(up.speeds().size() == 25 && up.sourceSpanUs() <= 1'960'000 && up.sourceSpanUs() >= 1'960'000 - 25,
          "a linear ramp integrates step by step");
    bool rising = true;
    for (size_t i = 1; i < up.startsUs().size(); ++i) {
        rising = rising && up.startsUs()[i] > up.startsUs()[i - 1] && up.speeds()[i] > up.speeds()[i - 1];
    }
    check(rising && up.startsUs().front() == 0, "segments start at 0 and climb");
    // Every source instant reads the segment it falls in, and its next change is that segment's end.
    for (size_t i = 0; i + 1 < up.startsUs().size(); ++i) {
        const int64_t mid = (up.startsUs()[i] + up.startsUs()[i + 1]) / 2;
        if (up.speedAt(mid) != up.speeds()[i] || up.nextChangeUs(mid) != up.startsUs()[i + 1]) {
            check(false, "a source instant reads its own segment and the next change after it");
            break;
        }
    }

    // ABI: step 0 means the 40 ms default (kills step_ms > 0 relaxed to >= 0, or step_ms ignored).
    geode_track* abi = geode_track_create();
    const float one[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    const float two[4] = {2.0f, 0.0f, 0.0f, 0.0f};
    geode_track_add_key(abi, 0, 0, one, 0, 0.0f, 0.0f, 0.0f, 0.0f);
    geode_track_add_key(abi, 500, 0, two, 1, 0.0f, 0.0f, 0.0f, 0.0f);
    geode_speed_ramp* coarse = geode_speed_ramp_create(abi, 0, 1000, 0);
    geode_speed_ramp* fine = geode_speed_ramp_create(abi, 0, 1000, 10);
    check(coarse && geode_speed_ramp_next_change_us(coarse, 0) == 520'000, "ABI: step 0 samples every 40 ms");
    check(fine && geode_speed_ramp_next_change_us(fine, 0) == 500'000, "ABI: a 10 ms step finds the change at 500 ms");
    // Kills: the copy ignoring capacity (writing past it), or the count shrinking to what was copied.
    int64_t starts[3] = {-7, -7, -7};
    float speeds[3] = {-7.0f, -7.0f, -7.0f};
    check(geode_speed_ramp_segments(coarse, starts, speeds, 1) == 2 && starts[0] == 0 && speeds[0] == 1.0f &&
              starts[1] == -7 && speeds[1] == -7.0f,
          "ABI segments copy no more than asked and still count them all");
    check(geode_speed_ramp_segments(coarse, starts, speeds, -3) == 2 && starts[1] == -7, "ABI: a negative capacity copies none");
    geode_speed_ramp_destroy(coarse);
    geode_speed_ramp_destroy(fine);
    geode_track_destroy(abi);
    check(geode_speed_ramp_speed_at(nullptr, 0) == 1.0f && geode_speed_ramp_next_change_us(nullptr, 0) == -1 &&
              geode_speed_ramp_source_span_us(nullptr) == 0 && geode_speed_ramp_segments(nullptr, starts, speeds, 3) == 0,
          "ABI: no ramp plays at 1x with no changes");
    // A denormal saved speed is a real, tiny speed: the 0.25x floor, not the broken-speed 1x.
    check(sanitizeClipSpeed(1e-40f) == 0.25f && geode_clip_speed_sanitize(4.0f) == 4.0f &&
              geode_clip_speed_sanitize(0.25f) == 0.25f,
          "the clamp keeps its ends and a tiny speed is the slowest");
}

}  // namespace

int main() {
    bezier();
    tracks();
    speedRamp();
    bezierBeyondThePresets();
    tracksBetweenKeys();
    speedRampsOnTrackTime();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("timeline_test: all passed\n");
    return 0;
}
