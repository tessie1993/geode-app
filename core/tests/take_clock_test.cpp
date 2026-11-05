// Host-side checks for a capture take's timing and sizing (core/output/TakeClock, ADR 0008). Built and run by CI
// ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL, no codec, no device.
//
// What is pinned (#456): the encoder size starts at the screen's and steps down for an encoder that refuses it;
// the frame rate is the display's up to 60; frames carry the engine's frame time from the take's start and a
// 120 Hz screen records at 60; the sound starts at the take's time zero, follows on sample by sample, fills a
// pause with silence and lands exactly at its own time after one, and lasts as long as the picture.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "output/TakeClock.hpp"

using geode::output::TakeAudio;
using geode::output::TakeFramePicker;
using geode::output::takeBitrate;
using geode::output::takeFps;
using geode::output::takeSize;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int64_t kMs = 1'000'000;  // ns
constexpr int kRate = TakeAudio::kRate;

// Interleaved stereo frames of one value, so a test can find where they landed.
std::vector<int16_t> block(int frames, int16_t value, int channels = 2) {
    return std::vector<int16_t>(static_cast<size_t>(frames) * channels, value);
}

// Everything the take would hand the encoder by `nowNs`, as frames (left channel).
std::vector<int16_t> drain(TakeAudio& audio, int64_t nowNs) {
    std::vector<int16_t> left;
    std::vector<int16_t> buf(1024 * 2);
    for (;;) {
        const int n = audio.read(buf.data(), 1024, nowNs);
        if (n == 0) break;
        for (int i = 0; i < n; ++i) left.push_back(buf[static_cast<size_t>(i) * 2]);
    }
    return left;
}

int firstIndexOf(const std::vector<int16_t>& v, int16_t value, size_t from = 0) {
    for (size_t i = from; i < v.size(); ++i) {
        if (v[i] == value) return static_cast<int>(i);
    }
    return -1;
}

void sizesStartAtTheScreenAndStepDown() {
    auto s = takeSize(1080, 2400, 0);
    check(s.width == 1080 && s.height == 2400, "attempt 0 is the screen's own size");
    s = takeSize(1081, 2401, 0);
    check(s.width == 1080 && s.height == 2400, "an odd screen rounds down to even sides");
    s = takeSize(1080, 2400, 1);
    check(s.width == 800 && s.height == 1792, "a refused size steps to 3/4, on the 16-pixel grid");
    auto prev = takeSize(1080, 2400, 1);
    for (int a = 2; a < geode::output::kMaxAttempts; ++a) {
        const auto next = takeSize(1080, 2400, a);
        check(next.width > 0 && next.width < prev.width && next.height < prev.height, "every retry is smaller");
        check(next.width % 16 == 0 && next.height % 16 == 0, "every retry is on the 16-pixel grid");
        prev = next;
    }
    check(takeSize(1080, 2400, geode::output::kMaxAttempts).width == 0, "past the last attempt there is no size");
    s = takeSize(8000, 3000, 0);
    check(s.width <= geode::output::kMaxSide && s.height <= geode::output::kMaxSide, "no side is ever past 4096");
    check(takeSize(0, 0, 0).width == 0, "a stage that never drew has no take size");
}

void fpsAndBitrate() {
    check(takeFps(120.0f) == 60.0f, "a 120 Hz screen records at 60");
    check(takeFps(90.0f) == 60.0f, "a 90 Hz screen records at 60");
    check(takeFps(60.0f) == 60.0f, "60 Hz records at 60");
    check(takeFps(30.0f) == 30.0f, "a 30 fps cap records at 30");
    check(takeFps(10.0f) == 24.0f, "never under 24");
    check(takeFps(0.0f) == 30.0f && takeFps(NAN) == 30.0f, "an unknown rate records at 30");
    const int phone = takeBitrate({1080, 2400}, 60.0f);
    check(phone > 15'000'000 && phone < 30'000'000, "a phone screen at 60 fps gets about 0.15 bits a pixel");
    check(takeBitrate({160, 96}, 24.0f) == 2'000'000, "a tiny take still gets 2 Mbit/s");
    check(takeBitrate({4096, 4096}, 60.0f) == 40'000'000, "a huge take is held to 40 Mbit/s");
}

void framesCarryTheEngineClock() {
    const int64_t start = 5'000 * kMs;
    TakeFramePicker picker(start, 60.0f);
    check(!picker.admit(start - 16 * kMs), "a frame rendered before the take began is not in it");
    // 60 Hz with up to 3 ms of jitter: every frame is kept.
    int kept = 0;
    int64_t last = -1;
    bool forward = true;
    for (int f = 0; f < 120; ++f) {
        const int64_t jitter = (f % 3 - 1) * 3 * kMs;
        const auto pts = picker.admit(start + f * 16'667'000 + jitter + 3 * kMs);
        if (pts) {
            ++kept;
            forward = forward && *pts > last;
            last = *pts;
        }
    }
    check(kept == 120, "every frame of a jittery 60 Hz screen is kept");
    check(forward, "presentation times only go forward");
    check(picker.lastPtsUs() == last, "the last kept frame's time is known for the audio pad");

    TakeFramePicker fast(start, 120.0f);
    int fastKept = 0;
    for (int f = 0; f < 240; ++f) {
        if (fast.admit(start + f * 8'333'333)) ++fastKept;
    }
    check(fastKept == 120, "a 120 Hz screen keeps every other frame: 60 a second");

    TakeFramePicker first(start, 60.0f);
    const auto pts = first.admit(start + 40 * kMs);
    check(pts && *pts == 40'000, "a frame's time is its engine frame time since the take began, in us");
}

void soundStartsAtTimeZero() {
    TakeAudio audio(0);
    const auto b = block(4800, 1000);
    audio.write(b.data(), 4800, 2, kRate, 100 * kMs);
    const auto left = drain(audio, 100 * kMs);
    check(left.size() == 9600, "the first block lands at its own time, after silence from time zero");
    check(firstIndexOf(left, 1000) == 4800, "100 ms of silence, then the sound");
    check(audio.nextReadPtsUs() == 200'000, "the next read starts at 200 ms");
}

void blocksFollowSampleBySample() {
    TakeAudio audio(0);
    const auto b = block(480, 500);
    // The sink writes every 10 ms, give or take 4 ms; the stream must not jitter with it.
    for (int i = 0; i < 100; ++i) {
        const int64_t jitter = (i % 5 - 2) * 2 * kMs;
        audio.write(b.data(), 480, 2, kRate, 50 * kMs + i * 10 * kMs + jitter);
    }
    const auto left = drain(audio, 1'050 * kMs);
    // The first block came 4 ms early (46 ms): 2208 frames of silence, then 100 blocks back to back.
    check(left.size() == static_cast<size_t>(2208 + 100 * 480), "no silence slips in between blocks that keep up");
}

void aPauseIsFilledAndTheSoundLandsOnTime() {
    TakeAudio audio(0);
    const auto playing = block(480, 700);
    for (int i = 0; i < 100; ++i) audio.write(playing.data(), 480, 2, kRate, i * 10 * kMs);
    // Paused for two seconds: the encoder keeps reading, and the track keeps running in silence.
    auto left = drain(audio, 1'000 * kMs);
    const auto paused = drain(audio, 3'000 * kMs);
    check(!paused.empty(), "a pause is filled with silence while it lasts");
    left.insert(left.end(), paused.begin(), paused.end());
    const auto resumed = block(480, 900);
    audio.write(resumed.data(), 480, 2, kRate, 3'000 * kMs);
    const auto after = drain(audio, 3'010 * kMs);
    left.insert(left.end(), after.begin(), after.end());
    check(firstIndexOf(left, 900) == 3 * kRate, "the first block after a pause lands exactly at its own time");
}

void aLongHoleWithoutReadsIsFilled() {
    TakeAudio audio(0);
    const auto a = block(480, 300);
    audio.write(a.data(), 480, 2, kRate, 0);
    const auto b = block(480, 400);
    audio.write(b.data(), 480, 2, kRate, 2'000 * kMs);
    const auto left = drain(audio, 2'010 * kMs);
    check(firstIndexOf(left, 400) == 2 * kRate, "a block after a hole longer than 200 ms fills the hole");
}

void noSourceIsASilentTrack() {
    TakeAudio audio(0);
    const auto left = drain(audio, 1'000 * kMs);
    check(left.size() == static_cast<size_t>(kRate * 8 / 10), "with nothing playing the track runs 200 ms behind now");
    audio.padTo(2'000'000);
    const auto rest = drain(audio, 1'000 * kMs);
    check(left.size() + rest.size() == static_cast<size_t>(2 * kRate), "the take's end pads the sound to the last frame");
}

void anyFormatBecomesStereo48k() {
    TakeAudio audio(0);
    std::vector<int16_t> mono(4410, 1200);
    audio.write(mono.data(), 4410, 1, 44'100, 0);
    std::vector<int16_t> buf(8000 * 2);
    const int n = audio.read(buf.data(), 8000, 0);
    check(std::abs(n - 4800) <= 2, "100 ms at 44.1 kHz is 100 ms at 48 kHz");
    bool doubled = n > 0;
    for (int i = 0; i < n; ++i) doubled = doubled && buf[static_cast<size_t>(i) * 2] == buf[static_cast<size_t>(i) * 2 + 1];
    check(doubled, "mono is doubled to both channels");

    TakeAudio surround(0);
    std::vector<int16_t> six(480 * 6, 0);
    for (int f = 0; f < 480; ++f) {
        six[static_cast<size_t>(f) * 6] = 11;
        six[static_cast<size_t>(f) * 6 + 1] = 22;
    }
    surround.write(six.data(), 480, 6, kRate, 0);
    std::vector<int16_t> out(480 * 2);
    const int m = surround.read(out.data(), 480, 0);
    check(m == 480 && out[0] == 11 && out[1] == 22, "more than two channels keep the front pair");
}

}  // namespace

int main() {
    sizesStartAtTheScreenAndStepDown();
    fpsAndBitrate();
    framesCarryTheEngineClock();
    soundStartsAtTimeZero();
    blocksFollowSampleBySample();
    aPauseIsFilledAndTheSoundLandsOnTime();
    aLongHoleWithoutReadsIsFilled();
    noSourceIsASilentTrack();
    anyFormatBecomesStereo48k();
    if (failures == 0) std::printf("take_clock_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
