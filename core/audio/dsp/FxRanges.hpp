#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "audio/dsp/EqPresets.hpp"

// The sound effects' ranges, the one place the shell and the chain take them from: a band level in
// millibels, the bass boost in permille, the loudness makeup in millibels.
namespace geode::audio::fx {

enum Control : int { kBand = 0, kBassBoost = 1, kLoudness = 2 };

inline constexpr int kMaxLoudnessMb = 1000;

struct Range {
    int min = 0;
    int max = 0;
};

// {0, 0} for a control that is not one.
constexpr Range rangeOf(int control) {
    switch (control) {
        case kBand: return {Equalizer::kMinMillibels, Equalizer::kMaxMillibels};
        case kBassBoost: return {0, Equalizer::kMaxBassBoost};
        case kLoudness: return {0, kMaxLoudnessMb};
        default: return {};
    }
}

constexpr int clamp(int control, int value) {
    const Range r = rangeOf(control);
    return std::clamp(value, r.min, r.max);
}

// The band levels a session starts on, into out[kBands]: the saved curve's when `preset` names one, else
// the saved levels, a band with neither at 0; each held to the band range.
constexpr void restoreBands(int preset, const int32_t* stored, int storedCount, int32_t* out) {
    const bool curve = preset >= 0 && preset < kEqPresets;
    for (int band = 0; band < Equalizer::kBands; ++band) {
        int level = 0;
        if (curve) {
            level = kEqPresetLevels[static_cast<size_t>(preset)][static_cast<size_t>(band)];
        } else if (stored && band < storedCount) {
            level = stored[band];
        }
        out[band] = clamp(kBand, level);
    }
}

// The saved band levels: millibels, comma separated, low band first ("0,-200,350"). An entry that is not
// a whole number in int range (empty, "abc", "1.5", "99999999999") is dropped and the ones after it move
// up; blanks around an entry are ignored, a leading + or - is a sign.
inline std::vector<int32_t> parseBandLevels(std::string_view csv) {
    std::vector<int32_t> levels;
    size_t at = 0;
    while (at <= csv.size()) {
        size_t end = csv.find(',', at);
        if (end == std::string_view::npos) end = csv.size();
        std::string_view part = csv.substr(at, end - at);
        at = end + 1;
        const auto blank = [](char c) { return c == ' ' || (c >= '\t' && c <= '\r'); };
        while (!part.empty() && blank(part.front())) part.remove_prefix(1);
        while (!part.empty() && blank(part.back())) part.remove_suffix(1);
        bool negative = false;
        if (!part.empty() && (part.front() == '-' || part.front() == '+')) {
            negative = part.front() == '-';
            part.remove_prefix(1);
        }
        if (part.empty()) continue;
        int64_t magnitude = 0;
        bool valid = true;
        for (const char c : part) {
            if (c < '0' || c > '9' || magnitude > INT32_MAX) {
                valid = false;
                break;
            }
            magnitude = magnitude * 10 + (c - '0');
        }
        if (!valid || magnitude > (negative ? int64_t{INT32_MAX} + 1 : int64_t{INT32_MAX})) continue;
        levels.push_back(static_cast<int32_t>(negative ? -magnitude : magnitude));
    }
    return levels;
}

inline std::string encodeBandLevels(const int32_t* levels, int count) {
    std::string text;
    for (int i = 0; levels && i < count; ++i) {
        if (i > 0) text += ',';
        text += std::to_string(levels[i]);
    }
    return text;
}

// A band's centre as the settings show it: whole hertz under 1 kHz ("125 Hz"), else kHz to a tenth, cut
// not rounded ("1 kHz", "1.5 kHz").
inline std::string freqLabel(int milliHz) {
    const int hz = milliHz / 1000;
    if (hz < 1000) return std::to_string(hz) + " Hz";
    const int tenth = hz % 1000 / 100;
    return std::to_string(hz / 1000) + (tenth == 0 ? "" : "." + std::to_string(tenth)) + " kHz";
}

// A level in millibels as dB to a tenth, cut not rounded, signed unless 0 ("+3.5 dB", "-12 dB", "0 dB").
inline std::string dbLabel(int millibels) {
    const int64_t magnitude = millibels < 0 ? -int64_t{millibels} : int64_t{millibels};
    const int64_t tenth = magnitude % 100 / 10;
    const std::string num = std::to_string(magnitude / 100) + (tenth == 0 ? "" : "." + std::to_string(tenth));
    if (millibels > 0) return "+" + num + " dB";
    if (millibels < 0) return "-" + num + " dB";
    return "0 dB";
}

// restoreBands from the saved text; no text (null) is nothing saved.
inline void restoreBandsText(int preset, std::string_view csv, int32_t* out) {
    const std::vector<int32_t> stored = parseBandLevels(csv);
    restoreBands(preset, stored.data(), static_cast<int>(stored.size()), out);
}

}  // namespace geode::audio::fx
