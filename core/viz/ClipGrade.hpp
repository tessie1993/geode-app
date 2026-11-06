#pragma once

namespace geode::viz {

// The Studio clip grade as one column-major 4x4 colour matrix: hue ∘ saturation ∘ contrast ∘ brightness,
// the order ClipEdit.videoEffects applies them. Ported from the Kotlin gradeMatrix
// (export/KeyframedClipEffects.kt); Media3's RgbMatrix effect stays Kotlin and asks this per frame.
//
// brightness is an offset added to each channel; contrast k follows Media3's Contrast,
// (c - 0.5) * (1 + k) / (1.0001 - k) + 0.5; saturationPercent is -100..100 around the Rec. 709 luma; hue
// rotates by degrees with the classic luminance-preserving matrix. Pure maths (core/tests/grade_test).
void clipGradeMatrix(float brightness, float contrast, float saturationPercent, float hueDegrees, float out[16]);

}  // namespace geode::viz
