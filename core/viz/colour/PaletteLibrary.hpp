#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "viz/colour/Oklab.hpp"

namespace geode::viz::colour {

// The curated palettes the colour engine walks through, grouped by mood.
//
// Each is five stops, dark to light or along a gradient, written as sRGB hex and interpolated in OKLab.
// The stops are hand-picked approximations of looks from painting, film grading and nature (see
// docs/COLOUR_ENGINE.md); none is sampled from a work, and the names describe the look, not its source.
//
// A mood is a place on the two axes the music-colour research found people use (Palmer et al. 2013,
// Whiteford et al. 2018): arousal (energy, loudness, punch) and valence (major, bright, happy). Auto
// mode matches the music's place on the same axes to choose a mood, then a palette within it.
enum class Mood : uint8_t { Calm, Warm, Dark, Melancholic, Euphoric, Neon, Natural, Count };

struct MoodInfo {
    const char* id;
    const char* name;
    float arousal;  // 0..1
    float valence;  // 0..1
};

struct Palette {
    const char* id;    // stable: what a saved choice stores, so the table can be reordered
    const char* name;
    Mood mood;
    std::array<uint32_t, 5> stops;  // 0xRRGGBB
};

constexpr int kMoodCount = static_cast<int>(Mood::Count);
constexpr uint32_t kAllMoods = (1u << kMoodCount) - 1u;

const MoodInfo& moodInfo(Mood mood);
const std::vector<Palette>& palettes();
// The index of the palette with this id, or -1.
int paletteIndex(const std::string& id);
// The stops of palette `index` in OKLab, converted once.
const std::array<Lab, 5>& paletteLab(int index);

// The library as text, one palette per line: id, name, mood id and the five stops as #rrggbb, tab
// separated. What Kotlin's picker parses (geode_colour_palettes), so it never keeps a copy of the table.
std::string paletteTable();

}  // namespace geode::viz::colour
