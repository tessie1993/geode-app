#include "viz/ClipGrade.hpp"

#include <cmath>

namespace geode::viz {

namespace {
constexpr float kLumaR = 0.2126f;
constexpr float kLumaG = 0.7152f;
constexpr float kLumaB = 0.0722f;
constexpr double kPi = 3.14159265358979323846;

struct Mat {
    float m[16];
};

Mat identity() {
    Mat a{};
    a.m[0] = a.m[5] = a.m[10] = a.m[15] = 1.0f;
    return a;
}

// lhs * rhs, both column-major (android.opengl.Matrix.multiplyMM).
Mat mul(const Mat& lhs, const Mat& rhs) {
    Mat out{};
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += lhs.m[k * 4 + r] * rhs.m[c * 4 + k];
            out.m[c * 4 + r] = sum;
        }
    }
    return out;
}

// A 3x3 given row by row (output R, then G, then B) as a column-major 4x4.
Mat rows(float rr, float rg, float rb, float gr, float gg, float gb, float br, float bg, float bb) {
    Mat a = identity();
    a.m[0] = rr;
    a.m[4] = rg;
    a.m[8] = rb;
    a.m[1] = gr;
    a.m[5] = gg;
    a.m[9] = gb;
    a.m[2] = br;
    a.m[6] = bg;
    a.m[10] = bb;
    return a;
}

Mat brightnessMatrix(float b) {
    Mat a = identity();
    a.m[12] = a.m[13] = a.m[14] = b;
    return a;
}

Mat contrastMatrix(float k) {
    const float factor = (1.0f + k) / (1.0001f - k);
    const float offset = 0.5f * (1.0f - factor);
    Mat a = identity();
    a.m[0] = a.m[5] = a.m[10] = factor;
    a.m[12] = a.m[13] = a.m[14] = offset;
    return a;
}

Mat saturationMatrix(float s) {
    const float r = kLumaR * (1.0f - s);
    const float g = kLumaG * (1.0f - s);
    const float b = kLumaB * (1.0f - s);
    return rows(r + s, g, b, r, g + s, b, r, g, b + s);
}

Mat hueMatrix(float degrees) {
    const double a = static_cast<double>(degrees) * kPi / 180.0;
    const float c = static_cast<float>(std::cos(a));
    const float s = static_cast<float>(std::sin(a));
    return rows(0.213f + 0.787f * c - 0.213f * s, 0.715f - 0.715f * c - 0.715f * s, 0.072f - 0.072f * c + 0.928f * s,
                0.213f - 0.213f * c + 0.143f * s, 0.715f + 0.285f * c + 0.140f * s, 0.072f - 0.072f * c - 0.283f * s,
                0.213f - 0.213f * c - 0.787f * s, 0.715f - 0.715f * c + 0.715f * s, 0.072f + 0.928f * c + 0.072f * s);
}
}  // namespace

void clipGradeMatrix(float brightness, float contrast, float saturationPercent, float hueDegrees, float out[16]) {
    const Mat graded = mul(hueMatrix(hueDegrees),
                           mul(saturationMatrix(1.0f + saturationPercent / 100.0f),
                               mul(contrastMatrix(contrast), brightnessMatrix(brightness))));
    for (int i = 0; i < 16; ++i) out[i] = graded.m[i];
}

}  // namespace geode::viz
