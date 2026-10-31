#pragma once
#include <complex>
#include <vector>

namespace geode::sound {

// Iterative radix-2 FFT of one power-of-two size, bit reversal and twiddles built once. Our own rather
// than a library's so the engine has no dependency and gives the same numbers on every platform that
// builds it, which is what lets the live path and the export path agree to the bit.
class Fft {
public:
    explicit Fft(int size);
    int size() const { return n_; }
    // |X[k]| for k = 0..size/2 of a real signal; `out` holds size/2 + 1 floats. Unnormalised.
    void magnitudes(const float* real, float* out);

private:
    int n_;
    std::vector<int> bitrev_;
    std::vector<std::complex<float>> twiddle_;
    std::vector<std::complex<float>> work_;
};

}  // namespace geode::sound
