#include "sound/Fft.hpp"

#include <cmath>
#include <numbers>

namespace geode::sound {

Fft::Fft(int size)
    : n_(size), bitrev_(static_cast<size_t>(size)), twiddle_(static_cast<size_t>(size / 2)), work_(static_cast<size_t>(size)) {
    int bits = 0;
    while ((1 << bits) < size) bits++;
    for (int i = 0; i < size; i++) {
        int r = 0;
        for (int b = 0; b < bits; b++) {
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        }
        bitrev_[i] = r;
    }
    for (int k = 0; k < size / 2; k++) {
        const double a = -2.0 * std::numbers::pi * k / size;
        twiddle_[k] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
    }
}

void Fft::magnitudes(const float* real, float* out) {
    for (int i = 0; i < n_; i++) work_[bitrev_[i]] = {real[i], 0.0f};
    for (int len = 2; len <= n_; len <<= 1) {
        const int half = len / 2;
        const int stride = n_ / len;
        for (int i = 0; i < n_; i += len) {
            for (int j = 0; j < half; j++) {
                const std::complex<float> t = twiddle_[j * stride] * work_[i + j + half];
                work_[i + j + half] = work_[i + j] - t;
                work_[i + j] += t;
            }
        }
    }
    for (int k = 0; k <= n_ / 2; k++) out[k] = std::sqrt(std::norm(work_[k]));
}

}  // namespace geode::sound
