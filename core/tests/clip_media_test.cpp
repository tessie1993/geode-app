// Host-side checks for the Studio clip maths and the export's PCM conversions: viz/ClipGrade (the colour
// matrix behind Media3's RgbMatrix effect), util/CubeLut (.cube parsing and the gamma cube) and
// audio/dsp/PcmConvert (downmix, float <-> s16), each through its C ABI too. Built and run by CI
// (.github/workflows/android.yml, "Native export planning tests"). The behaviour pinned is the Kotlin
// gradeMatrix (export/KeyframedClipEffects.kt), CubeLut (export/CubeLut.kt) and AudioTranscoder's loops.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
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

// ── #415 follow-up: .cube and PCM boundaries, parity with the deleted Kotlin (export/CubeLut.kt,
// AudioTranscoder's loops, LoudnessMeter.readSigned16). Each block names the mutants it kills; expected
// values are hand-worked (v * 255 rounded half up, as Kotlin's roundToInt; Int division truncating toward
// zero in both languages; Float.toInt / static_cast<int> discarding the fraction).

std::string cubeOf(int size, const char* row) {
    std::string text = "LUT_3D_SIZE " + std::to_string(size) + "\n";
    const long rows = static_cast<long>(size) * size * size;
    for (long i = 0; i < rows; ++i) {
        text += row;
        text += '\n';
    }
    return text;
}

// Kills: the 3D and 1D size bounds (2..65) loosened, the 1D row-count check, 1D channels read from the
// wrong row, the 3D branch losing precedence, the domain span guard (d > 0) as >= or dropped, rounding by
// truncation, and the last line lost when the file has no trailing newline.
void cubeLutBoundaries() {
    using geode::util::CubeLut;
    const std::string noNewline = "LUT_3D_SIZE 2\n0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1";
    const auto tail = CubeLut::parse(noNewline);
    check(tail && tail->at(1, 1, 1) == 0xFFFFFFFFu, "the last row counts without a trailing newline");
    check(!CubeLut::parse("") && !CubeLut::parse("# only a comment\n"), "nothing is not a cube");

    const auto biggest = CubeLut::parse(cubeOf(65, "0 0 0"));
    check(biggest && biggest->size() == 65 && biggest->argb().size() == 274'625u, "a 65^3 cube is the largest kept");
    check(!CubeLut::parse(cubeOf(66, "0 0 0")), "a 66^3 cube with every row is still refused");

    const auto ramp = CubeLut::parse("LUT_1D_SIZE 3\n0 0 0\n0.5 0.5 0.5\n1 1 1\n");
    check(ramp && ramp->size() == 3 && ramp->at(2, 1, 0) == 0xFFFF8000u && ramp->at(0, 2, 1) == 0xFF00FF80u,
          "a 1D table reads red, green and blue from their own rows");
    check(!CubeLut::parse("LUT_1D_SIZE 3\n0 0 0\n1 1 1\n"), "a 1D table short of rows");
    std::string oneD65 = "LUT_1D_SIZE 65\n";
    std::string oneD66 = "LUT_1D_SIZE 66\n";
    for (int i = 0; i < 66; ++i) {
        if (i < 65) oneD65 += "0.5 0.5 0.5\n";
        oneD66 += "0.5 0.5 0.5\n";
    }
    const auto ok65 = CubeLut::parse(oneD65);
    check(ok65 && ok65->size() == 65, "a 65-entry 1D table is kept");
    check(!CubeLut::parse(oneD66), "a 66-entry 1D table is refused");

    const auto both = CubeLut::parse(std::string("LUT_1D_SIZE 2\n") + kIdentity3d);
    check(both && both->at(0, 1, 0) == 0xFF00FF00u, "a 3D size wins over a 1D size");

    const auto clamped = CubeLut::parse(cubeOf(2, "1.5 -0.5 0.25"));
    check(clamped && clamped->at(0, 0, 0) == 0xFFFF0040u, "values outside 0..1 clamp; 0.25 is 63.75, so 64");
    const auto rounded = CubeLut::parse(cubeOf(2, "0.002 0.498 1"));
    check(rounded && rounded->at(1, 0, 1) == 0xFF017FFFu, "0.51 rounds up to 1 and 126.99 to 127, half up");

    const auto inverted = CubeLut::parse("DOMAIN_MIN 0.5 0.5 0.5\nDOMAIN_MAX 0 0 0\n" + cubeOf(2, "1 1 1"));
    check(inverted && inverted->at(0, 0, 0) == 0xFF808080u, "an inverted domain spans 1, it does not flip colours");
    const auto flat = CubeLut::parse("DOMAIN_MIN 0.5 0.5 0.5\nDOMAIN_MAX 0.5 0.5 0.5\n" + cubeOf(2, "1 1 1"));
    check(flat && flat->at(1, 1, 1) == 0xFF808080u, "an empty domain spans 1, never a division by zero");
    const auto badDomain = CubeLut::parse(std::string("DOMAIN_MAX 2 x 2\n") + kIdentity3d);
    check(badDomain && badDomain->at(1, 1, 1) == 0xFFFFFFFFu, "a malformed domain line keeps the 0..1 default");

    const std::string noisy =
        "LUT_3D_SIZE 2\n0 0 0\na b c\n1\t0\t0\n0 1 0 9\n1 1 0\n\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
    const auto messy = CubeLut::parse(noisy);
    check(messy && messy->at(1, 0, 0) == 0xFFFF0000u && messy->at(0, 1, 0) == 0xFF00FF00u && messy->at(1, 1, 1) == 0xFFFFFFFFu,
          "junk rows are skipped, tabs separate, a fourth number is ignored");
    check(!CubeLut::parse(std::string("LUT_3D_SIZE 2.0\n") + "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n"),
          "a size that is not an integer is no size, as Kotlin's toIntOrNull");
    check(!CubeLut::parse("LUT_3D_SIZE\n0 0 0\n"), "a size keyword without a size");
}

// Kills: the gamma cube's 2..65 size clamp dropped, channels crossed, the 0.01 gamma floor dropped (a
// negative gamma would brighten), and the ABI's null handling.
void gammaCube() {
    using geode::util::CubeLut;
    check(CubeLut::gamma(1.0f, 1.0f, 1.0f, 1).size() == 2 && CubeLut::gamma(1.0f, 1.0f, 1.0f, 0).size() == 2,
          "a gamma cube is at least 2 a side");
    check(CubeLut::gamma(1.0f, 1.0f, 1.0f, 100).size() == 65, "and at most 65");
    check(CubeLut::gamma(2.0f, 1.0f, 0.5f, 3).at(1, 1, 1) == 0xFFB48040u,
          "each channel its own gamma: 0.5 goes to 0.707 (180), 0.5 (128) and 0.25 (64)");
    const CubeLut dim = CubeLut::gamma(-1.0f, 1.0f, 1.0f, 3);
    check(dim.at(1, 0, 0) == 0xFF000000u && dim.at(2, 0, 0) == 0xFFFF0000u,
          "a gamma under 0.01 is 0.01: mid red goes to black, never brighter");
    const CubeLut odd = CubeLut::gamma(0.3f, 2.2f, 7.0f, 5);
    check(odd.at(4, 4, 4) == 0xFFFFFFFFu && odd.at(0, 0, 0) == 0xFF000000u, "any gamma keeps black and white");

    geode_cube_lut* g = geode_cube_lut_gamma(2.0f, 2.0f, 2.0f, 3);
    check(g && geode_cube_lut_size(g) == 3 && geode_cube_lut_argb(g)[(1 * 3 + 1) * 3 + 1] == 0xFFB4B4B4u, "ABI gamma cube");
    geode_cube_lut_destroy(g);
    geode_cube_lut_destroy(nullptr);
    check(geode_cube_lut_size(nullptr) == 0 && geode_cube_lut_argb(nullptr) == nullptr, "ABI null cube");
    check(geode_cube_lut_parse(nullptr, 10) == nullptr, "ABI null text");
}

// Kills: the mono mean summing only two channels, mean or fold rounding toward -inf instead of zero,
// interleaving indices (f * 2, f * src) off, the saturation's lower bound, and the null / layout guards.
void pcmBoundaries() {
    using namespace geode::audio;
    const int16_t sixTwo[12] = {1000, -1000, 600, 200, 100, 100, -7, 0, 0, 0, 0, 0};
    int16_t mono[2] = {};
    check(downmixS16(sixTwo, 2, 6, 1, mono) == 2 && mono[0] == 166 && mono[1] == -1,
          "5.1 to mono is the mean of all six, truncated toward zero");
    const int16_t fiveOne[12] = {1000, -1000, 600, 200, 100, 100, 0, 0, -600, -200, -100, -100};
    int16_t st[4] = {};
    check(downmixS16(fiveOne, 2, 6, 2, st) == 2 && st[0] == 1125 && st[1] == -875 && st[2] == -125 && st[3] == -125,
          "two 5.1 frames fold into two stereo frames, in order");
    const int16_t three[6] = {100, 200, 50, 0, 0, -3};
    check(downmixS16(three, 2, 3, 2, st) == 2 && st[0] == 125 && st[1] == 225 && st[2] == -1 && st[3] == -1,
          "three channels: half the third to each side, -1.5 truncated to -1");
    const int16_t quad[4] = {10, 20, 7, 0};
    check(downmixS16(quad, 1, 4, 2, st) == 1 && st[0] == 11 && st[1] == 21, "quad folds half the rear mean");
    const int16_t quiet[6] = {-32768, -32768, -32768, -32768, -32768, -32768};
    check(downmixS16(quiet, 1, 6, 2, st) == 1 && st[0] == -32768 && st[1] == -32768, "the fold saturates at -32768");
    check(downmixS16(quad, 1, 1, 1, st) == 0 && downmixS16(quad, 1, 4, 0, st) == 0, "not a downmix");
    check(downmixS16(nullptr, 1, 6, 2, st) == 0 && downmixS16(quad, 1, 4, 2, nullptr) == 0, "null buffers");

    const float f[6] = {INFINITY, -INFINITY, std::numeric_limits<float>::denorm_min(), -0.0f, 0.99999f, -0.00002f};
    int16_t s[6] = {1, 1, 1, 1, 1, 1};
    floatToS16(f, 6, s);
    check(s[0] == 32767 && s[1] == -32767 && s[2] == 0 && s[3] == 0 && s[4] == 32766 && s[5] == 0,
          "infinities clamp, denormals and -0 are 0, fractions truncate toward zero");
    floatToS16(nullptr, 6, s);
    const int16_t ints[3] = {32767, 1, 0};
    float g[3] = {};
    s16ToFloat(ints, 3, g);
    check(g[0] == 0.999969482421875f && g[1] == 3.0517578125e-05f && g[2] == 0.0f, "s16 full scale is 32767 / 32768");

    const float half[1] = {0.5f};
    int16_t one[1] = {};
    geode_pcm_float_to_s16(half, 1, one);
    const int16_t q[1] = {16384};
    float back[1] = {};
    geode_pcm_s16_to_float(q, 1, back);
    check(one[0] == 16383 && back[0] == 0.5f, "ABI float <-> s16");
}

}  // namespace

int main() {
    grade();
    cubeLut();
    pcm();
    cubeLutBoundaries();
    gammaCube();
    pcmBoundaries();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("clip_media_test: all passed\n");
    return 0;
}
