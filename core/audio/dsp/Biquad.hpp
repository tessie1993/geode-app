#pragma once
#include <cmath>

namespace geode::audio {

// One second-order IIR section in transposed direct form II; coefficients from the RBJ cookbook.
class Biquad {
public:
    struct Coefficients {
        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
    };

    static Coefficients peaking(float sampleRate, float centerHz, float q, float gainDb) {
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * centerHz / sampleRate;
        const float alpha = std::sin(w0) / (2.0f * q);
        const float cosw = std::cos(w0);
        const float a0 = 1.0f + alpha / a;
        return {(1.0f + alpha * a) / a0, (-2.0f * cosw) / a0, (1.0f - alpha * a) / a0, (-2.0f * cosw) / a0, (1.0f - alpha / a) / a0};
    }

    static Coefficients lowShelf(float sampleRate, float cornerHz, float slope, float gainDb) {
        const float a = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 2.0f * kPi * cornerHz / sampleRate;
        const float cosw = std::cos(w0);
        const float alpha = std::sin(w0) / 2.0f * std::sqrt((a + 1.0f / a) * (1.0f / slope - 1.0f) + 2.0f);
        const float beta = 2.0f * std::sqrt(a) * alpha;
        const float a0 = (a + 1.0f) + (a - 1.0f) * cosw + beta;
        return {a * ((a + 1.0f) - (a - 1.0f) * cosw + beta) / a0, 2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosw) / a0,
                a * ((a + 1.0f) - (a - 1.0f) * cosw - beta) / a0, -2.0f * ((a - 1.0f) + (a + 1.0f) * cosw) / a0,
                ((a + 1.0f) + (a - 1.0f) * cosw - beta) / a0};
    }

    static Coefficients lowPass(float sampleRate, float cutoffHz, float q) {
        const float w0 = 2.0f * kPi * cutoffHz / sampleRate;
        const float alpha = std::sin(w0) / (2.0f * q);
        const float cosw = std::cos(w0);
        const float a0 = 1.0f + alpha;
        return {(1.0f - cosw) / (2.0f * a0), (1.0f - cosw) / a0, (1.0f - cosw) / (2.0f * a0), (-2.0f * cosw) / a0, (1.0f - alpha) / a0};
    }

    void set(const Coefficients& c) { c_ = c; }
    void reset() { z1_ = z2_ = 0.0f; }

    float process(float x) {
        const float y = c_.b0 * x + z1_;
        z1_ = c_.b1 * x - c_.a1 * y + z2_;
        z2_ = c_.b2 * x - c_.a2 * y;
        return y;
    }

private:
    static constexpr float kPi = 3.14159265f;

    Coefficients c_;
    float z1_ = 0.0f;
    float z2_ = 0.0f;
};

}  // namespace geode::audio
