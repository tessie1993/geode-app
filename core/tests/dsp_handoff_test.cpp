// The playback DSP's format hand-off (issue #356): Media3 configures the next item's format while the old
// item's tail still drains, and the new format applies only after flush. A chain swapped in at configure fed
// a mono tail to a stereo chain, which walked twice the floats the buffer held. These cases drive the C ABI
// in the order Media3 calls it and check every float, plus guard floats past each buffer's capacity.
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

constexpr float kGuard = 12345.0f;
constexpr size_t kGuardFloats = 64;

// `floats` of signal followed by guard floats the DSP must never touch.
std::vector<float> block(size_t floats, float phase) {
    std::vector<float> v(floats + kGuardFloats, kGuard);
    for (size_t i = 0; i < floats; ++i) v[i] = 0.5f * std::sin(phase + 0.05f * static_cast<float>(i));
    return v;
}

bool guardsIntact(const std::vector<float>& v, size_t floats) {
    for (size_t i = floats; i < v.size(); ++i) {
        if (v[i] != kGuard) return false;
    }
    return true;
}

bool same(const std::vector<float>& a, const std::vector<float>& b) {
    return a == b;
}

// Settings that change every sample, so processed and pass-through output differ.
void tune(geode_dsp* d) {
    geode_dsp_ramp_to(d, -6.0f, 0);
    geode_dsp_set_enabled(d, 1);
    geode_dsp_set_band(d, 2, 600);
    geode_dsp_set_limiter(d, 1);
}

geode_dsp* fresh(int rate, int channels) {
    geode_dsp* d = geode_dsp_create();
    tune(d);
    geode_dsp_configure(d, rate, channels);
    geode_dsp_flush(d);
    return d;
}

void process(geode_dsp* d, std::vector<float>& v, size_t floats, int channels) {
    geode_dsp_process(d, v.data(), floats, floats / static_cast<size_t>(channels), channels);
}

// A mono item, then a stereo one configured while the mono tail still drains.
void monoTailDrainsThroughMonoChain() {
    geode_dsp* dsp = fresh(48000, 1);
    geode_dsp* reference = fresh(48000, 1);

    auto head = block(256, 0.0f);
    auto headRef = head;
    process(dsp, head, 256, 1);
    process(reference, headRef, 256, 1);

    check(geode_dsp_configure(dsp, 48000, 2) == 1, "stereo configures");

    // The held-back mono tail, in a buffer exactly as large as the tail.
    auto tail = block(256, 1.0f);
    auto tailRef = tail;
    process(dsp, tail, 256, 1);
    process(reference, tailRef, 256, 1);
    check(guardsIntact(tail, 256), "mono tail after a stereo configure stays inside its buffer");
    check(same(tail, tailRef), "mono tail after a stereo configure is processed by the mono chain");

    geode_dsp_flush(dsp);
    geode_dsp* stereoRef = fresh(48000, 2);
    auto next = block(512, 2.0f);
    auto nextRef = next;
    process(dsp, next, 512, 2);
    process(stereoRef, nextRef, 512, 2);
    check(guardsIntact(next, 512), "stereo after flush stays inside its buffer");
    check(same(next, nextRef), "stereo after flush runs a fresh stereo chain");

    geode_dsp_destroy(dsp);
    geode_dsp_destroy(reference);
    geode_dsp_destroy(stereoRef);
}

// Stereo to mono: the whole stereo tail is processed, not only its first half.
void stereoTailDrainsThroughStereoChain() {
    geode_dsp* dsp = fresh(44100, 2);
    geode_dsp* reference = fresh(44100, 2);
    check(geode_dsp_configure(dsp, 44100, 1) == 1, "mono configures");
    auto tail = block(400, 0.3f);
    auto tailRef = tail;
    process(dsp, tail, 400, 2);
    process(reference, tailRef, 400, 2);
    check(guardsIntact(tail, 400), "stereo tail after a mono configure stays inside its buffer");
    check(same(tail, tailRef), "stereo tail after a mono configure is processed by the stereo chain");
    geode_dsp_destroy(dsp);
    geode_dsp_destroy(reference);
}

// A rate change: the tail keeps the old rate's filters.
void rateChangeWaitsForFlush() {
    geode_dsp* dsp = fresh(44100, 2);
    geode_dsp* reference = fresh(44100, 2);
    geode_dsp_configure(dsp, 96000, 2);
    auto tail = block(300, 0.7f);
    auto tailRef = tail;
    process(dsp, tail, 300, 2);
    process(reference, tailRef, 300, 2);
    check(same(tail, tailRef), "tail after a rate configure keeps the old rate's chain");
    geode_dsp_destroy(dsp);
    geode_dsp_destroy(reference);
}

// A setting changed between configure and flush reaches the chain that goes live.
void settingsReachTheStagedChain() {
    geode_dsp* dsp = fresh(48000, 1);
    geode_dsp_configure(dsp, 48000, 2);
    geode_dsp_set_crossfeed(dsp, 1);
    geode_dsp_ramp_to(dsp, -3.0f, 0);
    geode_dsp_flush(dsp);

    geode_dsp* reference = geode_dsp_create();
    tune(reference);
    geode_dsp_set_crossfeed(reference, 1);
    geode_dsp_ramp_to(reference, -3.0f, 0);
    geode_dsp_configure(reference, 48000, 2);
    geode_dsp_flush(reference);

    auto v = block(512, 0.1f);
    auto vRef = v;
    process(dsp, v, 512, 2);
    process(reference, vRef, 512, 2);
    check(same(v, vRef), "settings set while a chain is staged apply once it is live");
    geode_dsp_destroy(dsp);
    geode_dsp_destroy(reference);
}

// No caller mistake walks past the buffer: wrong channels pass through, frames clamp to capacity.
void processNeverLeavesItsBuffer() {
    geode_dsp* dsp = fresh(48000, 2);
    auto v = block(100, 0.0f);
    const auto original = v;
    geode_dsp_process(dsp, v.data(), 100, 100, 1);
    check(same(v, original), "a block in a channel count the live chain does not run passes through");

    geode_dsp_process(dsp, v.data(), 50, 100, 2);  // 100 frames claimed, 25 fit
    check(guardsIntact(v, 100), "guards stay intact");
    bool restUntouched = true;
    for (size_t i = 50; i < 100; ++i) restUntouched = restUntouched && v[i] == original[i];
    check(restUntouched, "frames are clamped to the buffer's capacity");
    bool headProcessed = false;
    for (size_t i = 0; i < 50; ++i) headProcessed = headProcessed || v[i] != original[i];
    check(headProcessed, "the frames that fit are processed");
    geode_dsp_destroy(dsp);
}

// A format the chain cannot run passes through after its flush; the old chain does not linger.
void unrunnableFormatPassesThrough() {
    geode_dsp* dsp = fresh(48000, 2);
    check(geode_dsp_configure(dsp, 48000, 6) == 0, "six channels do not run");
    geode_dsp_flush(dsp);
    auto v = block(200, 0.4f);
    const auto original = v;
    process(dsp, v, 200, 2);
    check(same(v, original), "after an unrunnable format's flush nothing is processed");

    check(geode_dsp_configure(dsp, 48000, 2) == 1, "stereo configures again");
    geode_dsp_flush(dsp);
    process(dsp, v, 200, 2);
    check(!same(v, original), "stereo runs again after its flush");
    geode_dsp_destroy(dsp);
}

// Nothing is live before the first flush, as Media3 always flushes before queueing.
void nothingLiveBeforeFirstFlush() {
    geode_dsp* dsp = geode_dsp_create();
    tune(dsp);
    geode_dsp_configure(dsp, 48000, 2);
    auto v = block(64, 0.0f);
    const auto original = v;
    process(dsp, v, 64, 2);
    check(same(v, original), "before the first flush the processor passes through");
    geode_dsp_destroy(dsp);
}

}  // namespace

int main() {
    monoTailDrainsThroughMonoChain();
    stereoTailDrainsThroughStereoChain();
    rateChangeWaitsForFlush();
    settingsReachTheStagedChain();
    processNeverLeavesItsBuffer();
    unrunnableFormatPassesThrough();
    nothingLiveBeforeFirstFlush();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("dsp_handoff_test: all passed\n");
    return EXIT_SUCCESS;
}
