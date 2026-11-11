// Host-side checks for core/viz/TouchSmear: Settings › Visuals › Touch › Smear turns the fingers the touch field
// holds into strokes for the ripple overlay and Water. Off draws none, on draws one per moving finger, and the
// strength slider is the stroke's strength, clamped to 0.2..2.
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

bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

using Points = std::array<float, TouchField::kMaxPoints * TouchField::kPointStride>;

// The touch field's published points after one finger at (x, y) in y-up NDC (and a second when given).
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

// A drag from (-0.5, 0) to (0.5, 0) over ten frames: the strokes it gives under s.
std::vector<touch::SmearStroke> drag(touch::SmearSettings s) {
    touch::SmearTrail trail;
    std::vector<touch::SmearStroke> out;
    for (int i = 0; i <= 10; i++) {
        trail.step(fingers(-0.5f + 0.1f * static_cast<float>(i), 0.0f), 1.0f / 60.0f, s,
                   [&](const touch::SmearStroke& st) { out.push_back(st); });
    }
    return out;
}

}  // namespace

int main() {
    // Off: the same drag draws nothing.
    check(drag({false, 1.0f}).empty(), "smear off draws no stroke");

    // On: one stroke per frame the finger moved (the first frame only places it).
    const auto on = drag({true, 1.0f});
    check(on.size() == 10, "smear on draws a stroke per moved frame");
    if (!on.empty()) {
        // NDC (-0.4, 0) is surface (0.3, 0.5); a move of +0.1 NDC right is +0.05 of the surface.
        check(near(on[0].nx, 0.3f) && near(on[0].ny, 0.5f), "stroke sits under the finger, in 0..1 surface space");
        check(near(on[0].ndx, 0.05f) && near(on[0].ndy, 0.0f), "stroke carries the finger's motion");
        check(near(on[0].dt, 1.0f / 60.0f), "stroke carries the frame time");
    }
    // NDC is y up, the surface y down: a finger moving up moves the stroke toward 0.
    {
        touch::SmearTrail trail;
        std::vector<touch::SmearStroke> out;
        auto emit = [&](const touch::SmearStroke& st) { out.push_back(st); };
        trail.step(fingers(0.0f, 0.0f), 0.016f, {true, 1.0f}, emit);
        trail.step(fingers(0.0f, 0.2f), 0.016f, {true, 1.0f}, emit);
        check(out.size() == 1 && near(out[0].ndy, -0.1f) && near(out[0].ny, 0.4f), "y flips from NDC to the surface");
    }

    // Strength: low and high differ, and each stroke carries the slider's value.
    const auto low = drag({true, 0.2f});
    const auto high = drag({true, 2.0f});
    check(!low.empty() && near(low[0].strength, 0.2f), "low strength reaches the stroke");
    check(!high.empty() && near(high[0].strength, 2.0f), "high strength reaches the stroke");
    check(!low.empty() && !high.empty() && high[0].strength > low[0].strength * 5.0f, "the slider changes the smear");
    check(near(touch::smearStrength(10.0f), touch::kSmearStrengthMax), "strength clamps to the slider's max");
    check(near(touch::smearStrength(0.0f), touch::kSmearStrengthMin), "strength clamps to the slider's min");
    check(near(touch::smearStrength(std::numeric_limits<float>::quiet_NaN()), 1.0f), "NaN strength is the default");

    // A finger at rest draws nothing; a finger that jumps (another lifted, the slots shifted) draws nothing.
    {
        touch::SmearTrail trail;
        int n = 0;
        auto count = [&](const touch::SmearStroke&) { n++; };
        for (int i = 0; i < 5; i++) trail.step(fingers(0.2f, 0.2f), 0.016f, {true, 1.0f}, count);
        check(n == 0, "a still finger draws no stroke");
        trail.step(fingers(-0.8f, -0.8f), 0.016f, {true, 1.0f}, count);
        check(n == 0, "a slot jump draws no stroke");
    }
    // Two fingers dragging draw two strokes a frame; a lifted finger starts over when it lands again.
    {
        touch::SmearTrail trail;
        int n = 0;
        auto count = [&](const touch::SmearStroke&) { n++; };
        trail.step(fingers(0.0f, 0.0f, 0.5f, 0.0f), 0.016f, {true, 1.0f}, count);
        trail.step(fingers(0.05f, 0.0f, 0.55f, 0.0f), 0.016f, {true, 1.0f}, count);
        check(n == 2, "two moving fingers, two strokes");
        trail.step(Points{}, 0.016f, {true, 1.0f}, count);
        trail.step(fingers(0.1f, 0.0f), 0.016f, {true, 1.0f}, count);
        check(n == 2, "a finger landing again is placed, not dragged from where it lifted");
    }
    // Switched on mid-drag: the finger is already tracked, so the next move draws at once.
    {
        touch::SmearTrail trail;
        int n = 0;
        auto count = [&](const touch::SmearStroke&) { n++; };
        trail.step(fingers(0.0f, 0.0f), 0.016f, {false, 1.0f}, count);
        trail.step(fingers(0.05f, 0.0f), 0.016f, {true, 1.0f}, count);
        check(n == 1, "switching on mid-drag smears from the next move");
    }
    // A released finger fading in the touch field (strength < 1) is not down: no stroke.
    {
        touch::SmearTrail trail;
        int n = 0;
        auto count = [&](const touch::SmearStroke&) { n++; };
        Points fading = fingers(0.0f, 0.0f);
        fading[2] = 0.6f;
        trail.step(fading, 0.016f, {true, 1.0f}, count);
        fading[0] = 0.1f;
        trail.step(fading, 0.016f, {true, 1.0f}, count);
        check(n == 0, "a fading finger draws no stroke");
    }

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("touch_smear_test: OK\n");
    return 0;
}
