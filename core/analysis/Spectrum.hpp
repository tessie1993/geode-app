#pragma once
#include <memory>
#include <vector>

#include "kiss_fftr.h"

namespace geode::analysis {

class Spectrum {
public:
    explicit Spectrum(int fftSize);
    int fftSize() const { return fftSize_; }
    double binHz(int sampleRateHz) const { return static_cast<double>(sampleRateHz) / fftSize_; }
    void compute(const float* windowed);
    int peakBin() const;
    const std::vector<float>& magnitudes() const { return magnitudes_; }

private:
    struct CfgFree {
        void operator()(kiss_fftr_state* cfg) const { kiss_fftr_free(cfg); }
    };
    int fftSize_;
    std::unique_ptr<kiss_fftr_state, CfgFree> cfg_;
    std::vector<kiss_fft_cpx> bins_;
    std::vector<float> magnitudes_;
};

}  // namespace geode::analysis
