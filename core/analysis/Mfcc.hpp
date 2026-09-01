#pragma once
#include <vector>

namespace geode::analysis {

class Mfcc {
public:
    static constexpr double kLogPowerFloor = 1e-10;

    explicit Mfcc(int melCount, int count = 13);
    int count() const { return count_; }
    const std::vector<float>& coefficients() const { return coefficients_; }
    const std::vector<float>& delta() const { return delta_; }
    float timbreFlux() const { return timbreFlux_; }
    void compute(const float* melPower);
    void reset();

private:
    int melCount_;
    int count_;
    std::vector<std::vector<double>> basis_;
    std::vector<double> logMel_;
    std::vector<float> previous_;
    bool hasPrevious_ = false;
    std::vector<float> coefficients_;
    std::vector<float> delta_;
    float timbreFlux_ = 0.0f;
};

}  // namespace geode::analysis
