// Host-side checks for core/viz/TouchSmear: Settings › Gestures & controls › Touch › Smear turns the fingers the
// touch field holds into kicks on the flow field, and the composite bends the picture by it. Off kicks nothing,
// on kicks once per moving finger, the bend is the strength slider's, fades after the last kick, holds in
// silence and is scaled by reduced motion.
// Built and run by CI (.github/workflows/native-host-tests.yml) with the system compiler; no GL.
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "viz/TouchField.hpp"
#include "viz/TouchSmear.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

using Points = std::array<float, TouchField::kMaxPoints * TouchField::kPointStride>;

// The touch field's published points with one finger at (x, y) in y-up NDC (and a second when given).
Points fingers(float x, float y, float x2 = NAN, float y2 = NAN) {
    Points p{};
    p[0] = x;
    p[1] = y;
    p[2] = 1.0f;
    if (!std::isnan(x2)) {
        p[4] = x2;
        p[5] = y2;
        p[6] = 1.0f;
    }
    return p;
}

// A drag from (-0.5, 0) to (0.5, 0) over `frames` frames at `fps` (one clip unit per second by default).
std::vector<touch::SmearKick> drag(touch::SmearSettings s, float fps = 60.0f, int frames = 60) {
    touch::SmearTrail trail;
    std::vector<touch::SmearKick> out;
    for (int i = 0; i <= frames; i++) {
        trail.step(fingers(-0.5f + static_cast<float>(i) / static_cast<float>(frames), 0.0f), 1.0f / fps, s,
                   [&](const touch::SmearKick& k) { out.push_back(k); });
    }
    return out;
}

}  // namespace

int main() {
    // Off: the same drag kicks nothing.
    check(drag({false, 1.0f}).empty(), "smear off kicks nothing");

    // On: one kick per frame the finger moved (the first frame only places it), under the finger, along it.
    const auto on = drag({true, 1.0f});
    check(on.size() == 60, "smear on kicks once per moved frame");
    if (!on.empty()) {
        check(near(on[0].x, -0.5f + 1.0f / 60.0f) && near(on[0].y, 0.0f), "the kick sits under the finger, in clip units");
        // One clip unit per second, times the push.
        check(near(on[0].vx, touch::kSmearPush, 1e-2f) && near(on[0].vy, 0.0f), "the kick carries the finger's speed");
    }
    // The finger's speed, not the frame rate: the same drag over the same time at 30 and 120 fps.
    {
        const auto at30 = drag({true, 1.0f}, 30.0f, 30);
        const auto at120 = drag({true, 1.0f}, 120.0f, 120);
        check(!at30.empty() && !at120.empty() && near(at30[0].vx, at120[0].vx, 1e-2f), "the kick is per second, not per frame");
    }
    // A flick is capped.
    {
        touch::SmearTrail trail;
        std::vector<touch::SmearKick> out;
        auto emit = [&](const touch::SmearKick& k) { out.push_back(k); };
        trail.step(fingers(0.0f, 0.0f), 0.001f, {true, 1.0f}, emit);
        trail.step(fingers(0.4f, 0.0f), 0.001f, {true, 1.0f}, emit);
        check(out.size() == 1 && near(std::hypot(out[0].vx, out[0].vy), touch::kSmearMaxKick, 1e-2f), "a flick's kick is capped");
    }
    // y stays up, as the flow field's clip units are.
    {
        touch::SmearTrail trail;
        std::vector<touch::SmearKick> out;
        auto emit = [&](const touch::SmearKick& k) { out.push_back(k); };
        trail.step(fingers(0.0f, 0.0f), 0.016f, {true, 1.0f}, emit);
        trail.step(fingers(0.0f, 0.2f), 0.016f, {true, 1.0f}, emit);
        check(out.size() == 1 && out[0].vy > 0.0f && near(out[0].y, 0.2f), "a finger moving up kicks up");
    }
    // Silence: the frame clock is 0 and nothing kicks.
    {
        touch::SmearTrail trail;
        int n = 0;
        auto count = [&](const touch::SmearKick&) { n++; };
        trail.step(fingers(0.0f, 0.0f), 0.0f, {true, 1.0f}, count);
        trail.step(fingers(0.3f, 0.0f), 0.0f, {true, 1.0f}, count);
        check(n == 0, "no sound, no smear");
    }
    // A finger at rest kicks nothing; a finger that jumps (another lifted, the slots shifted) kicks nothing.
    {
        touch::SmearTrail trail;
        int n = 0;
        auto count = [&](const touch::SmearKick&) { n++; };
        for (int i = 0; i < 5; i++) trail.step(fingers(0.2f, 0.2f), 0.016f, {true, 1.0f}, count);
        check(n == 0, "a still finger kicks nothing");
        trail.step(fingers(-0.8f, -0.8f), 0.016f, {true, 1.0f}, count);
        check(n == 0, "a slot jump kicks nothing");
    }
    // Two fingers dragging kick twice a frame; a lifted finger starts over when it lands again.
    {
        touch::SmearTrail trail;
        int n = 0;
        auto count = [&](const touch::SmearKick&) { n++; };
        trail.step(fingers(0.0f, 0.0f, 0.5f, 0.0f), 0.016f, {true, 1.0f}, count);
        trail.step(fingers(0.05f, 0.0f, 0.55f, 0.0f), 0.016f, {true, 1.0f}, count);
        check(n == 2, "two moving fingers, two kicks");
        trail.step(Points{}, 0.016f, {true, 1.0f}, count);
        trail.step(fingers(0.1f, 0.0f), 0.016f, {true, 1.0f}, count);
        check(n == 2, "a finger landing again is placed, not dragged from where it lifted");
    }
    // A released finger fading in the touch field (strength < 1) is not down: no kick.
    {
        touch::SmearTrail trail;
        int n = 0;
        auto count = [&](const touch::SmearKick&) { n++; };
        Points fading = fingers(0.0f, 0.0f);
        fading[2] = 0.6f;
        trail.step(fading, 0.016f, {true, 1.0f}, count);
        fading[0] = 0.1f;
        trail.step(fading, 0.016f, {true, 1.0f}, count);
        check(n == 0, "a fading finger kicks nothing");
    }

    // The strength slider: clamped, and it is the bend.
    check(near(touch::smearStrength(10.0f), touch::kSmearStrengthMax), "strength clamps to the slider's max");
    check(near(touch::smearStrength(0.0f), touch::kSmearStrengthMin), "strength clamps to the slider's min");
    check(near(touch::smearStrength(std::numeric_limits<float>::quiet_NaN()), 1.0f), "NaN strength is the default");
    check(near(touch::smearWarp({true, 0.2f}, 1.0f, false), 0.2f) && near(touch::smearWarp({true, 2.0f}, 1.0f, false), 2.0f),
          "the slider sets the bend");
    check(touch::smearWarp({false, 2.0f}, 1.0f, false) == 0.0f, "off bends nothing");
    check(near(touch::smearWarp({true, 1.0f}, 1.0f, true), safety::kReducedMotionScale), "reduced motion scales the bend");
    check(touch::smearWarp({true, 1.0f}, std::numeric_limits<float>::quiet_NaN(), false) == 0.0f, "a broken fade bends nothing");

    // The linger: idle at start, live after a kick, full for a while, fading out, then idle; held in silence.
    {
        touch::SmearLinger l;
        check(!l.live() && l.fade() == 0.0f, "nothing to carry before a kick");
        l.kicked();
        check(l.live() && near(l.fade(), 1.0f), "a kick bends at once, fully");
        for (int i = 0; i < 60; i++) l.step(1.0f / 60.0f);
        check(l.live() && near(l.fade(), 1.0f), "still full a second after the last kick");
        for (int i = 0; i < 60; i++) l.step(1.0f / 60.0f);
        const float mid = l.fade();
        check(mid > 0.0f && mid < 1.0f, "fading in the last second");
        for (int i = 0; i < 600; i++) l.step(0.0f);
        check(near(l.fade(), mid), "silence holds the fade where it is");
        for (int i = 0; i < 60; i++) l.step(1.0f / 60.0f);
        check(!l.live() && l.fade() == 0.0f, "settled after the linger");
        l.kicked();
        l.reset();
        check(!l.live(), "reset settles it");
    }
    // The same linger at 30 and 120 fps.
    {
        touch::SmearLinger a;
        touch::SmearLinger b;
        a.kicked();
        b.kicked();
        for (int i = 0; i < 60; i++) a.step(1.0f / 30.0f);
        for (int i = 0; i < 240; i++) b.step(1.0f / 120.0f);
        check(near(a.fade(), b.fade(), 1e-3f), "the fade follows time, not frames");
    }

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("touch_smear_test: OK\n");
    return 0;
}
