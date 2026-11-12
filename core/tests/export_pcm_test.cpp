// Host-side checks for the export's PCM conversions: audio/dsp/PcmConvert (downmix, float <-> s16) and
// audio/dsp/PcmTiming (AAC rate, s16 durations, the start skip and trim), each through its C ABI too, which
// is what AudioTranscoder and PcmResampler call. Built and run by CI (native-host-tests.yml, "Native export
// planning tests"). The behaviour pinned is AudioTranscoder's loops. (This was clip_media_test; its Studio
// clip grade and .cube cases went with the Studio project render, ADR 0007.)
#include <cmath>
#include <random>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "api/geode_api.h"
#include "audio/dsp/PcmConvert.hpp"
#include "audio/dsp/PcmTiming.hpp"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void pcm() {
    using namespace geode::audio;
    // 5.1: L, R, C, LFE, Ls, Rs. The rest's mean, halved, goes to each side.
    const int16_t six[6] = {1000, -1000, 600, 200, 100, 100};
    int16_t two[2] = {};
    check(downmixS16(six, 1, 6, 2, two) == 1 && two[0] == 1125 && two[1] == -875, "5.1 folds into stereo");
    const int16_t loud[6] = {32767, 0, 32767, 32767, 32767, 32767};
    check(downmixS16(loud, 1, 6, 2, two) == 1 && two[0] == 32767 && two[1] == 16383, "the fold saturates");
    const int16_t st[4] = {100, 201, -3, -4};
    int16_t mono[2] = {};
    check(downmixS16(st, 2, 2, 1, mono) == 2 && mono[0] == 150 && mono[1] == -3, "mono is the truncated mean");
    check(downmixS16(st, 2, 2, 2, two) == 0, "stereo to stereo is not a downmix");
    check(downmixS16(six, 1, 6, 3, two) == 0, "only mono and stereo targets");

    const float f[5] = {1.5f, -1.0f, 0.5f, -0.5f, NAN};
    int16_t s[5] = {};
    floatToS16(f, 5, s);
    check(s[0] == 32767 && s[1] == -32767 && s[2] == 16383 && s[3] == -16383 && s[4] == 0, "float to s16 truncates; NaN is 0");
    const int16_t back[2] = {-32768, 16384};
    float g[2] = {};
    s16ToFloat(back, 2, g);
    check(g[0] == -1.0f && g[1] == 0.5f, "s16 to float by 1/32768");

    int16_t abi[2] = {};
    check(geode_pcm_downmix_s16(six, 1, 6, 2, abi) == 1 && abi[0] == 1125, "ABI downmix");
}

// Parity with the Kotlin it replaced (PcmResampler.aacRateFor / pcmBytesToUs, AudioTranscoder's AIFF skip and
// AAC end), plus the start trim, whose straddling case is the bug #421 fixes.
void pcmTiming() {
    using namespace geode::audio;
    // The rate policy (the old PcmResamplerTest cases).
    check(aacRateFor(22050) == 22050 && aacRateFor(44100) == 44100 && aacRateFor(48000) == 48000, "AAC keeps rates to 48k");
    check(aacRateFor(88200) == 44100 && aacRateFor(176400) == 44100, "the 44.1k family steps down to 44.1k");
    check(aacRateFor(96000) == 48000 && aacRateFor(192000) == 48000 && aacRateFor(64000) == 48000, "the rest to 48k");
    check(geode_aac_rate_for(96000) == 48000, "ABI AAC rate");
    check(kMixRateHz == 48000 && geode_mix_rate() == kMixRateHz, "a mix is brought to 48k, the rate PcmResampler held");
    check(aacRateFor(kMixRateHz) == kMixRateHz, "and the encoder keeps it");

    // One second of stereo s16 at each rate; bytes are timed at the rate they are at.
    check(s16BytesToUs(44100LL * 4, 44100, 2) == 1000000 && s16BytesToUs(48000LL * 4, 48000, 2) == 1000000 &&
              s16BytesToUs(96000LL * 4, 96000, 2) == 1000000,
          "a second of stereo s16 is 1e6 us");
    check(s16BytesToUs(48000LL * 4, 96000, 2) == 500000, "timing 48k bytes at 96k halves them");
    check(s16BytesToUs(3, 48000, 1) == 31, "floor, as Kotlin's Long division");
    check(s16BytesToUs(100, 0, 2) == 0 && s16BytesToUs(100, 48000, 0) == 0, "bad layouts are 0");
    check(geode_pcm_s16_bytes_to_us(192000, 48000, 2) == 1000000, "ABI bytes to us");

    // AIFF start skip: Kotlin's startMs * rate / 1000 * channels, from µs.
    check(samplesBeforeUs(1500 * 1000LL, 44100, 2) == 1500LL * 44100 / 1000 * 2, "start skip matches the Kotlin");
    check(samplesBeforeUs(1000, 44100, 6) == 44 * 6, "1 ms at 44.1k is 44 whole frames");
    check(samplesBeforeUs(0, 48000, 2) == 0 && samplesBeforeUs(-5, 48000, 2) == 0, "nothing before 0");
    check(geode_pcm_samples_before_us(1000000, 48000, 2) == 96000, "ABI samples before");

    // AAC end: one 1024-sample frame past the last pts, integer division.
    check(aacEndUs(1000000, 48000) == 1000000 + 21333 && aacEndUs(0, 44100) == 23219, "AAC frame length per rate");
    check(aacEndUs(500, 0) == 500, "a bad rate adds nothing");
    check(geode_aac_end_us(0, 48000) == 21333, "ABI AAC end");

    // Start trim at 48 kHz stereo (4 bytes a frame), 1024-frame buffers (4096 bytes).
    const int64_t start = 1000000;
    auto t = trimS16(0, 0, 4096, 48000, 2);
    check(t.skipBytes == 0 && t.ptsUs == 0, "no start: everything, at its own pts");
    t = trimS16(start, 1500000, 4096, 48000, 2);
    check(t.skipBytes == 0 && t.ptsUs == 500000, "a buffer after the start keeps all, at pts - start");
    t = trimS16(start, start, 4096, 48000, 2);
    check(t.skipBytes == 0 && t.ptsUs == 0, "a buffer on the start keeps all, at 0");
    // The bug: 10 ms early, 21.3 ms long. Kotlin dropped it whole (pts + 1 ms < start); 480 frames precede
    // the start, the other 544 are kept and stamped 0.
    t = trimS16(start, start - 10000, 4096, 48000, 2);
    check(t.skipBytes == 480 * 4 && t.ptsUs == 0, "a straddling buffer keeps the frames from the start");
    t = trimS16(start, start - 10000, 4096, 44100, 2);
    check(t.skipBytes == 441 * 4 && t.ptsUs == 0, "straddling at 44.1k");
    t = trimS16(start, start - 1000, 4096, 44100, 1);
    check(t.skipBytes == 44 * 2 && t.ptsUs == 0, "1 ms early at 44.1k mono skips 44 frames (44.1 rounded)");
    t = trimS16(start, start - 5, 4096, 48000, 2);
    check(t.skipBytes == 0 && t.ptsUs == 0, "under half a frame early skips nothing");
    t = trimS16(start, start - 100000, 4096, 48000, 2);
    check(t.skipBytes == 4096, "a buffer that ends before the start is dropped whole");
    t = trimS16(start, start - 21333, 4096, 48000, 2);
    check(t.skipBytes == 4096, "a buffer that ends at the start is dropped whole");
    t = trimS16(start, start - 10000, 4098, 48000, 2);
    check(t.skipBytes == 480 * 4, "the skip is whole frames even with a stray tail");
    t = trimS16(0, -2000, 4096, 48000, 2);
    check(t.skipBytes == 96 * 4 && t.ptsUs == 0, "a negative pts (priming) trims to 0");
    t = trimS16(start, start - 10000, 4096, 0, 2);
    check(t.skipBytes == 4096, "an early buffer without a rate keeps nothing");
    t = trimS16(start, start - 10000, 0, 48000, 2);
    check(t.skipBytes == 0, "an empty buffer: skip == bytes == 0, nothing kept");
    int64_t pts = -1;
    check(geode_pcm_trim_s16(start, start - 10000, 4096, 48000, 2, &pts) == 1920 && pts == 0, "ABI trim");
    check(geode_pcm_trim_s16(start, 1500000, 4096, 48000, 2, nullptr) == 0, "ABI trim with no pts out");
}

// ── #506 follow-up: PCM timing through its seams (audio/dsp/PcmTiming and the geode_* ABI). Expected values are
// hand-worked from the sample clock (a frame lasts 1e6 / rate us; 1024 frames at 48 kHz are 64000 / 3 us) or
// from the deleted Kotlin (startMs * rate / 1000 * channels, Long division truncating toward zero), never from
// PcmTiming's own formulas. Each block names the mutants it kills.

int64_t absI64(int64_t v) { return v < 0 ? -v : v; }

// Encoder pts from the bytes written so far: exact on whole seconds after ten hours or a day, never ahead of
// the sample clock and under one us behind it at every buffer boundary of an hour, and a rate change times
// each run at its own rate.
// Kills: the floor turned to nearest or ceil (a stamp ahead of its samples); the `<= 0` guards turned `== 0`
// (a negative rate or channel count gives a negative duration, a negative AAC rate an end before the last pts).
void pcmClockFromBytes() {
    using namespace geode::audio;
    // Ten hours of 44.1 kHz stereo s16 are 44100 * 4 * 36000 bytes and 36 000 s.
    check(s16BytesToUs(44100LL * 4 * 36000, 44100, 2) == 36000000000LL, "ten hours of bytes stamp exactly 36e9 us");
    check(geode_pcm_s16_bytes_to_us(192000LL * 16 * 86400, 192000, 8) == 86400000000LL, "ABI: a day of 192k 7.1 is 86400 s");

    // 1024-frame buffers of 48 kHz stereo (4096 bytes): buffer n ends at n * 64000 / 3 us.
    bool neverAhead = true;
    bool underOneUsBehind = true;
    for (int64_t n = 1; n <= 168750; ++n) {
        const int64_t pts = s16BytesToUs(n * 4096, 48000, 2);
        if (pts * 3 > n * 64000) neverAhead = false;
        if (n * 64000 - pts * 3 >= 3) underOneUsBehind = false;
    }
    check(neverAhead, "a buffer boundary's stamp is never ahead of its samples");
    check(underOneUsBehind, "a buffer boundary's stamp is under 1 us behind its samples: no drift over an hour");
    check(s16BytesToUs(168750LL * 4096, 48000, 2) == 3600000000LL, "168750 buffers of 1024 frames at 48k are one hour");

    // A rate change mid-stream: 1 s at 96 kHz, then 2 s at 48 kHz, each run timed at the rate it is at.
    const int64_t first = s16BytesToUs(96000LL * 4, 96000, 2);
    check(first == 1000000 && first + s16BytesToUs(2 * 48000LL * 4, 48000, 2) == 3000000, "a rate change: 1 s + 2 s");

    check(s16BytesToUs(4096, -48000, 2) == 0 && s16BytesToUs(4096, 48000, -2) == 0, "a negative rate or channel count lasts 0");
    check(aacEndUs(500, -48000) == 500, "a negative AAC rate adds nothing, so the end never precedes the last pts");
}

// The AIFF start skip: interleaved samples, so always whole frames (a cut mid-frame swaps left and right for
// the rest of the export), equal to the deleted Kotlin's startMs * rate / 1000 * channels.
// Kills: the floor taken over samples instead of frames (us * rate * channels / 1e6), rounding instead of the
// floor, and the removed rate or channel guard (a negative skip).
void pcmStartSkipWholeFrames() {
    using namespace geode::audio;
    // 9 ms at 44.1 kHz is 396.9 frames: 396 whole frames, 792 samples (not 793, not 794).
    check(samplesBeforeUs(9000, 44100, 2) == 792, "9 ms at 44.1k stereo skips 396 whole frames");
    check(geode_pcm_samples_before_us(9000, 44100, 6) == 396 * 6, "ABI: 9 ms at 44.1k 5.1 skips 396 whole frames");

    const int rates[] = {8000, 11025, 22050, 44100, 48000, 88200, 96000, 176400, 192000};
    std::mt19937_64 rng(506);
    bool parity = true;
    bool whole = true;
    for (int i = 0; i < 20000; ++i) {
        const int64_t ms = static_cast<int64_t>(rng() % (3ULL * 3600 * 1000));   // anywhere in three hours
        const int rate = rates[rng() % 9];
        const int channels = 1 + static_cast<int>(rng() % 8);
        const int64_t got = samplesBeforeUs(ms * 1000, rate, channels);
        if (got != ms * rate / 1000 * channels) parity = false;   // the deleted Kotlin, verbatim
        if (got % channels != 0) whole = false;
    }
    check(parity, "the start skip equals the Kotlin's startMs * rate / 1000 * channels");
    check(whole, "the start skip is always whole frames");
    check(samplesBeforeUs(1000000, -48000, 2) == 0 && samplesBeforeUs(1000000, 48000, -2) == 0,
          "a negative rate or channel count skips nothing");
}

// The part-render start trim, case by case.
// Kills: `lead <= 0` turned `< 0` (a buffer on the start with no usable layout would be dropped); `>=` turned
// `>` on the whole-buffer test (a buffer ending on the start with a stray tail would keep 2 bytes); the
// nearest frame turned floor or ceil; the removed channel, rate and negative-bytes guards; the ABI writing a
// negated pts.
void pcmTrimEdges() {
    using namespace geode::audio;
    const int64_t start = 1000000;
    auto t = trimS16(start, start, 4096, 0, 2);
    check(t.skipBytes == 0 && t.ptsUs == 0, "a buffer on the start keeps all even with no rate to cut at");
    t = trimS16(start, start, 4096, 48000, 0);
    check(t.skipBytes == 0 && t.ptsUs == 0, "a buffer on the start keeps all even with no channels");
    // 21333 us at 48 kHz is 1023.98 frames: rounds to all 1024, so the stray 2 bytes go too.
    t = trimS16(start, start - 21333, 4098, 48000, 2);
    check(t.skipBytes == 4098, "a buffer that ends on the start is dropped whole, stray tail included");
    // 15 us at 48 kHz is 0.72 of a frame: one frame (4 bytes); 10 us is 0.48: none.
    check(trimS16(start, start - 15, 4096, 48000, 2).skipBytes == 4, "0.72 of a frame early rounds to one frame");
    check(trimS16(start, start - 10, 4096, 48000, 2).skipBytes == 0, "0.48 of a frame early rounds to none");
    check(trimS16(start, start - 10000, 4096, 48000, 0).skipBytes == 4096, "an early buffer with no channels keeps nothing");
    check(trimS16(start, start - 10000, 4096, -48000, 2).skipBytes == 4096, "an early buffer with a negative rate keeps nothing");
    t = trimS16(start, start - 10000, -8, 48000, 2);
    check(t.skipBytes == 0, "a negative size never gives a negative skip");
    int64_t pts = -1;
    check(geode_pcm_trim_s16(start, 1500000, 4096, 48000, 2, &pts) == 0 && pts == 500000, "ABI trim: pts - start out");
}

// Contiguous decoder buffers from a seek before the start, as AudioTranscoder sees them: every kept part is
// stamped where its first frame truly lies (within half a frame), a dropped buffer ends at most half a frame
// past the start, every skip is whole frames inside the buffer, and the frames kept add up to all of them
// minus those before the start (within half a frame): each frame from the start on is kept exactly once.
// Kills: the nearest frame turned floor or ceil, the trim's pts sign, frame bytes counted as channels.
void pcmTrimKeepsEachFrameOnce() {
    using namespace geode::audio;
    struct Layout {
        int rate;
        int64_t frames;   // per decoder buffer, chosen so a buffer lasts a whole number of us
        int64_t bufUs;
    };
    const Layout layouts[] = {{48000, 4800, 100000}, {44100, 4410, 100000}, {96000, 960, 10000}, {22050, 2205, 100000}};
    std::mt19937_64 rng(421);
    bool stampsTrue = true;
    bool droppedOnlyBefore = true;
    bool wholeFrames = true;
    bool keptOnce = true;
    for (int iter = 0; iter < 4000; ++iter) {
        const Layout& l = layouts[rng() % 4];
        const int channels = 1 + static_cast<int>(rng() % 2);
        const int64_t frameBytes = 2 * channels;
        const int64_t bytes = l.frames * frameBytes;
        const int64_t start = static_cast<int64_t>(rng() % 3600000000ULL);   // anywhere in the first hour
        const int64_t lead = 1 + static_cast<int64_t>(rng() % static_cast<uint64_t>(3 * l.bufUs));
        int64_t keptFrames = 0;
        for (int64_t i = 0; i < 5; ++i) {
            const int64_t bufPts = start - lead + i * l.bufUs;
            const PcmTrim t = trimS16(start, bufPts, bytes, l.rate, channels);
            if (t.skipBytes < 0 || t.skipBytes > bytes || t.skipBytes % frameBytes != 0) wholeFrames = false;
            if (t.skipBytes == bytes) {
                if ((bufPts - start) * l.rate + l.frames * 1000000 > 500000) droppedOnlyBefore = false;
                continue;
            }
            const int64_t skipFrames = t.skipBytes / frameBytes;
            // (true source time of the first kept frame - its stamp on the render clock) * rate, in us * Hz.
            if (absI64((bufPts - start - t.ptsUs) * l.rate + skipFrames * 1000000) > 500000) stampsTrue = false;
            keptFrames += l.frames - skipFrames;
        }
        if (absI64((5 * l.frames - keptFrames) * 1000000 - lead * l.rate) > 500000) keptOnce = false;
    }
    check(stampsTrue, "each kept part is stamped where its first frame lies, within half a frame");
    check(droppedOnlyBefore, "a dropped buffer ends at most half a frame past the start");
    check(wholeFrames, "every skip is whole frames inside the buffer");
    check(keptOnce, "the frames kept are all of them minus those before the start");
}

}  // namespace

int main() {
    pcm();
    pcmTiming();
    pcmClockFromBytes();
    pcmStartSkipWholeFrames();
    pcmTrimEdges();
    pcmTrimKeepsEachFrameOnce();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("export_pcm_test: all passed\n");
    return 0;
}
