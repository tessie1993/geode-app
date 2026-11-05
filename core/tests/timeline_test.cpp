// Host-side checks for keyframe evaluation, clip speed ramps and caption cues (core/signal/timeline: Keyframes,
// SpeedRamp, Cues) and their C ABI. Built and run by CI (.github/workflows/android.yml, "Native export planning
// tests"). The behaviour pinned is the Kotlin editor/Keyframes.kt (BezierCurve, Interpolation,
// KeyframeTrack.valueAt) and export/SpeedRamp.kt, which these replace on the export and preview paths.
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "api/geode_api.h"
#include "signal/timeline/Cues.hpp"
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

// The Kotlin TimedTextOverlay: forSpan's filter and re-base, and getText's first-match lookup from the
// first frame's timestamp.
// A frame's caption the way the overlay asks: item time from the clock, then the first cue that spans it.
int32_t live(const CueTable& table, CueClock& clock, int64_t presentationUs) {
    return table.at(clock.itemMs(presentationUs));
}

void cues() {
    // Project time: 0..4 s, 3..9 s (overlapping the first), 12..15 s.
    const int64_t starts[] = {0, 3'000, 12'000};
    const int64_t ends[] = {4'000, 9'000, 15'000};
    const CueTable item = CueTable::forSpan(starts, ends, 3, 2'000, 10'000);
    check(item.cues().size() == 2, "cues: only those touching the span");
    check(item.cues()[0].startMs == 0 && item.cues()[0].endMs == 2'000 && item.cues()[0].source == 0,
          "cues: one starting before the span is cut to its first frame");
    check(item.cues()[1].startMs == 1'000 && item.cues()[1].endMs == 7'000 && item.cues()[1].source == 1,
          "cues: re-based to the item");
    check(item.at(0) == 0 && item.at(1'500) == 0, "cues: the first that spans wins where two overlap");
    check(item.at(2'000) == 1 && item.at(6'999) == 1, "cues: the end is exclusive");
    check(item.at(7'000) == -1 && item.at(-1) == -1, "cues: blank where none spans");
    const CueTable tail = CueTable::forSpan(starts, ends, 3, 13'000, 14'000);
    check(tail.cues().size() == 1 && tail.cues()[0].startMs == 0 && tail.cues()[0].endMs == 1'000 &&
              tail.cues()[0].source == 2,
          "cues: one running past the span is cut to its length");
    check(CueTable::forSpan(starts, ends, 3, 9'000, 12'000).empty(), "cues: an end at the span start misses");
    check(CueTable::forSpan(starts, ends, 3, 10'000, 12'000).empty(), "cues: a start at the span end misses");
    check(CueTable::forSpan(nullptr, ends, 3, 0, 1'000).empty(), "cues: no starts");

    CueClock clock;
    check(live(item, clock, 5'000'000) == 0, "cues: the first frame is the origin");
    check(live(item, clock, 5'000'000 + 2'500'000) == 1, "cues: later frames from that origin");
    check(live(item, clock, 5'000'000 + 7'000'000) == -1, "cues: past the last cue");

    // The ABI is stateless: triples out, then the lookup with the caller's origin pair.
    int64_t triples[9] = {};
    check(geode_cues_for_span(starts, ends, 3, 2'000, 10'000, triples, 3) == 2, "ABI cues");
    check(triples[0] == 0 && triples[1] == 2'000 && triples[2] == 0 && triples[3] == 1'000 && triples[4] == 7'000 &&
              triples[5] == 1,
          "ABI cues: the triples match the table");
    int64_t origin[2] = {0, 0};
    check(geode_cues_at(triples, 2, origin, 1'000'000) == 0 && origin[0] == 1 && origin[1] == 1'000'000,
          "ABI cue lookup: the first frame fixes the caller's origin");
    check(geode_cues_at(triples, 2, origin, 1'000'000 + 3'000'000) == 1, "ABI cue lookup from that origin");
    check(geode_cues_for_span(starts, ends, 3, 2'000, 10'000, triples, 1) == 1, "ABI: cap bounds the triples");
    check(geode_cues_for_span(starts, ends, 3, 20'000, 30'000, triples, 3) == 0, "ABI: no cue in the span is 0");
    check(geode_cues_at(nullptr, 0, origin, 0) == -1 && geode_cues_at(triples, 2, nullptr, 0) == -1,
          "ABI: no cues or no clock is blank");
}

}  // namespace

int main() {
    bezier();
    tracks();
    speedRamp();
    cues();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("timeline_test: all passed\n");
    return 0;
}
