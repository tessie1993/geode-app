#include "analysis/FrameLevels.hpp"

#include <cmath>

namespace geode::analysis::levels {

double rms(const float* frame, int count) {
    if (count <= 0) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        const double x = frame[i];
        sum += x * x;
    }
    return std::sqrt(sum / count);
}

float peak(const float* frame, int count) {
    float top = 0.0f;
    for (int i = 0; i < count; i++) {
        const float magnitude = std::fabs(frame[i]);
        if (magnitude > top) top = magnitude;
    }
    return top;
}

double zeroCrossingRate(const float* frame, int count) {
    if (count < 2) return 0.0;
    int crossings = 0;
    bool previousNegative = frame[0] < 0.0f;
    for (int i = 1; i < count; i++) {
        const bool negative = frame[i] < 0.0f;
        if (negative != previousNegative) crossings++;
        previousNegative = negative;
    }
    return static_cast<double>(crossings) / (count - 1);
}

}  // namespace geode::analysis::levels
