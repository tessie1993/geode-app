// The sound effects' ranges (audio/dsp/FxRanges) through the C ABI the shell asks them with: each control's
// range and clamp, pinned to the numbers the Kotlin shell held before, and the band levels a session starts
// on, which were the shell's too: the saved curve, else the saved levels, else 0, each in range.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include "api/geode_api.h"
#include "audio/dsp/FxRanges.hpp"

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

bool restoresText(int preset, const char* csv, const int32_t (&want)[GEODE_DSP_BANDS]) {
    int32_t out[GEODE_DSP_BANDS + 1];
    for (int32_t& v : out) v = kUntouched;
    if (geode_dsp_restore_bands_text(preset, csv, out, GEODE_DSP_BANDS + 1) != GEODE_DSP_BANDS) return false;
    for (int band = 0; band < GEODE_DSP_BANDS; ++band) {
        if (out[band] != want[band]) return false;
    }
    return out[GEODE_DSP_BANDS] == kUntouched;
}

std::string encoded(const int32_t* levels, int count) {
    char buf[256];
    const int need = geode_dsp_encode_bands(levels, count, buf, static_cast<int>(sizeof buf));
    return need == static_cast<int>(std::strlen(buf)) ? std::string(buf) : std::string("<length mismatch>");
}

void text() {
    const int32_t stored[GEODE_DSP_BANDS] = {100, -200, 300, -400, 500, -600, 700, -800, 900, -1000};
    const int32_t flat[GEODE_DSP_BANDS] = {};
    check(restoresText(-1, "100,-200,300,-400,500,-600,700,-800,900,-1000", stored), "the saved text restores");
    check(restoresText(-1, nullptr, flat) && restoresText(-1, "", flat), "no text, or empty: flat");
    check(restoresText(-1, " 100 , -200,\t300\n,-400,+500,-600,700,-800,900,-1000 ", stored), "blanks and a plus sign are read");
    const int32_t skipped[GEODE_DSP_BANDS] = {100, 300, 0, 0, 0, 0, 0, 0, 0, 0};
    check(restoresText(-1, "100,abc,300", skipped), "an entry that is not a number is dropped and the rest move up");
    check(restoresText(-1, "100,,1.5,-,+,0x10,300", skipped), "empty, fractional, bare sign and hex entries too");
    const int32_t three[GEODE_DSP_BANDS] = {100, -200, 300, 0, 0, 0, 0, 0, 0, 0};
    check(restoresText(-1, "100,-200,300", three), "a short list: the rest at 0");
    check(restoresText(-1, "100,-200,300,-400,500,-600,700,-800,900,-1000,1100,1200", stored), "a long list: the first ten");
    const int32_t wild[GEODE_DSP_BANDS] = {-1500, 1500, -1500, 1500, 0, 0, 0, 0, 0, 0};
    check(restoresText(-1, "-2000,2000,-2147483648,2147483647", wild), "int extremes are numbers, held to the band range");
    const int32_t one[GEODE_DSP_BANDS] = {100};
    check(restoresText(-1, "2147483648,-2147483649,99999999999,100", one), "overflow entries are dropped, the rest move up");
    int32_t curve[GEODE_DSP_BANDS];
    for (int preset = 0; preset < geode_dsp_eq_preset_count(); ++preset) {
        geode_dsp_eq_preset_levels(preset, curve, GEODE_DSP_BANDS);
        check(restoresText(preset, "100,-200", curve), "a saved curve beats the text");
    }
    int32_t out[GEODE_DSP_BANDS];
    for (int32_t& v : out) v = kUntouched;
    check(geode_dsp_restore_bands_text(-1, "1", out, GEODE_DSP_BANDS - 1) == 0 && out[0] == kUntouched, "a short buffer is refused");
    check(geode_dsp_restore_bands_text(-1, "1", nullptr, GEODE_DSP_BANDS) == 0, "no buffer is refused");
}

void encode() {
    const int32_t levels[4] = {100, -200, 0, 1500};
    check(encoded(levels, 4) == "100,-200,0,1500", "levels joined by commas");
    check(encoded(levels, 1) == "100", "one level has no comma");
    check(encoded(levels, 0).empty() && encoded(nullptr, 3).empty(), "none is empty text");
    const int32_t extremes[2] = {INT32_MIN, INT32_MAX};
    check(encoded(extremes, 2) == "-2147483648,2147483647", "int extremes");

    char small[6];
    std::memset(small, 'x', sizeof small);
    check(geode_dsp_encode_bands(levels, 4, small, static_cast<int>(sizeof small)) == 15, "a short buffer gets the length needed");
    check(std::strcmp(small, "100,-") == 0, "and as much as fits, NUL-terminated");
    check(geode_dsp_encode_bands(levels, 4, nullptr, 0) == 15, "no buffer asks the length");

    const int32_t stored[GEODE_DSP_BANDS] = {100, -200, 300, -400, 500, -600, 700, -800, 900, -1000};
    const std::string saved = encoded(stored, GEODE_DSP_BANDS);
    check(restoresText(-1, saved.c_str(), stored), "what is saved restores the same");
}

std::string bandLabel(int band) {
    char buf[32];
    const int need = geode_dsp_band_label(band, buf, static_cast<int>(sizeof buf));
    return need == static_cast<int>(std::strlen(buf)) ? std::string(buf) : std::string("<length mismatch>");
}

std::string dbLabel(int millibels) {
    char buf[32];
    const int need = geode_dsp_db_label(millibels, buf, static_cast<int>(sizeof buf));
    return need == static_cast<int>(std::strlen(buf)) ? std::string(buf) : std::string("<length mismatch>");
}

// The labels the settings showed when Kotlin wrote them (AudioFxFormat): cut, not rounded.
void labels() {
    const char* const want[GEODE_DSP_BANDS] = {"31 Hz", "62 Hz", "125 Hz", "250 Hz", "500 Hz",
                                               "1 kHz", "2 kHz", "4 kHz",  "8 kHz",  "16 kHz"};
    for (int band = 0; band < GEODE_DSP_BANDS; ++band) check(bandLabel(band) == want[band], "each band's centre");
    check(bandLabel(-1) == "0 Hz" && bandLabel(GEODE_DSP_BANDS) == "0 Hz", "no such band");
    using geode::audio::fx::freqLabel;
    check(freqLabel(999'999) == "999 Hz" && freqLabel(1'000'000) == "1 kHz", "kHz from 1000 Hz");
    check(freqLabel(1'550'000) == "1.5 kHz" && freqLabel(12'099'000) == "12 kHz", "a tenth, cut");
    check(freqLabel(999) == "0 Hz", "under a hertz");

    check(dbLabel(0) == "0 dB", "zero has no sign");
    check(dbLabel(350) == "+3.5 dB" && dbLabel(-1200) == "-12 dB", "a tenth when there is one");
    check(dbLabel(1500) == "+15 dB" && dbLabel(-1500) == "-15 dB", "the band ends");
    check(dbLabel(99) == "+0.9 dB" && dbLabel(-55) == "-0.5 dB", "cut, not rounded");
    check(dbLabel(5) == "+0 dB" && dbLabel(-1) == "-0 dB", "under a tenth keeps its sign");
    check(dbLabel(1000) == "+10 dB", "the loudness top");

    char small[4];
    check(geode_dsp_db_label(350, small, static_cast<int>(sizeof small)) == 7 && std::strcmp(small, "+3.") == 0,
          "a short buffer gets the length needed and what fits");
    check(geode_dsp_band_label(0, nullptr, 0) == 5, "no buffer asks the length");
}

}  // namespace

int main() {
    ranges();
    clamps();
    restore();
    text();
    encode();
    labels();
    if (failures == 0) std::printf("fx_ranges_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
