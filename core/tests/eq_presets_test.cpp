// The built-in equalizer curves (audio/dsp/EqPresets) through the C ABI the shell lists them with. The
// settings store a curve by its index and the band levels it set, so the table is saved data: these cases
// pin every curve in its place, and check that a call for a curve that is not there, or into a buffer too
// small, writes nothing.
#include <cstdint>
#include <cstdio>

#include "api/geode_api.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int kCurves = 6;
constexpr int32_t kUntouched = 0x5a5a5a5a;

// Flat, bass, treble, vocal, rock, electronic: the order the settings' saved index means.
constexpr int32_t kExpected[kCurves][GEODE_DSP_BANDS] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {600, 500, 350, 150, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 100, 250, 400, 500, 550},
    {-200, -150, -50, 100, 300, 350, 300, 150, 0, -100},
    {450, 350, 150, -50, -150, -50, 150, 300, 400, 450},
    {500, 400, 100, 0, -150, 0, 150, 300, 450, 500},
};

void table() {
    check(geode_dsp_eq_preset_count() == kCurves, "six curves");
    for (int preset = 0; preset < kCurves; ++preset) {
        // One slot past the bands, to see that a curve stops at its last band.
        int32_t out[GEODE_DSP_BANDS + 1];
        for (int32_t& v : out) v = kUntouched;
        check(geode_dsp_eq_preset_levels(preset, out, GEODE_DSP_BANDS + 1) == GEODE_DSP_BANDS, "a curve is ten bands");
        bool same = true;
        bool inRange = true;
        for (int band = 0; band < GEODE_DSP_BANDS; ++band) {
            same = same && out[band] == kExpected[preset][band];
            inRange = inRange && out[band] >= GEODE_DSP_MIN_MILLIBELS && out[band] <= GEODE_DSP_MAX_MILLIBELS;
        }
        check(same, "the curve's levels, in its saved place");
        check(inRange, "every level is one the equalizer takes");
        check(out[GEODE_DSP_BANDS] == kUntouched, "nothing written past the last band");
    }
}

void refusals() {
    int32_t out[GEODE_DSP_BANDS];
    for (int32_t& v : out) v = kUntouched;
    const int count = geode_dsp_eq_preset_count();
    check(geode_dsp_eq_preset_levels(-1, out, GEODE_DSP_BANDS) == 0, "no curve below the first");
    check(geode_dsp_eq_preset_levels(count, out, GEODE_DSP_BANDS) == 0, "no curve past the last");
    check(geode_dsp_eq_preset_levels(INT32_MAX, out, GEODE_DSP_BANDS) == 0, "no curve far past the last");
    check(geode_dsp_eq_preset_levels(INT32_MIN, out, GEODE_DSP_BANDS) == 0, "no curve far below the first");
    check(geode_dsp_eq_preset_levels(1, out, GEODE_DSP_BANDS - 1) == 0, "a buffer one band short is refused");
    check(geode_dsp_eq_preset_levels(1, out, 0) == 0, "an empty buffer is refused");
    check(geode_dsp_eq_preset_levels(1, out, -1) == 0, "a negative capacity is refused");
    check(geode_dsp_eq_preset_levels(1, nullptr, GEODE_DSP_BANDS) == 0, "no buffer is refused");
    bool untouched = true;
    for (int32_t v : out) untouched = untouched && v == kUntouched;
    check(untouched, "a refused call writes nothing");
}

}  // namespace

int main() {
    table();
    refusals();
    if (failures == 0) std::printf("eq_presets_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
