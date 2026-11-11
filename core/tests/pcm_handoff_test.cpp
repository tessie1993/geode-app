// Host-side checks for MilkDrop's raw-PCM hand-off (core/viz/PcmHandoff). Built and run by CI ("Native
// host tests" in .github/workflows/android.yml) with the system compiler; no GL, no device.
//
// The renderer feeds projectM from the samples its own sound engine heard (Renderer::writeAudio), not from a
// second Kotlin ring and a push (#360). What is pinned:
// - a frame with no new samples (paused, starved) hands projectM and the PCM scenes nothing;
// - what a frame hands over is exactly the mono mix of the samples written since the last frame, in order,
//   however the writes were chunked: the same values the deleted Kotlin mono ring (PcmRingBuffer, the mean
//   of the channels) held, so MilkDrop sees what it saw in the app, now on every surface;
// - only the newest capacity() samples are kept, a new epoch drops what the old one left;
// - a PcmPulse fed nothing decays to 0 instead of holding its last peak.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "viz/PcmHandoff.hpp"

using geode::viz::PcmHandoff;
using geode::viz::PcmPulse;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-6f; }

// The ring's mono mix as the Kotlin PcmRingBuffer wrote it: the mean of a frame's channels.
float monoOf(const std::vector<float>& interleaved, int frame, int channels) {
    float acc = 0.0f;
    for (int c = 0; c < channels; ++c) acc += interleaved[static_cast<size_t>(frame * channels + c)];
    return acc / static_cast<float>(channels);
}

void noNewSamplesGivesNothing() {
    PcmHandoff handoff(8);
    std::vector<float> out(8, 0.0f);
    check(handoff.take(out.data()) == 0, "nothing written, nothing taken");
    const float stereo[] = {0.2f, 0.0f, -0.8f, -1.0f, 0.4f, 0.2f};
    handoff.write(stereo, 3, 2);
    check(handoff.take(out.data()) == 3, "the frame takes the three samples written");
    check(near(out[0], 0.1f) && near(out[1], -0.9f) && near(out[2], 0.3f), "as their mono mix, (L + R) / 2");
    check(handoff.take(out.data()) == 0, "a frame with no new samples gives projectM nothing");
    handoff.write(stereo, 0, 2);
    handoff.write(nullptr, 3, 2);
    handoff.write(stereo, 3, 0);
    check(handoff.take(out.data()) == 0, "empty writes leave nothing pending");
}

void monoPassesThrough() {
    PcmHandoff handoff(8);
    std::vector<float> out(8, 0.0f);
    const float mono[] = {0.5f, -0.25f};
    handoff.write(mono, 2, 1);
    check(handoff.take(out.data()) == 2 && out[0] == 0.5f && out[1] == -0.25f, "mono samples pass unchanged");
}

// Random chunk sizes, several writes per frame, a random number of frames: every frame hands over exactly
// the mono mix of what was written since the previous frame, in order, and nothing is dropped or repeated.
void rawTailMatchesTheRingsMonoMix() {
    constexpr int kChannels = 2;
    constexpr int kCapacity = 4096;
    std::mt19937 rng(486);
    std::uniform_real_distribution<float> sample(-1.0f, 1.0f);
    std::uniform_int_distribution<int> chunk(1, 700);
    std::uniform_int_distribution<int> writesPerFrame(0, 3);

    PcmHandoff handoff(kCapacity);
    std::vector<float> out(kCapacity, 0.0f);
    std::vector<float> written;  // every interleaved sample, as the ring held it
    std::vector<float> taken;    // every mono sample the frames handed over
    long long expectedFrames = 0;
    bool allMatched = true;
    bool emptyFramesEmpty = true;
    for (int frame = 0; frame < 400; ++frame) {
        const size_t frameStart = written.size() / kChannels;
        const int writes = writesPerFrame(rng);
        for (int w = 0; w < writes; ++w) {
            const int n = chunk(rng);
            std::vector<float> block(static_cast<size_t>(n * kChannels));
            for (float& s : block) s = sample(rng);
            handoff.write(block.data(), n, kChannels);
            written.insert(written.end(), block.begin(), block.end());
        }
        const int got = handoff.take(out.data());
        const int fresh = static_cast<int>(written.size() / kChannels - frameStart);
        if (writes == 0 && got != 0) emptyFramesEmpty = false;
        // Under capacity (all of these are), a frame hands over exactly what it heard.
        if (got != fresh) allMatched = false;
        for (int i = 0; i < got; ++i) {
            const float want = monoOf(written, static_cast<int>(frameStart) + i, kChannels);
            if (out[static_cast<size_t>(i)] != want) allMatched = false;
            taken.push_back(out[static_cast<size_t>(i)]);
        }
        expectedFrames += fresh;
    }
    check(emptyFramesEmpty, "frames with no writes take nothing");
    check(allMatched, "each frame's raw PCM is the mono mix of exactly the samples written since the last");
    check(static_cast<long long>(taken.size()) == expectedFrames, "every written sample reaches projectM once");
}

void keepsTheNewestSamples() {
    PcmHandoff handoff(4);
    std::vector<float> out(4, 0.0f);
    const float block[] = {1, 2, 3, 4, 5, 6};
    handoff.write(block, 6, 1);
    check(handoff.take(out.data()) == 4, "a long block is cut to capacity");
    check(out[0] == 3.0f && out[3] == 6.0f, "keeping its newest samples");
    handoff.write(block, 3, 1);
    handoff.write(block + 3, 3, 1);
    check(handoff.take(out.data()) == 4, "two writes over capacity in one frame are cut to capacity");
    check(out[0] == 3.0f && out[1] == 4.0f && out[3] == 6.0f, "keeping the newest across both writes");
}

void newEpochDropsTheOld() {
    PcmHandoff handoff(8);
    std::vector<float> out(8, 0.0f);
    const float old[] = {0.9f, 0.9f};
    handoff.write(old, 2, 1);
    handoff.clear();
    check(handoff.take(out.data()) == 0, "a reset drops samples of the old epoch not yet taken");
    const float fresh[] = {0.1f};
    handoff.write(fresh, 1, 1);
    check(handoff.take(out.data()) == 1 && out[0] == 0.1f, "the new epoch's samples follow");
}

void pulseDecaysWithoutNewPcm() {
    PcmHandoff handoff(8);
    PcmPulse pulse;
    std::vector<float> out(8, 0.0f);
    const float loud[] = {0.8f, -1.0f, 0.5f};
    handoff.write(loud, 3, 1);
    // Sixty frames as Renderer runs them: take once per frame, deliver only what was taken.
    float first = 0.0f;
    float last = 1.0f;
    for (int frame = 0; frame < 60; ++frame) {
        const int n = handoff.take(out.data());
        if (n > 0) pulse.accept(out.data(), n);
        const float level = pulse.tick(1.0f / 60.0f);
        if (frame == 0) first = level;
        last = level;
    }
    check(first == 1.0f, "the pulse peaks on the written block");
    check(last == 0.0f, "with no new PCM it decays to 0 within a second");
}

}  // namespace

int main() {
    noNewSamplesGivesNothing();
    monoPassesThrough();
    rawTailMatchesTheRingsMonoMix();
    keepsTheNewestSamples();
    newEpochDropsTheOld();
    pulseDecaysWithoutNewPcm();
    if (failures == 0) std::printf("pcm_handoff_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
