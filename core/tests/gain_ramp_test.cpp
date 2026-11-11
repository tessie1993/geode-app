// The playback gain as native ramps (#483, #351): every ramp is linear in dB and counted in samples, no frame
// steps by more than one ramp increment, a track's ReplayGain starts on its first sample, a crossfade's two
// halves mirror each other across the join, and the sleep fade ends in silence as the timer does. Driven
// through the C ABI in the order Media3 calls it, with block sizes that never line up with a ramp.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

#include "api/geode_api.h"
#include "audio/dsp/GainRamp.hpp"
#include "audio/dsp/ReplayGain.hpp"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int kRate = 48000;
constexpr float kLevel = 0.5f;
constexpr double kSilence = static_cast<double>(GEODE_DSP_SILENCE_DB);
constexpr double kNoGain = -1000.0;  // what level() reports for a frame multiplied by exactly 0

// A mono chain that changes nothing but the gain: equalizer off (its default), limiter off.
geode_dsp* fresh() {
    geode_dsp* d = geode_dsp_create();
    geode_dsp_set_limiter(d, 0);
    geode_dsp_configure(d, kRate, 1);
    geode_dsp_flush(d);
    return d;
}

// Block sizes from a fixed sequence that never lines up with the ramps: the result must not depend on them.
size_t nextBlock() {
    static uint32_t state = 12345u;
    state = state * 1664525u + 1013904223u;
    return 1 + (state >> 8) % 700u;
}

double level(float out) {
    return out == 0.0f ? kNoGain : 20.0 * std::log10(static_cast<double>(out) / static_cast<double>(kLevel));
}

// Processes `frames` frames of a constant level and returns each frame's gain in dB.
std::vector<double> run(geode_dsp* d, size_t frames) {
    std::vector<double> db;
    db.reserve(frames);
    while (db.size() < frames) {
        const size_t n = std::min(nextBlock(), frames - db.size());
        std::vector<float> block(n, kLevel);
        geode_dsp_process(d, block.data(), n, n, 1);
        for (float v : block) db.push_back(level(v));
    }
    return db;
}

bool near(double a, double b, double tolerance) {
    return std::fabs(a - b) <= tolerance;
}

// The largest step between two frames that are both above silence.
double largestStep(const std::vector<double>& db) {
    double largest = 0.0;
    for (size_t i = 1; i < db.size(); ++i) {
        if (db[i] == kNoGain || db[i - 1] == kNoGain) continue;
        largest = std::max(largest, std::fabs(db[i] - db[i - 1]));
    }
    return largest;
}

// GainRamp itself: equal steps, the last frame exactly on target, and nothing after it moves.
void rampIsLinearInDb() {
    geode::audio::GainRamp ramp;
    ramp.start(-20.0f, 1000);
    double previous = 0.0;
    bool even = true;
    for (int k = 1; k <= 1000; ++k) {
        const double db = ramp.next();
        even = even && near(db, -20.0 * k / 1000.0, 1e-9) && near(previous - db, 0.02, 1e-9);
        previous = db;
        if (k == 999) check(db != -20.0, "the ramp is not on target before its last frame");
    }
    check(even, "every frame of a ramp moves by the same step in dB");
    check(previous == -20.0, "the last frame of a ramp is exactly the target");
    check(ramp.next() == -20.0 && !ramp.ramping(), "a finished ramp holds");
    ramp.start(0.0f, 0);
    check(ramp.next() == 0.0, "a ramp of no frames jumps");
    check(geode::audio::clampDb(-std::numeric_limits<float>::infinity()) == GEODE_DSP_SILENCE_DB, "-inf means silence");
    check(geode::audio::clampDb(std::nanf("")) == GEODE_DSP_SILENCE_DB, "NaN means silence");
}

// geode_dsp_ramp_to: 12 dB down over 10 ms is 480 frames of 0.025 dB, whatever the block sizes.
void uiFadeIsSampleAccurate() {
    geode_dsp* d = fresh();
    geode_dsp_ramp_to(d, -12.0f, 10);
    const auto db = run(d, 2000);
    bool onLine = true;
    for (size_t k = 0; k < 480; ++k) onLine = onLine && near(db[k], -12.0 * static_cast<double>(k + 1) / 480.0, 1e-4);
    check(onLine, "the UI fade follows its line frame by frame");
    check(db[478] < -11.97 && db[478] > -11.98, "one frame before the end is one step short");
    check(near(db[479], -12.0, 1e-5) && near(db[1999], -12.0, 1e-5), "the fade lands on its 480th frame and holds");
    check(largestStep(db) <= 12.0 / 480.0 + 1e-4, "no frame steps by more than one ramp increment");
    geode_dsp_destroy(d);
}

// A fade to silence (-inf from Kotlin) ramps to the floor in dB, then multiplies by exactly 0.
void fadeToSilenceEndsAtZero() {
    geode_dsp* d = fresh();
    const size_t frames = 100 * kRate / 1000;  // 100 ms
    geode_dsp_ramp_to(d, -std::numeric_limits<float>::infinity(), 100);
    const auto db = run(d, frames + 500);
    check(db[frames - 2] != kNoGain, "one frame before the end of a fade-out still sounds");
    bool silent = true;
    for (size_t k = frames - 1; k < db.size(); ++k) silent = silent && db[k] == kNoGain;
    check(silent, "a fade-out is exactly silent from its last frame on");
    check(largestStep(db) <= -kSilence / static_cast<double>(frames) + 1e-4, "a fade-out never steps");

    // Back up: from silence to full level, the same line the other way.
    geode_dsp_ramp_to(d, 0.0f, 100);
    const auto up = run(d, frames);
    check(near(up[frames - 1], 0.0, 1e-5), "a fade-in lands on full level");
    check(largestStep(up) <= -kSilence / static_cast<double>(frames) + 1e-4, "a fade-in never steps");
    geode_dsp_destroy(d);
}

// Under a +6 dB ReplayGain a faded-out pause is still exactly silent.
void silenceWinsOverBoost() {
    geode_dsp* d = fresh();
    geode_dsp_set_replaygain(d, 1, 0.0f, 0);
    geode_dsp_set_stream_tags(d, "loud", GEODE_TAG_TRACK_GAIN, 6.0f, 0.0f, 0.0f, 0.0f);
    geode_dsp_begin_stream(d, "loud", 0, 0);
    geode_dsp_ramp_to(d, -std::numeric_limits<float>::infinity(), 0);
    const auto db = run(d, 64);
    check(db[0] == kNoGain && db[63] == kNoGain, "a lane at silence silences the frame whatever the others add");
    geode_dsp_destroy(d);
}

// The ReplayGain rule: mode, the other mode's fallback, preamp and the clip guard.
void replayGainRule() {
    using geode::audio::ReplayGainTags;
    using geode::audio::replayGainDb;
    ReplayGainTags t;
    t.mask = GEODE_TAG_TRACK_GAIN | GEODE_TAG_TRACK_PEAK | GEODE_TAG_ALBUM_GAIN;
    t.trackGainDb = -7.0f;
    t.trackPeak = 0.5f;
    t.albumGainDb = -4.0f;
    check(replayGainDb(t, 0, 3.0f, true) == 0.0f, "off is 0 dB");
    check(replayGainDb(t, 1, 0.0f, true) == -7.0f, "track mode takes the track gain");
    check(replayGainDb(t, 2, 0.0f, true) == -4.0f, "album mode takes the album gain");
    check(replayGainDb(t, 1, 2.0f, false) == -5.0f, "the preamp adds on");
    t.trackGainDb = 9.0f;
    check(near(replayGainDb(t, 1, 0.0f, true), -20.0 * std::log10(0.5), 1e-5), "the clip guard holds the peak at full scale");
    check(replayGainDb(t, 1, 0.0f, false) == 9.0f, "without the clip guard the gain stands");
    t.mask = GEODE_TAG_ALBUM_GAIN;
    check(replayGainDb(t, 1, 0.0f, true) == -4.0f, "a missing track gain falls back to the album gain");
    t.mask = 0;
    check(replayGainDb(t, 2, 5.0f, true) == 0.0f, "an untagged track plays at 0 dB");
}

// #351: each track's gain is on its own first sample, and a track whose tags are late keeps the last gain,
// never 0 dB, then glides to its own.
void replayGainSwitchesOnTheBoundary() {
    geode_dsp* d = fresh();
    geode_dsp_set_replaygain(d, 1, 0.0f, 0);
    geode_dsp_set_stream_tags(d, "file:///a.flac", GEODE_TAG_TRACK_GAIN, -9.0f, 0.0f, 0.0f, 0.0f);
    geode_dsp_set_stream_tags(d, "file:///b.flac", GEODE_TAG_TRACK_GAIN, 3.0f, 0.0f, 0.0f, 0.0f);

    geode_dsp_begin_stream(d, "file:///a.flac", 0, 0);
    const auto a = run(d, 3001);
    check(near(a.front(), -9.0, 1e-4) && near(a.back(), -9.0, 1e-4), "track A plays at its gain from its first sample");

    // A gapless hand-off: end of stream, flush, the next stream.
    geode_dsp_end_stream(d);
    geode_dsp_flush(d);
    geode_dsp_begin_stream(d, "file:///b.flac", 0, 0);
    const auto b = run(d, 1000);
    check(near(b.front(), 3.0, 1e-4), "track B's gain is on B's first sample, not a buffer later");
    check(near(b.back(), 3.0, 1e-4), "and stays");

    // C's tags are still loading at its boundary: it starts at B's gain, not 0 dB.
    geode_dsp_end_stream(d);
    geode_dsp_flush(d);
    geode_dsp_begin_stream(d, "file:///c.flac", 0, 0);
    const auto cHead = run(d, 777);
    check(near(cHead.front(), 3.0, 1e-4) && near(cHead.back(), 3.0, 1e-4), "untagged-yet keeps the last gain, never 0 dB");

    // They arrive mid-track: a glide over 250 ms, linear in dB, no step.
    geode_dsp_set_stream_tags(d, "file:///c.flac", GEODE_TAG_TRACK_GAIN, -2.0f, 0.0f, 0.0f, 0.0f);
    const size_t glide = 250 * kRate / 1000;
    const auto cGlide = run(d, glide + 100);
    check(largestStep(cGlide) <= 5.0 / static_cast<double>(glide) + 1e-4, "late tags glide in without a step");
    check(cGlide[glide - 2] > -2.0 + 1e-6, "the glide is still moving one frame before its end");
    check(near(cGlide[glide - 1], -2.0, 1e-4) && near(cGlide.back(), -2.0, 1e-4), "the glide lands on the new gain");

    // Tags that arrive after the boundary was announced but before its first sample still land on it.
    geode_dsp_end_stream(d);
    geode_dsp_flush(d);
    geode_dsp_begin_stream(d, "file:///d.flac", 0, 0);
    geode_dsp_set_stream_tags(d, "file:///d.flac", GEODE_TAG_TRACK_GAIN, -5.0f, 0.0f, 0.0f, 0.0f);
    const auto dHead = run(d, 10);
    check(near(dHead.front(), -5.0, 1e-4), "tags in before the first sample are on the first sample");

    // A seek inside D (a flush without an end of stream, the same stream) keeps D's gain.
    geode_dsp_flush(d);
    geode_dsp_begin_stream(d, "file:///d.flac", 60000000, 0);
    const auto dSeek = run(d, 10);
    check(near(dSeek.front(), -5.0, 1e-4), "a seek keeps the stream's gain");

    // Turning ReplayGain off glides to 0 dB.
    geode_dsp_set_replaygain(d, 0, 0.0f, 0);
    const auto off = run(d, glide);
    check(near(off.back(), 0.0, 1e-4) && largestStep(off) <= 5.0 / static_cast<double>(glide) + 1e-4, "off glides to 0 dB");
    geode_dsp_destroy(d);
}

// Settings › Playback › Crossfade at 1 s: the last 500 ms of A fade out and the first 500 ms of B fade in, the
// k-th frame before the join and the k-th after it at the same level, and both join frames silent.
void crossfadeHalvesMirror() {
    geode_dsp* d = fresh();
    geode_dsp_set_crossfade(d, 1000);
    geode_dsp_set_follows_on(d, 1);
    const size_t half = static_cast<size_t>(kRate) / 2;
    const size_t length = 2 * static_cast<size_t>(kRate);  // a 2 s track

    geode_dsp_begin_stream(d, "a", 0, 2000000);
    const auto a = run(d, length);
    geode_dsp_end_stream(d);
    geode_dsp_flush(d);
    geode_dsp_begin_stream(d, "b", 0, 2000000);
    const auto b = run(d, length);

    check(near(a[length - half - 1], 0.0, 1e-9), "A is at full level until its last half crossfade");
    check(a[length - 1] == kNoGain, "A's last frame is silent");
    check(b[0] == kNoGain, "B's first frame is silent");
    check(near(b[half], 0.0, 1e-9) && near(b[length / 2], 0.0, 1e-9), "B is at full level after its half crossfade");
    bool mirrored = true;
    bool onLine = true;
    for (size_t k = 1; k < half; ++k) {
        const double out = a[length - 1 - k];
        const double in = b[k];
        mirrored = mirrored && near(out, in, 1e-4);
        onLine = onLine && near(in, kSilence * (1.0 - static_cast<double>(k) / static_cast<double>(half)), 1e-3);
    }
    check(mirrored, "the k-th frame before the join and the k-th after it have the same level");
    check(onLine, "each half is a line in dB from silence to full level");
    // Together the two halves span the crossfade setting exactly: 500 ms faded out, 500 ms faded in.
    // (B's own tail fades out too, since something still follows it; only its first half is the join.)
    size_t faded = 0;
    for (double v : a) faded += v < -1e-9 ? 1 : 0;
    for (size_t k = 0; k < length / 2; ++k) faded += b[k] < -1e-9 ? 1 : 0;
    check(faded == 2 * half, "the two halves sum to the crossfade length");
    std::vector<double> join(a.end() - static_cast<std::ptrdiff_t>(half), a.end());
    join.insert(join.end(), b.begin(), b.begin() + static_cast<std::ptrdiff_t>(half));
    check(largestStep(join) <= -kSilence / static_cast<double>(half) + 1e-4, "no step through the crossfade");

    // A skip (a flush with no end of stream) starts the next track at full level, as gapless did.
    geode_dsp_flush(d);
    geode_dsp_begin_stream(d, "c", 0, 2000000);
    const auto c = run(d, 16);
    check(near(c[0], 0.0, 1e-9), "a skipped-to track starts at full level");

    // Nothing after the last track: it plays out at full level.
    geode_dsp_set_follows_on(d, 0);
    const auto rest = run(d, length - 16);
    check(near(rest.back(), 0.0, 1e-9), "a track nothing follows does not fade out");

    // The setting came on only at the very end, after C's tail had played at full level: D follows on, but
    // has no silence to fade in from, so it holds at full level instead of dipping to its line.
    geode_dsp_set_follows_on(d, 1);
    geode_dsp_end_stream(d);
    geode_dsp_flush(d);
    geode_dsp_begin_stream(d, "d", 0, 2000000);
    const auto dHead = run(d, half);
    bool held = true;
    for (double v : dHead) held = held && near(v, 0.0, 1e-9);
    check(held, "a fade-in after a tail that never faded does not dip");
    geode_dsp_destroy(d);
}

// At 2x a frame is twice the media time, so each half of a 1 s crossfade takes 250 ms of frames.
void crossfadeFollowsSpeed() {
    geode_dsp* d = fresh();
    geode_dsp_set_crossfade(d, 1000);
    geode_dsp_set_follows_on(d, 1);
    geode_dsp_set_speed(d, 2.0f);
    const size_t quarter = static_cast<size_t>(kRate) / 4;
    geode_dsp_begin_stream(d, "a", 0, 1000000);
    const auto a = run(d, 2 * quarter);  // 1 s of media at 2x is 500 ms of frames
    geode_dsp_end_stream(d);
    geode_dsp_flush(d);
    // B is twice as long as A: in a track only as long as the crossfade the fade-in's last frame is
    // also the first of its own fade-out, so it would not be back at full level there.
    geode_dsp_begin_stream(d, "b", 0, 2000000);
    const auto b = run(d, 2 * quarter);
    check(near(a[quarter - 1], 0.0, 1e-9) && a[quarter] < 0.0 && a.back() == kNoGain, "the fade-out is 250 ms of frames at 2x");
    check(b[0] == kNoGain && b[quarter - 1] < 0.0 && near(b[quarter], 0.0, 1e-9), "the fade-in is 250 ms of frames at 2x");
    geode_dsp_destroy(d);
}

// Repeat one: the same stream again from its start is a new play of it, and fades in.
void repeatOneFadesIn() {
    geode_dsp* d = fresh();
    geode_dsp_set_crossfade(d, 200);
    geode_dsp_set_follows_on(d, 1);
    geode_dsp_begin_stream(d, "loop", 0, 1000000);
    run(d, static_cast<size_t>(kRate));
    geode_dsp_end_stream(d);
    geode_dsp_flush(d);
    geode_dsp_begin_stream(d, "loop", 0, 1000000);
    const auto again = run(d, 10);
    check(again[0] == kNoGain, "a repeat of the same track fades in");
    geode_dsp_destroy(d);
}

// The sleep timer: nothing until its last 3 s, then a line in dB to silence that ends with it; a cancel
// comes back up in 50 ms without a step.
void sleepFadeEndsWithTheTimer() {
    geode_dsp* d = fresh();
    check(geode_dsp_sleep_fade(d, 65000) == 62000, "before its window the fade asks to hear at its start");
    check(near(run(d, 100).back(), 0.0, 1e-9), "no fade before the window");
    check(geode_dsp_sleep_fade(d, 3000) == 3000, "inside the window it asks for the end");
    const size_t frames = 3 * static_cast<size_t>(kRate);
    const auto db = run(d, frames + 10);
    bool onLine = true;
    for (size_t k = 0; k + 1 < frames; ++k) {
        onLine = onLine && near(db[k], kSilence * static_cast<double>(k + 1) / static_cast<double>(frames), 1e-3);
    }
    check(onLine, "the sleep fade is a line in dB over the last 3 s");
    check(db[frames - 2] != kNoGain && db[frames - 1] == kNoGain && db.back() == kNoGain, "it is silent as the timer ends");
    // Reports inside the window do not re-aim it.
    geode_dsp_sleep_fade(d, 1200);
    check(run(d, 10).back() == kNoGain, "a later report leaves the finished fade alone");

    geode_dsp_sleep_fade(d, -1);
    const size_t recover = 50 * kRate / 1000;
    const auto back = run(d, recover + 10);
    check(near(back[recover - 1], 0.0, 1e-6), "a cancel brings it back in 50 ms");
    check(largestStep(back) <= -kSilence / static_cast<double>(recover) + 1e-4, "without a step");
    geode_dsp_destroy(d);
}

// With nothing to do the gain changes no bit, in float and in 16-bit.
void unityIsBitIdentical() {
    geode_dsp* d = fresh();
    std::vector<float> f(1024);
    for (size_t i = 0; i < f.size(); ++i) f[i] = 0.9f * std::sin(0.01f * static_cast<float>(i));
    const auto fOriginal = f;
    geode_dsp_process(d, f.data(), f.size(), f.size(), 1);
    check(f == fOriginal, "float at 0 dB is bit-identical");

    std::vector<int16_t> s(5000);  // more than one native chunk
    for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<int16_t>(static_cast<int>(i * 37u % 65536u) - 32768);
    const auto sOriginal = s;
    geode_dsp_process_pcm16(d, s.data(), s.size(), s.size(), 1);
    check(s == sOriginal, "16-bit at 0 dB is bit-identical, across chunks");

    // And 16-bit hears the same gain as float.
    geode_dsp_ramp_to(d, -6.0f, 0);
    std::vector<int16_t> q(64, 16384);
    geode_dsp_process_pcm16(d, q.data(), q.size(), q.size(), 1);
    check(q.front() == q.back() && q.front() == static_cast<int16_t>(16384.0 * std::pow(10.0, -6.0 / 20.0)),
          "16-bit takes the gain and truncates like the Kotlin conversion did");
    geode_dsp_destroy(d);
}

}  // namespace

int main() {
    rampIsLinearInDb();
    uiFadeIsSampleAccurate();
    fadeToSilenceEndsAtZero();
    silenceWinsOverBoost();
    replayGainRule();
    replayGainSwitchesOnTheBoundary();
    crossfadeHalvesMirror();
    crossfadeFollowsSpeed();
    repeatOneFadesIn();
    sleepFadeEndsWithTheTimer();
    unityIsBitIdentical();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("gain_ramp_test: all passed\n");
    return EXIT_SUCCESS;
}
