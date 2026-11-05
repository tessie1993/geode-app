// Host-side checks for keyframe evaluation (core/signal/timeline/Keyframes) and its C ABI. Built and run by CI
// (.github/workflows/android.yml, "Native export planning tests"). The behaviour pinned is the Kotlin
// editor/Keyframes.kt (BezierCurve, Interpolation, KeyframeTrack.valueAt). The clip speed ramps and caption
// cues it once checked went with video editing (#227).
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "api/geode_api.h"
#include "signal/timeline/Keyframes.hpp"

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

}  // namespace

int main() {
    bezier();
    tracks();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("timeline_test: all passed\n");
    return 0;
}
