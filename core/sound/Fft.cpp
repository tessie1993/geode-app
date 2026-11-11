#include "sound/Fft.hpp"

#include <bit>
#include <cmath>
#include <cstdlib>

// Reached by its path from this file, so the host tests' plain `-Icore` builds find it too.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "../../third_party/kissfft/kissfft.hh"
#pragma GCC diagnostic pop

namespace geode::sound {

Fft::Fft(int size) : n_(size), in_(static_cast<size_t>(size)), out_(static_cast<size_t>(size)) {
    // Power-of-two sizes only: the engine's frame sizes are, and KISS FFT stays on its radix-4 and
    // radix-2 butterflies for them, which never touch its scratch buffer.
    if (size < 2 || !std::has_single_bit(static_cast<unsigned>(size))) std::abort();
    plan_ = std::make_shared<const kissfft<float>>(static_cast<std::size_t>(size), false);
}

void Fft::magnitudes(const float* real, float* out) {
    for (int i = 0; i < n_; i++) in_[static_cast<size_t>(i)] = {real[i], 0.0f};
    plan_->transform(in_.data(), out_.data());
    for (int k = 0; k <= n_ / 2; k++) out[k] = std::sqrt(std::norm(out_[static_cast<size_t>(k)]));
}

}  // namespace geode::sound
