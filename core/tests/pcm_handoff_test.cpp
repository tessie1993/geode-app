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
// - a hi-res source (88.2-192 kHz) reaches projectM at 44.1-48 kHz, whatever the chunk sizes;
// - a PcmPulse fed nothing decays to 0 instead of holding its last peak.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "viz/PcmHandoff.hpp"

using geode::viz::PcmHandoff;
using geode::viz::PcmPulse;
using geode::viz::PcmStarve;

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

// Kills handing projectM source-rate PCM: its buffers are sized for 44.1-48 kHz, so at 96 kHz they would
// cover half the time and its bands shift an octave. A 1 kHz stereo sine at 96 kHz, written in odd chunks
// (so most writes end half way through an output sample), comes out as 48 kHz: half the samples, each the
// mean of two source frames, one period every 48 samples.
void hiResSourcesAreDecimated() {
    check(PcmHandoff::decimationFor(0) == 1 && PcmHandoff::decimationFor(44100) == 1 && PcmHandoff::decimationFor(48000) == 1,
          "44.1 and 48 kHz pass through");
    check(PcmHandoff::decimationFor(88200) == 2 && PcmHandoff::decimationFor(96000) == 2, "88.2 and 96 kHz are halved");
    check(PcmHandoff::decimationFor(176400) == 4 && PcmHandoff::decimationFor(192000) == 4 && PcmHandoff::decimationFor(384000) == 4,
          "176.4 kHz and up are quartered");

    constexpr int kRate = 96000;
    constexpr int kFrames = 9600;  // 100 ms
    constexpr float kTwoPi = 6.2831853f;
    std::vector<float> stereo(static_cast<size_t>(kFrames) * 2);
    for (int i = 0; i < kFrames; ++i) {
        const float s = std::sin(kTwoPi * 1000.0f * static_cast<float>(i) / static_cast<float>(kRate));
        stereo[static_cast<size_t>(i) * 2] = s;
        stereo[static_cast<size_t>(i) * 2 + 1] = s;
    }
    PcmHandoff handoff(4096);
    std::vector<float> out(4096, 0.0f);
    std::vector<float> taken;
    const int chunks[] = {331, 77, 1023, 1, 505};
    int at = 0;
    for (int w = 0; at < kFrames; ++w) {
        const int n = std::min(chunks[w % 5], kFrames - at);
        handoff.write(stereo.data() + static_cast<size_t>(at) * 2, n, 2, kRate);
        at += n;
        const int got = handoff.take(out.data());
        taken.insert(taken.end(), out.begin(), out.begin() + got);
    }
    check(taken.size() == static_cast<size_t>(kFrames / 2), "96 kHz reaches projectM as 48 kHz: half the samples, none dropped");
    bool pairs = taken.size() == static_cast<size_t>(kFrames / 2);
    for (size_t k = 0; pairs && k < taken.size(); ++k) {
        if (!near(taken[k], 0.5f * (stereo[4 * k] + stereo[4 * k + 2]))) pairs = false;
    }
    check(pairs, "each sample is the mean of two source frames, across chunk boundaries");
    bool periodic = taken.size() > 48;
    float peak = 0.0f;
    for (size_t k = 0; k + 48 < taken.size(); ++k) {
        if (std::fabs(taken[k] - taken[k + 48]) > 1e-3f || std::fabs(taken[k] + taken[k + 24]) > 1e-3f) periodic = false;
        peak = std::max(peak, std::fabs(taken[k]));
    }
    check(periodic, "a 1 kHz sine keeps its pitch: one period every 48 samples");
    check(peak > 0.99f && peak <= 1.0f, "and its level");

    // 192 kHz: four frames a sample, a remainder carried over the write, dropped by a new epoch.
    PcmHandoff quad(16);
    const float ramp[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    quad.write(ramp, 3, 1, 192000);
    check(quad.take(out.data()) == 0, "three frames at 192 kHz are not yet a sample");
    quad.write(ramp + 3, 7, 1, 192000);
    check(quad.take(out.data()) == 2 && near(out[0], 2.5f) && near(out[1], 6.5f), "the remainder joins the next write");
    quad.clear();
    quad.write(ramp, 4, 1, 192000);
    check(quad.take(out.data()) == 1 && near(out[0], 2.5f), "a new epoch drops the old remainder");
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

void sideSignalGivesStereoBack() {
    PcmHandoff handoff(8);
    std::vector<float> mono(8, 0.0f);
    std::vector<float> side(8, 9.0f);
    const float stereo[] = {0.5f, -0.3f, 0.2f, 0.2f, -1.0f, 1.0f};
    handoff.write(stereo, 3, 2);
    check(handoff.take(mono.data(), side.data()) == 3, "three stereo frames taken");
    bool rebuilt = true;
    for (int i = 0; i < 3; ++i) {
        rebuilt = rebuilt && near(mono[i] + side[i], stereo[i * 2]) && near(mono[i] - side[i], stereo[i * 2 + 1]);
    }
    check(rebuilt, "mono + side is left and mono - side is right");
    check(near(mono[2], 0.0f) && near(side[2], -1.0f), "out-of-phase content survives in the side signal");

    const float single[] = {0.4f, -0.6f};
    handoff.write(single, 2, 1);
    check(handoff.take(mono.data(), side.data()) == 2 && side[0] == 0.0f && side[1] == 0.0f, "a mono source has no side signal");

    // 96 kHz is halved: the side signal is averaged over the same pair of frames as the mono mix.
    const float hiRes[] = {1.0f, 0.0f, 0.0f, 1.0f, 0.5f, -0.5f, 0.5f, -0.5f};
    handoff.write(hiRes, 4, 2, 96000);
    check(handoff.take(mono.data(), side.data()) == 2, "four frames at 96 kHz are two samples");
    check(near(mono[0], 0.5f) && near(side[0], 0.0f) && near(mono[1], 0.0f) && near(side[1], 0.5f), "decimated like the mono mix");

    // Over capacity, the side signal keeps the same newest samples as the mono mix.
    PcmHandoff small(2);
    const float ramp[] = {1.0f, 0.0f, 2.0f, 0.0f, 3.0f, 0.0f};
    small.write(ramp, 3, 2);
    check(small.take(mono.data(), side.data()) == 2 && near(mono[0], 1.0f) && near(side[0], 1.0f) && near(mono[1], 1.5f) && near(side[1], 1.5f),
          "the newest samples, on both signals");
}

void starvedEngineIsFlushedOnce() {
    const float dt = 1.0f / 60.0f;
    PcmStarve never;
    bool flushedUnfed = false;
    for (int frame = 0; frame < 120; ++frame) flushedUnfed = flushedUnfed || never.tick(false, dt);
    check(!flushedUnfed, "an engine that was never fed is not handed silence");

    PcmStarve starve;
    check(!starve.tick(true, dt), "a frame with samples is not starved");
    // A decoder that delivers a block every 100 ms leaves five empty frames in six.
    bool flushedBetweenBlocks = false;
    for (int frame = 0; frame < 60; ++frame) flushedBetweenBlocks = flushedBetweenBlocks || starve.tick(frame % 6 == 0, dt);
    check(!flushedBetweenBlocks, "gaps between decoder blocks are not silence");
    int flushes = 0;
    int firstFlush = -1;
    for (int frame = 0; frame < 300; ++frame) {
        if (starve.tick(false, dt)) {
            ++flushes;
            if (firstFlush < 0) firstFlush = frame;
        }
    }
    check(flushes == 1, "a pause hands the engine silence exactly once");
    check(firstFlush >= 9 && firstFlush <= 15, "about a quarter of a second in");
    check(!starve.tick(true, dt), "samples end the gap");
    flushes = 0;
    for (int frame = 0; frame < 60; ++frame) flushes += starve.tick(false, dt) ? 1 : 0;
    check(flushes == 1, "and the next pause is flushed again");
}

int main() {
    noNewSamplesGivesNothing();
    monoPassesThrough();
    rawTailMatchesTheRingsMonoMix();
    keepsTheNewestSamples();
    newEpochDropsTheOld();
    hiResSourcesAreDecimated();
    pulseDecaysWithoutNewPcm();
    sideSignalGivesStereoBack();
    starvedEngineIsFlushedOnce();
    if (failures == 0) std::printf("pcm_handoff_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
