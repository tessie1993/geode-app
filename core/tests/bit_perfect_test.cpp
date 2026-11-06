// Bit-perfect USB (issue #353, ticket #489): while the platform takes the AudioTrack bit-perfect, the playback
// DSP is pure pass-through, so the DAC gets the file's exact samples whatever gain, equalizer, bass boost,
// loudness, crossfeed or limiter is set (the gain ramps of #483 sit inside the same chain, so they are skipped
// with it). These cases drive the C ABI in the order Media3 calls it (configure, flush, process) and compare
// every byte, on both the float and the 16-bit path, including full-scale and silent samples.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "api/geode_api.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// Every setting that changes samples, turned well away from neutral, so a chain that runs at all shows it.
void tuneHard(geode_dsp* d) {
    geode_dsp_set_gain_db(d, -9.0f);
    geode_dsp_set_enabled(d, 1);
    for (int band = 0; band < GEODE_DSP_BANDS; ++band) geode_dsp_set_band(d, band, (band % 2 ? 1 : -1) * 900);
    geode_dsp_set_bass_boost(d, 800);
    geode_dsp_set_loudness_mb(d, 600);
    geode_dsp_set_crossfeed(d, 1);
    geode_dsp_set_limiter(d, 1);
}

// Neutral: nothing that changes a sample (the limiter is off too: its lookahead delays the signal).
void tuneNeutral(geode_dsp* d) {
    geode_dsp_set_gain_db(d, 0.0f);
    geode_dsp_set_enabled(d, 0);
    geode_dsp_set_bass_boost(d, 0);
    geode_dsp_set_loudness_mb(d, 0);
    geode_dsp_set_crossfeed(d, 0);
    geode_dsp_set_limiter(d, 0);
}

std::vector<float> floats(size_t n, float phase) {
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = 0.9f * std::sin(phase + 0.031f * static_cast<float>(i));
    v[0] = 1.0f;
    v[1] = -1.0f;
    v[2] = 0.0f;
    v[3] = -0.0f;
    return v;
}

std::vector<int16_t> pcm16(size_t n, uint32_t seed) {
    std::vector<int16_t> v(n);
    for (size_t i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u;  // a full-range LCG: every bit pattern is fair game
        v[i] = static_cast<int16_t>(seed >> 16);
    }
    v[0] = INT16_MAX;
    v[1] = INT16_MIN;
    v[2] = 0;
    v[3] = -1;
    return v;
}

template <typename T>
bool bytesEqual(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0;
}

void runFloat(geode_dsp* d, std::vector<float>& v, int channels) {
    geode_dsp_process(d, v.data(), v.size(), v.size() / static_cast<size_t>(channels), channels);
}

void runPcm16(geode_dsp* d, std::vector<int16_t>& v, int channels) {
    geode_dsp_process_pcm16(d, v.data(), v.size(), v.size() / static_cast<size_t>(channels), channels);
}

geode_dsp* bitPerfectDsp(int rate, int channels) {
    geode_dsp* d = geode_dsp_create();
    tuneHard(d);
    geode_dsp_set_bit_perfect(d, 1);  // before configure: the output provider runs before the pipeline flush
    geode_dsp_configure(d, rate, channels);
    geode_dsp_flush(d);
    return d;
}

// The acceptance case: every setting on, bit-perfect live, every byte out equals every byte in, at every
// rate and channel count the chain runs, over many blocks (the chunked 16-bit path crosses its chunk size).
void bitIdenticalWhateverIsSet() {
    const int rates[] = {44100, 48000, 96000, 192000};
    for (int rate : rates) {
        for (int channels = 1; channels <= 2; ++channels) {
            geode_dsp* d = bitPerfectDsp(rate, channels);
            check(geode_dsp_bit_perfect_live(d) == 1, "bit-perfect is live after the flush");
            for (int block = 0; block < 8; ++block) {
                const size_t n = static_cast<size_t>(channels) * (block % 2 ? 4096 : 333);
                auto f = floats(n, static_cast<float>(block));
                const auto fIn = f;
                runFloat(d, f, channels);
                check(bytesEqual(f, fIn), "float block is bit-identical with bit-perfect on");

                auto s = pcm16(n, static_cast<uint32_t>(rate + block * 7 + channels));
                const auto sIn = s;
                runPcm16(d, s, channels);
                check(bytesEqual(s, sIn), "16-bit block is bit-identical with bit-perfect on");

                // Settings moved mid-play (a ReplayGain change, an EQ drag) change nothing either.
                geode_dsp_set_gain_db(d, static_cast<float>(block) - 4.0f);
                geode_dsp_set_band(d, block % GEODE_DSP_BANDS, GEODE_DSP_MAX_MILLIBELS);
            }
            geode_dsp_destroy(d);
        }
    }
}

// A seek or a new item flushes again: bit-perfect stays live through it, and a format change under it too.
void survivesFlushAndFormatChange() {
    geode_dsp* d = bitPerfectDsp(44100, 2);
    geode_dsp_configure(d, 96000, 1);
    geode_dsp_flush(d);
    auto s = pcm16(1000, 99);
    const auto sIn = s;
    runPcm16(d, s, 1);
    check(bytesEqual(s, sIn), "bit-identical after a format change under bit-perfect");
    geode_dsp_flush(d);
    auto f = floats(1000, 3.0f);
    const auto fIn = f;
    runFloat(d, f, 1);
    check(bytesEqual(f, fIn), "bit-identical after a seek's flush");
    geode_dsp_destroy(d);
}

// The flag latches at flush, the moment a new AudioTrack follows, never mid-track: before the flush the
// chain still runs (the open track is still mixed), after the flush it is pass-through, and off again it
// runs from clean state, exactly like a chain that never was bit-perfect.
void latchesAtFlush() {
    geode_dsp* d = geode_dsp_create();
    tuneHard(d);
    geode_dsp_configure(d, 48000, 2);
    geode_dsp_flush(d);

    geode_dsp_set_bit_perfect(d, 1);
    check(geode_dsp_bit_perfect_live(d) == 0, "not live before the flush");
    auto before = pcm16(2048, 5);
    const auto beforeIn = before;
    runPcm16(d, before, 2);
    check(!bytesEqual(before, beforeIn), "the chain still runs until the flush");

    geode_dsp_flush(d);
    auto during = pcm16(2048, 6);
    const auto duringIn = during;
    runPcm16(d, during, 2);
    check(bytesEqual(during, duringIn), "pass-through after the flush");

    geode_dsp_set_bit_perfect(d, 0);
    auto stillOn = pcm16(2048, 7);
    const auto stillOnIn = stillOn;
    runPcm16(d, stillOn, 2);
    check(bytesEqual(stillOn, stillOnIn), "still pass-through until the flush that ends it");

    geode_dsp_flush(d);
    check(geode_dsp_bit_perfect_live(d) == 0, "off after the flush");
    geode_dsp* reference = geode_dsp_create();
    tuneHard(reference);
    geode_dsp_configure(reference, 48000, 2);
    geode_dsp_flush(reference);
    auto after = floats(4096, 1.5f);
    auto afterRef = after;
    runFloat(d, after, 2);
    runFloat(reference, afterRef, 2);
    check(!bytesEqual(after, floats(4096, 1.5f)), "the chain runs again after bit-perfect ends");
    check(bytesEqual(after, afterRef), "and from clean state, like a chain that never stepped aside");
    geode_dsp_destroy(reference);
    geode_dsp_destroy(d);
}

// The 16-bit path's own conversion (moved out of Kotlin) is lossless through a neutral chain: int16 -> float
// -> int16 is exact at full scale 32768, so turning nothing on also leaves every sample as it was.
void neutralPcm16RoundTripIsExact() {
    geode_dsp* d = geode_dsp_create();
    tuneNeutral(d);
    geode_dsp_configure(d, 44100, 2);
    geode_dsp_flush(d);
    std::vector<int16_t> all(65536);
    for (size_t i = 0; i < all.size(); ++i) all[i] = static_cast<int16_t>(static_cast<int32_t>(i) - 32768);
    const auto in = all;
    runPcm16(d, all, 2);
    check(bytesEqual(all, in), "every int16 value survives the neutral chain unchanged");
    geode_dsp_destroy(d);
}

// The 16-bit path clips like the Kotlin shell did: a +12 dB gain on full scale lands on the int16 rails.
void pcm16Clips() {
    geode_dsp* d = geode_dsp_create();
    tuneNeutral(d);
    geode_dsp_set_gain_db(d, 12.0f);
    geode_dsp_configure(d, 48000, 1);
    geode_dsp_flush(d);  // reset() snaps the gain to its target, so the first sample is at +12 dB
    std::vector<int16_t> v = {INT16_MAX, INT16_MIN, 16384, -16384, 1000};
    runPcm16(d, v, 1);
    check(v[0] == INT16_MAX && v[1] == INT16_MIN, "full scale clips to the rails");
    check(v[2] == INT16_MAX && v[3] == INT16_MIN, "half scale at +12 dB clips too");
    check(v[4] > 3900 && v[4] < 4000, "a small sample is scaled by about four");
    geode_dsp_destroy(d);
}

// Capacity still bounds the 16-bit path: frames past the buffer are never touched.
void pcm16RespectsCapacity() {
    geode_dsp* d = geode_dsp_create();
    tuneHard(d);
    geode_dsp_configure(d, 48000, 2);
    geode_dsp_flush(d);
    std::vector<int16_t> v(200, 1234);
    geode_dsp_process_pcm16(d, v.data(), 100, 1000, 2);  // 1000 frames claimed, 50 fit
    bool tailIntact = true;
    for (size_t i = 100; i < v.size(); ++i) tailIntact = tailIntact && v[i] == 1234;
    check(tailIntact, "samples past capacity are untouched");
    geode_dsp_destroy(d);
}

}  // namespace

int main() {
    bitIdenticalWhateverIsSet();
    survivesFlushAndFormatChange();
    latchesAtFlush();
    neutralPcm16RoundTripIsExact();
    pcm16Clips();
    pcm16RespectsCapacity();
    if (failures == 0) std::printf("bit_perfect_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
