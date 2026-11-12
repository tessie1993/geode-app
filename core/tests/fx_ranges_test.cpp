// The sound effects' ranges (audio/dsp/FxRanges) through the C ABI the shell asks them with: each control's
// range and clamp, pinned to the numbers the Kotlin shell held before, and the band levels a session starts
// on, which were the shell's too: the saved curve, else the saved levels, else 0, each in range.
#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include "api/geode_api.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int32_t kUntouched = 0x5a5a5a5a;

void ranges() {
    check(geode_dsp_band_count() == 10 && GEODE_DSP_BANDS == 10, "ten bands");
    check(geode_dsp_fx_min(GEODE_DSP_FX_BAND) == -1500 && geode_dsp_fx_max(GEODE_DSP_FX_BAND) == 1500, "a band is -15..15 dB");
    check(GEODE_DSP_MIN_MILLIBELS == -1500 && GEODE_DSP_MAX_MILLIBELS == 1500, "the header's band range is the same");
    check(geode_dsp_fx_min(GEODE_DSP_FX_BASS_BOOST) == 0 && geode_dsp_fx_max(GEODE_DSP_FX_BASS_BOOST) == 1000, "bass boost is 0..1000");
    check(geode_dsp_fx_min(GEODE_DSP_FX_LOUDNESS) == 0 && geode_dsp_fx_max(GEODE_DSP_FX_LOUDNESS) == 1000, "loudness is 0..1000");
    for (int control : {-1, 3, INT32_MAX, INT32_MIN}) {
        check(geode_dsp_fx_min(control) == 0 && geode_dsp_fx_max(control) == 0, "no range for a control that is not one");
        check(geode_dsp_fx_clamp(control, 700) == 0, "and no value");
    }
}

void clamps() {
    check(geode_dsp_fx_clamp(GEODE_DSP_FX_BAND, -1501) == -1500 && geode_dsp_fx_clamp(GEODE_DSP_FX_BAND, 1501) == 1500, "a band held at its ends");
    check(geode_dsp_fx_clamp(GEODE_DSP_FX_BAND, INT32_MIN) == -1500 && geode_dsp_fx_clamp(GEODE_DSP_FX_BAND, INT32_MAX) == 1500, "far outside too");
    for (int v : {-1500, -1, 0, 350, 1500}) check(geode_dsp_fx_clamp(GEODE_DSP_FX_BAND, v) == v, "a band level in range is kept");
    for (int control : {GEODE_DSP_FX_BASS_BOOST, GEODE_DSP_FX_LOUDNESS}) {
        check(geode_dsp_fx_clamp(control, -1) == 0 && geode_dsp_fx_clamp(control, INT32_MIN) == 0, "nothing below 0");
        check(geode_dsp_fx_clamp(control, 1001) == 1000 && geode_dsp_fx_clamp(control, INT32_MAX) == 1000, "nothing above 1000");
        for (int v : {0, 1, 500, 1000}) check(geode_dsp_fx_clamp(control, v) == v, "a value in range is kept");
    }
}

bool restores(int preset, const int32_t* stored, int count, const int32_t (&want)[GEODE_DSP_BANDS]) {
    int32_t out[GEODE_DSP_BANDS + 1];
    for (int32_t& v : out) v = kUntouched;
    if (geode_dsp_restore_bands(preset, stored, count, out, GEODE_DSP_BANDS + 1) != GEODE_DSP_BANDS) return false;
    for (int band = 0; band < GEODE_DSP_BANDS; ++band) {
        if (out[band] != want[band]) return false;
    }
    return out[GEODE_DSP_BANDS] == kUntouched;
}

void restore() {
    const int32_t stored[GEODE_DSP_BANDS] = {100, -200, 300, -400, 500, -600, 700, -800, 900, -1000};
    const int32_t flat[GEODE_DSP_BANDS] = {};
    check(restores(-1, stored, GEODE_DSP_BANDS, stored), "no curve: the saved levels");
    check(restores(-1, nullptr, 0, flat), "nothing saved: flat");
    check(restores(-1, nullptr, GEODE_DSP_BANDS, flat), "no levels behind a count: flat");

    int32_t curve[GEODE_DSP_BANDS];
    for (int preset = 0; preset < geode_dsp_eq_preset_count(); ++preset) {
        geode_dsp_eq_preset_levels(preset, curve, GEODE_DSP_BANDS);
        check(restores(preset, stored, GEODE_DSP_BANDS, curve), "a saved curve beats the saved levels");
        check(restores(preset, nullptr, 0, curve), "and needs none");
    }
    check(restores(geode_dsp_eq_preset_count(), stored, GEODE_DSP_BANDS, stored), "a curve past the last: the saved levels");
    check(restores(INT32_MAX, stored, GEODE_DSP_BANDS, stored) && restores(INT32_MIN, stored, GEODE_DSP_BANDS, stored), "far outside too");

    const int32_t three[GEODE_DSP_BANDS] = {100, -200, 300, 0, 0, 0, 0, 0, 0, 0};
    check(restores(-1, stored, 3, three), "fewer saved levels: the rest at 0");
    const int32_t longer[GEODE_DSP_BANDS + 2] = {100, -200, 300, -400, 500, -600, 700, -800, 900, -1000, 1100, 1200};
    check(restores(-1, longer, GEODE_DSP_BANDS + 2, stored), "more saved levels: the first ten");
    check(restores(-1, stored, -1, flat), "a negative count reads nothing");

    const int32_t wild[GEODE_DSP_BANDS] = {-1501, 1501, INT32_MIN, INT32_MAX, -1500, 1500, 0, 1, -1, 2000};
    const int32_t held[GEODE_DSP_BANDS] = {-1500, 1500, -1500, 1500, -1500, 1500, 0, 1, -1, 1500};
    check(restores(-1, wild, GEODE_DSP_BANDS, held), "saved levels out of range are held to it");

    int32_t out[GEODE_DSP_BANDS];
    for (int32_t& v : out) v = kUntouched;
    check(geode_dsp_restore_bands(-1, stored, GEODE_DSP_BANDS, out, GEODE_DSP_BANDS - 1) == 0, "a buffer one band short is refused");
    check(geode_dsp_restore_bands(-1, stored, GEODE_DSP_BANDS, out, -1) == 0, "a negative capacity is refused");
    check(geode_dsp_restore_bands(-1, stored, GEODE_DSP_BANDS, nullptr, GEODE_DSP_BANDS) == 0, "no buffer is refused");
    bool untouched = true;
    for (int32_t v : out) untouched = untouched && v == kUntouched;
    check(untouched, "a refused call writes nothing");
}

}  // namespace

int main() {
    ranges();
    clamps();
    restore();
    if (failures == 0) std::printf("fx_ranges_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
