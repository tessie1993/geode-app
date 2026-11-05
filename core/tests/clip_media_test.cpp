// Host-side checks for the Studio clip maths and the export's PCM conversions: viz/ClipGrade (the colour
// matrix behind Media3's RgbMatrix effect), util/CubeLut (.cube parsing and the gamma cube) and
// audio/dsp/PcmConvert (downmix, float <-> s16), each through its C ABI too. Built and run by CI
// (.github/workflows/android.yml, "Native export planning tests"). The behaviour pinned is the Kotlin
// gradeMatrix (export/KeyframedClipEffects.kt), CubeLut (export/CubeLut.kt) and AudioTranscoder's loops.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "api/geode_api.h"
#include "audio/dsp/PcmConvert.hpp"
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

    // Kills: brightness applied after contrast instead of before (mul's operands swapped). Media3's Contrast on
    // mid grey lifted by 0.1: ((0.5 + 0.1) - 0.5) * 1.5 / 0.5001 + 0.5 = 0.79994; the other order gives 0.6.
    geode::viz::clipGradeMatrix(0.1f, 0.5f, 0.0f, 0.0f, m);
    apply(m, grey, o);
    check(near(o[0], 0.79994f, 1e-4f) && near(o[1], 0.79994f, 1e-4f) && near(o[2], 0.79994f, 1e-4f),
          "brightness lifts before contrast stretches");

    // Kills: the "+ s" on any diagonal of the saturation rows dropped (invisible at s = 0, full desaturation),
    // or 1 + p/100 turned to 1 - p/100. +100 % is s = 2: diagonal 2 - luma, off-diagonal -luma (Rec. 709).
    geode::viz::clipGradeMatrix(0.0f, 0.0f, 100.0f, 0.0f, m);
    check(near(m[0], 1.7874f, 1e-3f) && near(m[5], 1.2848f, 1e-3f) && near(m[10], 1.9278f, 1e-3f) &&
              near(m[4], -0.7152f, 1e-3f) && near(m[8], -0.0722f, 1e-3f) && near(m[1], -0.2126f, 1e-3f),
          "double saturation pushes each channel away from the Rec. 709 luma");

    // Kills: any coefficient of the saturation rows mistyped or moved: saturation keeps Rec. 709 luma for every
    // colour and amount (the matrix is s*I + (1 - s) * 1 * luma^T, and luma . 1 = 1).
    bool lumaKept = true;
    for (float sat : {-100.0f, -40.0f, 35.0f, 100.0f}) {
        geode::viz::clipGradeMatrix(0.0f, 0.0f, sat, 0.0f, m);
        for (int c = 0; c < 27; ++c) {
            const float rgb[3] = {static_cast<float>(c % 3) * 0.5f, static_cast<float>((c / 3) % 3) * 0.5f,
                                  static_cast<float>(c / 9) * 0.5f};
            apply(m, rgb, o);
            const float before = 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
            const float after = 0.2126f * o[0] + 0.7152f * o[1] + 0.0722f * o[2];
            if (!near(before, after, 1e-3f)) lumaKept = false;
        }
    }
    check(lumaKept, "saturation never changes a colour's luma");

    // Kills: a sin term's sign flipped (hue 180 has sin = 0 and white has every row summing to 1, so neither
    // saw it), degrees not turned into radians, or rows() storing a row as a column. Hue 90 per the W3C
    // feColorMatrix hueRotate coefficients with cos = 0, sin = 1; R takes B, so m[8] (R from B) is 1.
    geode::viz::clipGradeMatrix(0.0f, 0.0f, 0.0f, 90.0f, m);
    check(near(m[0], 0.0f, 1e-3f) && near(m[4], 0.0f, 1e-3f) && near(m[8], 1.0f, 1e-3f), "hue 90: red row");
    check(near(m[1], 0.356f, 1e-3f) && near(m[5], 0.855f, 1e-3f) && near(m[9], -0.211f, 1e-3f), "hue 90: green row");
    check(near(m[2], -0.574f, 1e-3f) && near(m[6], 1.430f, 1e-3f) && near(m[10], 0.144f, 1e-3f), "hue 90: blue row");

    // Kills: hue coefficients mistyped: the hue turn keeps luminance (weights 0.213, 0.715, 0.072) at any angle.
    bool hueLumaKept = true;
    for (float deg : {-45.0f, 30.0f, 200.0f, 333.0f}) {
        geode::viz::clipGradeMatrix(0.0f, 0.0f, 0.0f, deg, m);
        for (int c = 0; c < 27; ++c) {
            const float rgb[3] = {static_cast<float>(c % 3) * 0.5f, static_cast<float>((c / 3) % 3) * 0.5f,
                                  static_cast<float>(c / 9) * 0.5f};
            apply(m, rgb, o);
            const float before = 0.213f * rgb[0] + 0.715f * rgb[1] + 0.072f * rgb[2];
            const float after = 0.213f * o[0] + 0.715f * o[1] + 0.072f * o[2];
            if (!near(before, after, 2e-3f)) hueLumaKept = false;
        }
    }
    check(hueLumaKept, "a hue turn never changes a colour's luminance");

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

}  // namespace

int main() {
    grade();
    cubeLut();
    pcm();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("clip_media_test: all passed\n");
    return 0;
}
