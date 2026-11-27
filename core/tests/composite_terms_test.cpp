// Host-side checks for core/viz/passes/CompositeTerms.hpp: the per-frame terms and stage flags composite_frag.glsl
// applies. Built and run by CI from the repository root.
//
// What is pinned: neutral params run no stage; each stage turns on just past the threshold the shader used to test
// per pixel and not at it; a texture's gate (grade::gateFor) leaves the stages its scene already applied off; a
// camera at rest runs only the mirror fold, a camera away from it runs the projection too; the rotation's cos and
// sin, the sway, and the frame's derived values. What it cannot show: the pixels.
#include <cmath>
#include <cstdio>
#include <functional>

#include "viz/passes/CompositeTerms.hpp"

namespace composite = geode::viz::composite;
namespace grade = geode::viz::grade;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

const grade::Gate kAll{true, true, true};
const grade::Gate kNone{false, false, false};
const grade::Gate kGeoOnly{true, false, false};

int flagsOf(const std::function<void(composite::Look&)>& set, const grade::Gate& gate = kAll, bool camera = false) {
    composite::Look look;
    set(look);
    return composite::textureTerms(look, gate, camera).flags;
}

int frameOf(const std::function<void(composite::Look&)>& set) {
    composite::Look look;
    set(look);
    return composite::frameTerms(look).frameFlags;
}

void neutral() {
    check(flagsOf([](composite::Look&) {}) == 0, "neutral params, every gate on: no stage");
    check(flagsOf([](composite::Look&) {}, kNone) == 0, "neutral params, no gate: no stage");
    check(frameOf([](composite::Look&) {}) == 0, "neutral params: no lens stage");
    const composite::TextureTerms t = composite::textureTerms(composite::Look{}, kAll, false);
    check(t.rotCos == 1.0f && t.rotSin == 0.0f, "no rotation: cos 1, sin 0");
}

void thresholds() {
    struct Case {
        const char* what;
        int flag;
        std::function<void(composite::Look&, bool)> set;
    };
    const Case cases[] = {
        {"rotation", composite::kRotate, [](composite::Look& l, bool on) { l.rotation = on ? 0.00011f : 0.0001f; }},
        {"zoom", composite::kZoom, [](composite::Look& l, bool on) { l.zoom = on ? 1.00011f : 1.00005f; }},
        {"mirror", composite::kMirror, [](composite::Look& l, bool on) { l.mirror = on; }},
        {"kaleido", composite::kKaleido, [](composite::Look& l, bool on) { l.kaleidoOn = 1.0f; l.kaleidoFolds = on ? 2.0f : 1.0f; }},
        {"twist", composite::kTwist, [](composite::Look& l, bool on) { l.twist = on ? 0.0011f : 0.001f; }},
        {"warp", composite::kWarp, [](composite::Look& l, bool on) { l.warp = on ? 0.0011f : 0.001f; }},
        {"ripple", composite::kRipple, [](composite::Look& l, bool on) { l.ripple = on ? 0.0011f : 0.001f; }},
        {"births", composite::kBirths, [](composite::Look& l, bool on) { l.life = on ? 0.01f : 0.0f; }},
        {"drift", composite::kDrift, [](composite::Look& l, bool on) { l.driftX = on ? 0.0011f : 0.001f; }},
        {"tile", composite::kTile, [](composite::Look& l, bool on) { l.tile = on ? 1.02f : 1.01f; }},
        {"pixelate", composite::kPixelate, [](composite::Look& l, bool on) { l.pixelate = on ? 0.0011f : 0.001f; }},
        {"bloom", composite::kBloom, [](composite::Look& l, bool on) { l.bloom = on ? 0.0011f : 0.001f; }},
        {"posterize", composite::kPosterize, [](composite::Look& l, bool on) { l.posterize = on ? 0.0011f : 0.001f; }},
        {"hue", composite::kHue, [](composite::Look& l, bool on) { l.hue = on ? 0.00011f : 0.0001f; }},
        {"saturation", composite::kSaturation, [](composite::Look& l, bool on) { l.saturation = on ? 1.00011f : 1.00005f; }},
        {"contrast", composite::kContrast, [](composite::Look& l, bool on) { l.contrast = on ? 1.00011f : 1.00005f; }},
        {"gamma", composite::kGamma, [](composite::Look& l, bool on) { l.gamma = on ? 1.00011f : 1.00005f; }},
        {"temperature", composite::kTemperature, [](composite::Look& l, bool on) { l.temperature = on ? 0.001f : 0.0f; }},
        {"solarize", composite::kSolarize, [](composite::Look& l, bool on) { l.solarize = on; }},
        {"brightness", composite::kBrightness, [](composite::Look& l, bool on) { l.brightness = on ? 1.001f : 1.0f; }},
        {"invert", composite::kInvert, [](composite::Look& l, bool on) { l.invert = on; }},
    };
    for (const Case& c : cases) {
        char what[96];
        const int on = flagsOf([&](composite::Look& l) { c.set(l, true); });
        std::snprintf(what, sizeof what, "%s: on just past its threshold", c.what);
        check((on & c.flag) != 0, what);
        const int off = flagsOf([&](composite::Look& l) { c.set(l, false); });
        std::snprintf(what, sizeof what, "%s: off at its threshold", c.what);
        check((off & c.flag) == 0, what);
        const int gated = flagsOf([&](composite::Look& l) { c.set(l, true); }, kNone);
        std::snprintf(what, sizeof what, "%s: off when the scene applied it", c.what);
        check((gated & c.flag) == 0, what);
    }
}

void gates() {
    const auto all = [](composite::Look& l) {
        l.zoom = 2.0f;
        l.rotation = 1.0f;
        l.saturation = 2.0f;
        l.hue = 0.5f;
        l.mirror = true;
        l.invert = true;
        l.warp = 1.0f;
        l.solarize = true;
        l.brightness = 2.0f;
    };
    const int geo = flagsOf(all, kGeoOnly);
    check((geo & composite::kWarp) != 0 && (geo & composite::kSolarize) != 0, "geometry gate: warp and solarize run");
    check((geo & (composite::kZoom | composite::kSaturation | composite::kHue | composite::kBrightness | composite::kMirror | composite::kInvert)) == 0,
          "geometry gate: grade, zoom, brightness, mirror and invert stay off");
    const int mirrorInvert = flagsOf(all, grade::Gate{false, true, false});
    check(mirrorInvert == (composite::kMirror | composite::kInvert), "mirror gate: only mirror and invert");
    const int grading = flagsOf(all, grade::Gate{false, false, true});
    check((grading & composite::kRotate) != 0 && (grading & composite::kZoom) != 0 && (grading & composite::kBrightness) != 0,
          "grade gate: rotation, zoom and brightness run");
    check((grading & composite::kWarp) == 0, "grade gate: warp stays off");
}

void camera() {
    check(flagsOf([](composite::Look&) {}, kNone, true) == composite::kCameraFold, "a camera at rest: only the fold");
    check(flagsOf([](composite::Look&) {}, kNone, false) == 0, "a scene that drew the camera: no camera stage");
    const auto moved = [](composite::Look& l) { l.camFlat[0] = 0.1f; };
    check(flagsOf(moved, kNone, true) == (composite::kCamera | composite::kCameraFold), "a panned camera: projection and fold");
    check(flagsOf([](composite::Look& l) { l.camFlat[2] = 1.5f; }, kNone, true) == (composite::kCamera | composite::kCameraFold),
          "a dollied camera runs the projection");
    check(flagsOf([](composite::Look& l) { l.camFlat[3] = 0.2f; }, kNone, true) == (composite::kCamera | composite::kCameraFold),
          "a rolled camera runs the projection");
    check(flagsOf([](composite::Look& l) { l.camTilt[1] = 0.2f; }, kNone, true) == (composite::kCamera | composite::kCameraFold),
          "a pitched camera runs the projection");
    check(flagsOf(moved, kNone, false) == 0, "a moved camera the scene drew itself: no camera stage");
}

void rotation() {
    composite::Look l;
    l.rotation = 0.5f;
    composite::TextureTerms t = composite::textureTerms(l, kAll, false);
    check(near(t.rotCos, std::cos(0.5f)) && near(t.rotSin, std::sin(0.5f)), "rotation: cos and sin of the angle");
    l.sway = 1.0f;
    l.time = 2.0f;
    const float angle = 0.5f + composite::swayAngle(1.0f, 2.0f);
    t = composite::textureTerms(l, kAll, false);
    check(near(t.rotCos, std::cos(angle)) && near(t.rotSin, std::sin(angle)), "sway joins the rotation by the plasma view's law");
    t = composite::textureTerms(l, kGeoOnly, false);
    check((t.flags & composite::kRotate) != 0 && near(t.rotCos, std::cos(composite::swayAngle(1.0f, 2.0f))),
          "geometry gate: the sway alone, not the rotation");
    t = composite::textureTerms(l, grade::Gate{false, false, true}, false);
    check(near(t.rotCos, std::cos(0.5f)), "grade gate: the rotation alone, not the sway");
}

void frame() {
    check((frameOf([](composite::Look& l) { l.flowStrength = 0.002f; }) & composite::kFlow) != 0, "flow past 0.001");
    check(frameOf([](composite::Look& l) { l.flowStrength = 0.001f; }) == 0, "flow at 0.001 is off");
    check((frameOf([](composite::Look& l) { l.fisheye = -0.5f; }) & composite::kFisheye) != 0, "a negative fisheye runs");
    check((frameOf([](composite::Look& l) { l.chroma = 0.5f; }) & composite::kChroma) != 0, "chroma");
    check((frameOf([](composite::Look& l) { l.scanlines = 0.5f; }) & composite::kScanlines) != 0, "scanlines");
    check((frameOf([](composite::Look& l) { l.grain = 0.5f; }) & composite::kGrain) != 0, "grain");
    check((frameOf([](composite::Look& l) { l.vignette = 0.5f; }) & composite::kVignette) != 0, "vignette");

    composite::Look l;
    l.kaleidoFolds = 6.0f;
    l.twist = 0.5f;
    l.twistBend = 0.5f;
    l.warp = 0.5f;
    l.ripple = 0.5f;
    l.driftX = 1.0f;
    l.driftY = -1.0f;
    l.time = 10.0f;
    l.pixelate = 0.5f;
    l.posterize = 1.0f;
    l.gamma = 2.0f;
    l.temperature = 1.0f;
    l.zoom = 4.0f;
    l.hue = 0.25f;
    l.chroma = 1.0f;
    l.fisheye = 1.0f;
    l.scanlines = 1.0f;
    l.grain = 1.0f;
    l.camFlat = {0.0f, 0.0f, 0.0f, 0.5f};
    l.camTilt = {0.3f, 0.4f};
    const composite::FrameTerms t = composite::frameTerms(l);
    check(near(t.geo[0], 6.2831853f / 6.0f) && near(t.geo[1], 1.0f) && near(t.geo[2], 0.03f) && near(t.geo[3], 0.06f), "geometry terms");
    check(near(t.geo2[0], 1.0f) && near(t.geo2[1], -1.0f) && near(t.geo2[2], 276.0f) && near(t.geo2[3], 1.5f), "drift, pixelate cells, twist exponent");
    check(near(t.shade[0], 3.0f) && near(t.shade[1], 0.5f) && near(t.shade[2], 0.12f) && near(t.shade[3], 0.25f), "shading terms");
    check(near(t.hueCS[0], std::cos(0.25f * 6.2831f)) && near(t.hueCS[1], std::sin(0.25f * 6.2831f)), "hue's cos and sin");
    check(near(t.lens[1], 0.6f) && near(t.lens[2], 0.03f) && near(t.lens[3], 0.35f) && near(t.lens2[0], 0.25f), "lens terms");
    check(near(t.lens2[2], 12.0f) && near(t.lens2[3], 30.0f), "the clock's multiples");
    check(near(t.camTiltCS[0], std::cos(0.3f)) && near(t.camTiltCS[3], std::sin(0.4f)), "camera tilt's cos and sin");
    check(near(t.camRoll[0], std::cos(0.5f)) && near(t.camRoll[1], std::sin(0.5f)) && near(t.camRoll[2], 20.0f),
          "camera roll's cos and sin, and the dolly's reciprocal floored at 0.05");
    check(composite::frameTerms(composite::Look{}).geo[0] == 0.0f, "no kaleidoscope: no segment");
}

}  // namespace

int main() {
    neutral();
    thresholds();
    gates();
    camera();
    rotation();
    frame();
    if (failures != 0) {
        std::printf("composite_terms_test: %d failed\n", failures);
        return 1;
    }
    std::printf("composite_terms_test: all passed\n");
    return 0;
}
