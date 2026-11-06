#pragma once
#include <cstdint>
#include <vector>

namespace geode::signal::timeline {

// Keyframe evaluation on programme time (ADR 0006, core/signal/timeline). The project document (adding,
// moving and rippling keys, `editor/Keyframes.kt`) stays Kotlin; what a track is worth at time t is
// answered here, once, for the Studio preview, the scene export and the Media3 clip effects alike. Today
// that evaluation runs in Kotlin on the export thread every frame, in two places that can disagree.
//
// Ported from Kotlin's BezierCurve, Interpolation and KeyframeTrack.valueAt with the same float maths, so a
// project animates the same after the port (core/tests/timeline_test).

// A cubic bezier with its ends pinned at (0,0) and (1,1): the curve editor's handles. eval(x) solves for
// the curve parameter at x (Newton, then bisection when Newton misses) and reads y there: the handles bend
// time as well as value. The x handles are clamped to 0..1 so the curve stays monotonic in time.
class BezierCurve {
public:
    BezierCurve() : BezierCurve(0.0f, 0.0f, 1.0f, 1.0f) {}
    BezierCurve(float c1x, float c1y, float c2x, float c2y);

    float eval(float x) const;

private:
    float axisX(float s) const { return ((xA_ * s + xB_) * s + xC_) * s; }
    float axisY(float s) const { return ((yA_ * s + yB_) * s + yC_) * s; }
    float slopeX(float s) const { return (3.0f * xA_ * s + 2.0f * xB_) * s + xC_; }
    float solveForX(float x) const;
    float bisectForX(float x) const;

    float xA_, xB_, xC_, yA_, yB_, yC_;
};

// The curve editor's named presets (Kotlin EaseShape), each an ordinary bezier.
enum class Ease : int { In = 0, Out = 1, InOut = 2, Smooth = 3 };
BezierCurve easeCurve(Ease shape);

// How the value leaves a key on its way to the next one.
enum class InterpKind : int { Hold = 0, Linear = 1, Curve = 2 };

struct Interp {
    InterpKind kind = InterpKind::Linear;
    BezierCurve curve;  // read only for Curve (an Ease preset or a custom handle pair)

    static Interp hold() { return {InterpKind::Hold, {}}; }
    static Interp linear() { return {InterpKind::Linear, {}}; }
    static Interp eased(Ease shape) { return {InterpKind::Curve, easeCurve(shape)}; }
    static Interp custom(BezierCurve c) { return {InterpKind::Curve, c}; }

    // The eased 0..1 position between two keys at raw progress 0..1.
    float fraction(float progress) const;
};

// Kotlin ParamValue's shapes. Scalar, Vector2 and Colour blend component-wise; Toggle and Choice are
// discrete and hold until their own next key (their value travels in v[0]).
enum class ValueKind : int { Scalar = 0, Vector2 = 1, Colour = 2, Toggle = 3, Choice = 4 };

struct Value {
    ValueKind kind = ValueKind::Scalar;
    float v[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    static int components(ValueKind kind);
};

struct Key {
    int64_t atMs = 0;
    Value value;
    Interp interp;
};

// The animation of one parameter. Keys are kept in time order and unique by time: a key added at an
// instant that already has one replaces it, as KeyframeTrack.withKey does.
class Track {
public:
    void add(const Key& key);
    void clear() { keys_.clear(); }

    bool empty() const { return keys_.empty(); }
    const std::vector<Key>& keys() const { return keys_; }

    // The value at timeMs: the first key's before it, the last key's after it, a blend between. False only
    // when the track has no keys (a parameter is never undefined while its track has keys).
    bool valueAt(int64_t timeMs, Value* out) const;

private:
    int indexAtOrBefore(int64_t timeMs) const;
    std::vector<Key> keys_;
};

}  // namespace geode::signal::timeline
