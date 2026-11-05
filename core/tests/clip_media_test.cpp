// Host-side checks for the Studio clip maths and the export's PCM conversions: viz/ClipGrade (the colour
// matrix behind Media3's RgbMatrix effect), util/CubeLut (.cube parsing and the gamma cube) and
// audio/dsp/PcmConvert (downmix, float <-> s16) and audio/dsp/PcmTiming (AAC rate, s16 durations, the start
// skip and trim), each through its C ABI too. Built and run by CI
// (.github/workflows/android.yml, "Native export planning tests"). The behaviour pinned is the Kotlin
// gradeMatrix (export/KeyframedClipEffects.kt), CubeLut (export/CubeLut.kt) and AudioTranscoder's loops.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "api/geode_api.h"
#include "audio/dsp/PcmConvert.hpp"
#include "audio/dsp/PcmTiming.hpp"
#include "util/CubeLut.hpp"
#include "viz/ClipGrade.hpp"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

// Column-major 4x4 times (r, g, b, 1).
void apply(const float m[16], const float in[3], float out[3]) {
    for (int r = 0; r < 3; ++r) out[r] = m[r] * in[0] + m[4 + r] * in[1] + m[8 + r] * in[2] + m[12 + r];
}

void grade() {
    float m[16];
    geode::viz::clipGradeMatrix(0.0f, 0.0f, 0.0f, 0.0f, m);
    bool identity = true;
    for (int i = 0; i < 16; ++i) {
        const float want = (i % 5 == 0) ? 1.0f : 0.0f;
        if (!near(m[i], want, 1e-3f)) identity = false;
    }
    check(identity, "a neutral grade is (almost) identity: Media3's contrast keeps its 1.0001");

    geode::viz::clipGradeMatrix(0.1f, 0.0f, 0.0f, 0.0f, m);
    check(near(m[12], 0.1f, 1e-3f) && near(m[13], 0.1f, 1e-3f) && near(m[14], 0.1f, 1e-3f), "brightness is an offset");

    geode::viz::clipGradeMatrix(0.0f, 0.5f, 0.0f, 0.0f, m);
    const float grey[3] = {0.5f, 0.5f, 0.5f};
    float o[3];
    apply(m, grey, o);
    check(near(o[0], 0.5f, 1e-4f) && near(m[0], 1.5f / 0.5001f, 1e-4f), "contrast pivots on mid grey");

    geode::viz::clipGradeMatrix(0.0f, 0.0f, -100.0f, 0.0f, m);
    check(near(m[0], 0.2126f, 1e-3f) && near(m[4], 0.7152f, 1e-3f) && near(m[8], 0.0722f, 1e-3f) && near(m[1], 0.2126f, 1e-3f),
          "full desaturation is Rec. 709 luma on every channel");

    geode::viz::clipGradeMatrix(0.0f, 0.0f, 0.0f, 73.0f, m);
    const float white[3] = {1.0f, 1.0f, 1.0f};
    apply(m, white, o);
    check(near(o[0], 1.0f, 2e-3f) && near(o[1], 1.0f, 2e-3f) && near(o[2], 1.0f, 2e-3f), "a hue turn keeps white white");
    geode::viz::clipGradeMatrix(0.0f, 0.0f, 0.0f, 180.0f, m);
    check(near(m[0], 0.213f - 0.787f, 1e-3f), "hue 180");

    float abi[16];
    geode_clip_grade_matrix(0.0f, 0.0f, 0.0f, 180.0f, abi);
    check(std::memcmp(abi, m, sizeof m) == 0, "ABI grade matches");
}

const char* kIdentity3d =
    "# a comment\n"
    "TITLE \"identity\"\n"
    "lut_3d_size 2\r\n"
    "0 0 0\n1 0 0\n0 1 0\n1 1 0\n"
    "0 0 1\n1 0 1\n0 1 1\n1 1 1  # trailing comment\n";

void cubeLut() {
    using geode::util::CubeLut;
    const auto id = CubeLut::parse(kIdentity3d);
    check(id && id->size() == 2, "a 2^3 cube, keyword in any case, CRLF and comments tolerated");
    if (id) {
        check(id->at(1, 0, 0) == 0xFFFF0000u && id->at(0, 1, 0) == 0xFF00FF00u && id->at(0, 0, 1) == 0xFF0000FFu,
              "red runs fastest in the rows");
        check(id->at(1, 1, 1) == 0xFFFFFFFFu && id->at(0, 0, 0) == 0xFF000000u, "corners");
    }
    const std::string domain = std::string("DOMAIN_MIN 0 0 0\nDOMAIN_MAX 2 2 2\nLUT_3D_SIZE 2\n") +
                               "0 0 0\n2 0 0\n0 2 0\n2 2 0\n0 0 2\n2 0 2\n0 2 2\n1 1 1\n";
    const auto scaled = CubeLut::parse(domain);
    check(scaled && scaled->at(1, 0, 0) == 0xFFFF0000u, "values are normalised by the domain");
    check(scaled && scaled->at(1, 1, 1) == 0xFF808080u, "1 of 0..2 is 127.5, rounded half up to 128");

    const auto oneD = CubeLut::parse("LUT_1D_SIZE 2\n0 0 0\n1 1 1\n");
    check(oneD && oneD->size() == 2 && oneD->at(1, 0, 1) == 0xFFFF00FFu, "a 1D table expands to a cube");

    check(!CubeLut::parse("LUT_3D_SIZE 2\n0 0 0\n1 1 1\n"), "too few rows");
    check(!CubeLut::parse("LUT_3D_SIZE 1\n0 0 0\n"), "a size under 2");
    check(!CubeLut::parse("LUT_3D_SIZE 66\n"), "a size over 65");
    check(!CubeLut::parse("hello world\n"), "not a cube");

    const CubeLut flat = CubeLut::gamma(1.0f, 1.0f, 1.0f, 2);
    check(flat.at(1, 1, 1) == 0xFFFFFFFFu && flat.at(0, 0, 0) == 0xFF000000u, "gamma 1 is identity");
    const CubeLut lift = CubeLut::gamma(2.0f, 2.0f, 2.0f, 3);
    check(lift.at(1, 1, 1) == 0xFFB4B4B4u, "gamma 2 lifts 0.5 to 0.707 (180)");
    check(CubeLut::gamma(1.0f, 1.0f, 1.0f).size() == CubeLut::kGammaSize, "default gamma cube size");

    geode_cube_lut* lut = geode_cube_lut_parse(kIdentity3d, std::strlen(kIdentity3d));
    check(lut && geode_cube_lut_size(lut) == 2 && geode_cube_lut_argb(lut)[(1 * 2 + 0) * 2 + 0] == 0xFFFF0000u, "ABI cube");
    geode_cube_lut_destroy(lut);
    check(geode_cube_lut_parse("nope", 4) == nullptr, "ABI rejects a bad cube");
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

}  // namespace

int main() {
    grade();
    cubeLut();
    pcm();
    pcmTiming();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("clip_media_test: all passed\n");
    return 0;
}
