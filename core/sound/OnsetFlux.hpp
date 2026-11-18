#pragma once
#include <algorithm>
#include <cmath>

// The reference spectrum for the frame's spectral flux, with vibrato suppressed.
//
// Flux (SoundEngine::descriptors) is the positive change of each bin's log magnitude against the spectrum
// ~30 ms before, summed over the 64 log-spaced bands: the log-magnitude flux that Böck, Krebs & Schedl (2012)
// found the most accurate of the classic online onset detection functions. Its weak spot is a held note that
// wobbles in pitch (a singer's or a violin's vibrato, a slide): each wobble moves energy into the next bin up
// or down, the bin it leaves falls (and the half-wave rectifier drops that) while the bin it enters rises, so
// every cycle of the wobble reads as a little onset. Böck & Widmer (2013) fix it by comparing each bin not with
// the same bin earlier but with the largest of its neighbours earlier: the earlier spectrum goes through a
// maximum filter along frequency before the difference. Energy that only slid sideways within the filter's
// reach finds itself already there and counts nothing; a new sound, which adds energy where none was, still
// rises above the whole neighbourhood and counts in full.
//
// The reach. The paper filters a quarter-tone filter bank over ±1 band at a 10 ms lag. A 6 Hz vibrato a
// semitone deep moves the pitch at up to ~38 semitones a second: about 0.4 semitone in 10 ms, and about 1.3 in
// this engine's ~33 ms lag. So the reach here is the same idea scaled to the lag: a semitone either way of each
// bin's own frequency (2^(1/12) - 1 of it), and at least one bin, where a semitone is narrower than a bin. As a
// fraction of the bin's own frequency it is the same in Hz at every sample rate.
//
// Not added: adaptive whitening (Stowell & Plumbley 2007), which divides each bin by a slowly released memory of
// its own peak. It helps a linear-magnitude flux (Böck, Krebs & Schedl 2012), but a log-magnitude difference is
// already a ratio of the bin to its earlier self, so a per-bin gain divides out of it except below the log's
// knee; and how loud a hit is, is judged absolutely afterwards (the hit floor and the drive), not here.
//
// Allocation-free: the reach is a table built once, and the filter writes into a buffer the caller owns.
//
//   S. Böck, G. Widmer, "Maximum filter vibrato suppression for onset detection", DAFx-13, 2013.
//   S. Böck, F. Krebs, M. Schedl, "Evaluating the online capabilities of onset detection methods", ISMIR 2012.
//   D. Stowell, M. D. Plumbley, "Adaptive whitening for improved real-time audio onset detection", ICMC 2007.
namespace geode::sound::onsetflux {

// A semitone, as a fraction of a frequency: 2^(1/12) - 1.
constexpr float kReachFraction = 0.05946309f;

// How many bins either way bin `k`'s neighbourhood spans.
inline int reachOf(int k) { return std::max(1, static_cast<int>(std::lround(static_cast<float>(k) * kReachFraction))); }

// out[k] = the largest of in[k - reach[k] … k + reach[k]], for k in [from, to), the window clipped to [from, to).
// `in` and `out` must not overlap. A sliding maximum when the windows only move up (reachOf's table: the reach grows
// by at most a bin a step) and fit the queue; else the plain scan, the same values either way.
inline void maxFilter(const float* in, float* out, int from, int to, const int* reach) {
    constexpr int kQueue = 1024;  // a power of two: wider windows take the plain scan
    bool sliding = true;
    int prevLo = from;
    for (int k = from; k < to && sliding; k++) {
        const int lo = std::max(from, k - reach[k]);
        const int hi = std::min(to - 1, k + reach[k]);
        sliding = lo >= prevLo && hi - lo < kQueue;
        prevLo = lo;
    }
    if (!sliding) {
        for (int k = from; k < to; k++) {
            const int lo = std::max(from, k - reach[k]);
            const int hi = std::min(to - 1, k + reach[k]);
            float m = in[lo];
            for (int j = lo + 1; j <= hi; j++) m = std::max(m, in[j]);
            out[k] = m;
        }
        return;
    }
    // Bin indices, the values in `in` falling from the front: the front is the window's largest.
    int queue[kQueue];
    unsigned head = 0;
    unsigned tail = 0;
    int next = from;
    for (int k = from; k < to; k++) {
        const int lo = std::max(from, k - reach[k]);
        const int hi = std::min(to - 1, k + reach[k]);
        for (; next <= hi; next++) {
            while (tail != head && in[queue[(tail - 1) % kQueue]] <= in[next]) tail--;
            queue[tail++ % kQueue] = next;
        }
        while (queue[head % kQueue] < lo) head++;
        out[k] = in[queue[head % kQueue]];
    }
}

}  // namespace geode::sound::onsetflux
