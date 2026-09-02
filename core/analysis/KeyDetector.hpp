#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace geode::analysis {

class KeyDetector {
public:
    void accumulate(const float* magnitudes, int magnitudeCount, int sampleRateHz, int fftSize);
    std::string finish() const;
    static std::string compact(const std::string& key);

private:
    static double norm(const double* v);

    std::array<double, 12> chroma_{};
    std::array<double, 12> frame_{};
    int64_t frames_ = 0;
};

}  // namespace geode::analysis
