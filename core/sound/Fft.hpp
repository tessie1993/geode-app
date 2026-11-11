#pragma once
#include <complex>
#include <memory>
#include <vector>

template <typename scalar_t>
class kissfft;

namespace geode::sound {

// One power-of-two FFT size on KISS FFT (third_party/kissfft, its header-only C++ transform), plan
// built once. The live path and the export path run this same code on the same samples, which is what
// keeps them in agreement.
class Fft {
public:
    explicit Fft(int size);
    int size() const { return n_; }
    // |X[k]| for k = 0..size/2 of a real signal; `out` holds size/2 + 1 floats. Unnormalised.
    void magnitudes(const float* real, float* out);

private:
    int n_;
    std::shared_ptr<const kissfft<float>> plan_;
    std::vector<std::complex<float>> in_;
    std::vector<std::complex<float>> out_;
};

}  // namespace geode::sound
