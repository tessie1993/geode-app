#include "viz/ModCatalog.hpp"

#include <array>

#include "viz/ModTargets.hpp"
#include "viz/ReactionText.hpp"
#include "viz/TableNumber.hpp"

namespace geode::viz::modcatalog {

namespace {

struct Words {
    const char* name;
    const char* label;
};

// One label per LfoTarget, in wire order; a retired target has none.
constexpr std::array<const char*, modtargets::kTargetCount> kTargetLabels{{
    "None", "Speed", "Zoom", "Rotation", "Sway", "", "Drift X", "Drift Y", "Warp", "Ripple", "Morph", "Twist", "Tile",
    "Pixelate", "Posterize", "Hue shift", "Palette blend", "Saturation", "Brightness", "Intensity", "Bloom",
    "Temperature", "Turbulence", "Chroma AB", "Vignette", "", "Fisheye", "Particle size", "Trail length", "Fluid curl",
    "Fluid splat radius", "Fluid splat force", "Fluid glow", "Fluid fade", "Catch pull", "Catch radius", "Flow strength",
    "", "", "Wave 1 speed", "Wave 1 depth", "Wave 2 speed", "Wave 2 depth", "Wave 3 speed", "Wave 3 depth",
    "Camera speed", "Camera swing", "Camera push", "Camera turn", "Camera tilt", "Camera roll", "Camera FOV",
    "Camera shake", "Swirl", "Push", "Dive", "Dive twist", "Dive aim sideways", "Dive aim up and down", "Macro knob",
    "Palette 1 amount", "Palette 3 amount", "Palette 4 amount", "Palette 5 amount", "Palette 6 amount",
    "Palette 7 amount", "Palette 8 amount", "Palette position",
}};

constexpr std::array<const char*, kSourceCount> kSourceLabels{{
    "Steady wave", "Bass band", "Mid band", "Treble band", "Level", "Brightness", "Note starts", "Stereo width",
    "L/R movement",
}};
constexpr std::array<const char*, kPolarityCount> kPolarityLabels{{"Both ways", "Up only", "Down only"}};
constexpr std::array<const char*, kCurveCount> kCurveLabels{{"Linear", "Exponential", "Logarithmic", "S-curve"}};
constexpr std::array<const char*, kWaveCount> kWaveLabels{{
    "Sine", "Triangle", "Saw", "Square", "Random steps", "Random glide",
}};
constexpr std::array<Words, kBandCount> kBands{{
    {"BASS", "Bass"},
    {"MID", "Mid"},
    {"TREBLE", "Treble"},
    {"RMS", "Level"},
    {"BRIGHTNESS", "Brightness"},
    {"WIDTH", "Stereo width"},
}};
constexpr std::array<Words, kReactModeCount> kModes{{
    {"FOLLOW", "Follow"}, {"STEP", "Step"}, {"SPIN", "Add up"}, {"JUMP", "Jump"}, {"TOGGLE", "Toggle"}, {"JUMP_SETTLED", "Jump, settled"},
}};

// The reaction sources' groups (ReactSourceInfo::group), in the order the source picker lists them.
constexpr std::array<Words, 10> kGroups{{
    {"LEVELS", "Levels"},
    {"DRUMS", "Drums"},
    {"EVENTS", "Events"},
    {"MELODY", "Melody"},
    {"CLOCKS", "Clocks"},
    {"TEXTURE", "Texture"},
    {"EQ", "EQ bands"},
    {"RHYTHM", "Rhythm"},
    {"HARMONY", "Harmony"},
    {"FORM", "Song form"},
}};

struct RecipeWords {
    const char* name;
    const char* title;
    const char* sentence;
};

// One row per EventRecipe, in the menu's order.
constexpr std::array<RecipeWords, kEventRecipeCount> kRecipes{{
    {"KICK_PUNCH", "Kick punch", "When the kick hits, push in"},
    {"SNARE_CRACK", "Snare crack", "When the snare hits, split the colours"},
    {"HAT_SHIMMER", "Hi-hat shimmer", "On every hi-hat, sparkle"},
    {"SUB_WEIGHT", "Sub weight", "The sub bass bends the picture"},
    {"LOUDNESS_GLOW", "Loudness glow", "The louder it gets, the more it glows"},
    {"SWELL_BREATHE", "Swell breathe", "The longer it stays loud, the wider the view"},
    {"BUILDUP_RUSH", "Build-up rush", "As the music rises, speed up and tense"},
    {"SECTION_COLOURS", "New colours on big moments", "When everything jumps at once, change colour"},
    {"BEAT_TURN", "Kick turn", "Tilt the camera on every kick"},
    {"TIMBRE_TURN", "New angle every 8 kicks", "Every eighth kick, look elsewhere"},
    {"PITCH_TINT", "Melody tint", "When the melody moves to a new note, nudge the hue"},
    {"FLUX_TEXTURE", "Texture on change", "Busy passages ripple and twist"},
    {"STEREO_SWAY", "Stereo sway", "Follow the sound left and right"},
    {"BAR_HUE", "Colour every 4 kicks", "Shift the hue on every fourth kick"},
    {"HIT_CHAOS", "Chaos on hit", "Every hit roughs the picture up"},
    {"KICK_SWAP", "Kick colour swap", "Every kick swaps between two colourings"},
    {"KICK_ANGLE", "New angle each kick", "Every kick looks somewhere new"},
    {"SNARE_ROLL", "Roll every 2nd snare", "Every second snare rolls the camera"},
    {"MELODY_LIFT", "Melody lift", "The higher the melody, the higher the camera looks"},
    {"NOTE_HUE", "A colour for each note", "Each note of the melody has its own hue"},
    {"GLIDE_TWIST", "Slides twist", "When the pitch slides, twist with it"},
    {"VIBRATO_SHIMMER", "Vibrato shimmer", "A wobbling note makes the picture shimmer"},
    {"SLIDE_SMEAR", "Slide smear", "When a note slides into the next, smear"},
    {"SILENCE_FADE", "Fade in the quiet", "When the music stops, dim and close in"},
    {"RETURN_FLASH", "Back with a flash", "When the sound comes back, flash and push in"},
    {"BASS_PULL", "Bass pulls you in", "The busier the bass, the closer the view"},
    {"CLOCK_ORBIT", "Orbit with the music", "The camera circles as fast as the music moves"},
    {"BAR_TURN", "Turn on the bar", "On the first beat of every bar, turn the view"},
    {"SCALE_MOOD", "Scale mood", "Bright scales lift the light, tense scales stir the picture"},
    {"BUILD_AND_DROP", "Build and drop", "Speed up through the build, glow on the drop"},
}};

// Customize › Music's rows, one per Moment in wire order. The shell draws its own localised label.
constexpr std::array<Words, kMomentCount> kMoments{{
    {"KICK", "Kick"},
    {"SNARE", "Snare"},
    {"HAT", "Hi-hat"},
    {"BASS", "Bass"},
    {"DROP", "Drop"},
    {"BUILD_UP", "Build-up"},
    {"CHORD_CHANGE", "Chord change"},
    {"NEW_SECTION", "New section"},
    {"NOTE", "Note"},
    {"LOUDNESS", "Loudness"},
}};

struct ActionWords {
    MusicAction action;
    const char* name;
    const char* label;
    const char* group;
};

// A moment's action menu in the page's order: what lights the picture, then what moves it; the shell
// puts a divider where the group changes. Every MusicAction once (mod_document_test).
constexpr std::array<ActionWords, kMusicActionCount> kActionMenu{{
    {MusicAction::Flash, "FLASH", "Flash", "LIGHT"},
    {MusicAction::Colour, "COLOUR", "Colour shift", "LIGHT"},
    {MusicAction::Zoom, "ZOOM", "Zoom", "MOTION"},
    {MusicAction::Shake, "SHAKE", "Shake", "MOTION"},
    {MusicAction::Camera, "CAMERA", "Camera move", "MOTION"},
    {MusicAction::Warp, "WARP", "Warp", "MOTION"},
}};

constexpr std::array<float, kBeatChoices> kBeats{{0.0f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f}};

// The ordinal whose name (by nameAt) is `wanted`; -1 when none is.
template <typename NameAt>
int ordinalNamed(int count, std::string_view wanted, NameAt nameAt) {
    if (wanted.empty()) return -1;
    for (int i = 0; i < count; ++i) {
        if (wanted == std::string_view(nameAt(i))) return i;
    }
    return -1;
}

std::string row(const char* kind, int ordinal, const char* name, const char* label) {
    return std::string(kind) + "|" + std::to_string(ordinal) + "|" + name + "|" + label;
}

void countRow(std::string& out, const char* name, int n) {
    out += std::string("count|") + name + "|" + std::to_string(n) + "\n";
}

void rangeRow(std::string& out, const char* name, float def, float lo, float hi) {
    out += std::string("range|") + name + "|" + tableNumber(def) + "|" + tableNumber(lo) + "|" + tableNumber(hi) + "\n";
}

void configRange(std::string& out, const char* name, config::Slot slot) {
    const config::Spec& s = config::spec(slot);
    rangeRow(out, name, s.defaultValue, s.min, s.max);
}

std::string build() {
    std::string out = "catalog|" + std::to_string(kTextVersion) + "\n";
    countRow(out, "waves", LfoEngine::kSlots);
    countRow(out, "envelopes", AdsrEngine::kCount);
    countRow(out, "routes", ReactionEngine::kRoutes);
    configRange(out, "lfo.rateSeconds", config::Slot::lfo_rateSeconds);
    configRange(out, "lfo.depth", config::Slot::lfo_depth);
    configRange(out, "adsr.attack", config::Slot::adsr_attack);
    configRange(out, "adsr.decay", config::Slot::adsr_decay);
    configRange(out, "adsr.sustain", config::Slot::adsr_sustain);
    configRange(out, "adsr.release", config::Slot::adsr_release);
    configRange(out, "adsr.amount", config::Slot::adsr_amount);
    configRange(out, "adsr.gateThreshold", config::Slot::adsr_gateThreshold);
    const ReactRoute route{};
    rangeRow(out, "route.amount", route.amount, reactiontext::kMinAmount, reactiontext::kMaxAmount);
    rangeRow(out, "route.attackSeconds", route.attackSeconds, 0.0f, kRouteRiseMaxSeconds);
    rangeRow(out, "route.releaseSeconds", route.releaseSeconds, 0.0f, kRouteFallMaxSeconds);
    rangeRow(out, "music.strength", 1.0f, 0.0f, kMaxMomentStrength);
    for (int i = 0; i < kBeatChoices; ++i) {
        const int beats = static_cast<int>(kBeats[static_cast<size_t>(i)]);
        const std::string label = beats == 0 ? "Free" : std::to_string(beats) + (beats == 1 ? " beat" : " beats");
        out += "beats|" + std::to_string(i) + "|" + std::to_string(beats) + "|" + label + "\n";
    }
    for (int i = 0; i < modtargets::kTargetCount; ++i) {
        const auto t = static_cast<LfoTarget>(i);
        out += row("target", i, modtargets::name(t), targetLabel(t)) + "\n";
    }
    for (int i = 0; i < kSourceCount; ++i) {
        const auto s = static_cast<ModSource>(i);
        out += row("source", i, modtargets::sourceName(s), kSourceLabels[static_cast<size_t>(i)]) + "|" +
               std::to_string(static_cast<int>(modtargets::naturalPolarity(s))) + "\n";
    }
    for (int i = 0; i < kPolarityCount; ++i) {
        const auto p = static_cast<ModPolarity>(i);
        out += row("polarity", i, modtargets::polarityName(p), kPolarityLabels[static_cast<size_t>(i)]) + "\n";
    }
    for (int i = 0; i < kCurveCount; ++i) {
        const auto c = static_cast<ModCurve>(i);
        out += row("curve", i, modtargets::curveName(c), kCurveLabels[static_cast<size_t>(i)]) + "\n";
    }
    for (int i = 0; i < kWaveCount; ++i) {
        const auto w = static_cast<LfoWave>(i);
        out += row("wave", i, modtargets::waveName(w), kWaveLabels[static_cast<size_t>(i)]) + "\n";
    }
    for (int i = 0; i < kBandCount; ++i) {
        const Words& w = kBands[static_cast<size_t>(i)];
        out += row("band", i, w.name, w.label) + "\n";
    }
    for (int i = 0; i < kReactModeCount; ++i) {
        const Words& w = kModes[static_cast<size_t>(i)];
        out += row("mode", i, w.name, w.label) + "\n";
    }
    for (size_t i = 0; i < kGroups.size(); ++i) out += row("group", static_cast<int>(i), kGroups[i].name, kGroups[i].label) + "\n";
    for (int i = 0; i < kReactSourceCount; ++i) {
        const ReactSourceInfo& s = reactSourceInfo(static_cast<ReactSource>(i));
        out += row("react", i, s.name, s.label) + "|" + s.group + (s.event ? "|1\n" : "|0\n");
    }
    for (int i = 0; i < kEventRecipeCount; ++i) {
        const RecipeWords& r = kRecipes[static_cast<size_t>(i)];
        out += row("recipe", i, r.name, r.title) + "|" + r.sentence + "\n";
    }
    for (int i = 0; i < kMomentCount; ++i) {
        const Words& w = kMoments[static_cast<size_t>(i)];
        out += row("moment", i, w.name, w.label) + "\n";
    }
    for (const ActionWords& a : kActionMenu) {
        out += row("music", static_cast<int>(a.action), a.name, a.label) + "|" + a.group + "\n";
    }
    return out;
}

}  // namespace

float beatChoice(int index) { return index >= 0 && index < kBeatChoices ? kBeats[static_cast<size_t>(index)] : 0.0f; }

const char* bandName(EnvBand band) {
    const int i = static_cast<int>(band);
    return i >= 0 && i < kBandCount ? kBands[static_cast<size_t>(i)].name : "";
}

int targetNamed(std::string_view name) {
    return ordinalNamed(modtargets::kTargetCount, name, [](int i) { return modtargets::name(static_cast<LfoTarget>(i)); });
}

int sourceNamed(std::string_view name) {
    return ordinalNamed(kSourceCount, name, [](int i) { return modtargets::sourceName(static_cast<ModSource>(i)); });
}

int polarityNamed(std::string_view name) {
    return ordinalNamed(kPolarityCount, name, [](int i) { return modtargets::polarityName(static_cast<ModPolarity>(i)); });
}

int curveNamed(std::string_view name) {
    return ordinalNamed(kCurveCount, name, [](int i) { return modtargets::curveName(static_cast<ModCurve>(i)); });
}

int waveNamed(std::string_view name) {
    return ordinalNamed(kWaveCount, name, [](int i) { return modtargets::waveName(static_cast<LfoWave>(i)); });
}

int bandNamed(std::string_view name) {
    return ordinalNamed(kBandCount, name, [](int i) { return kBands[static_cast<size_t>(i)].name; });
}

const char* targetLabel(LfoTarget target) {
    const int i = static_cast<int>(target);
    return i >= 0 && i < modtargets::kTargetCount ? kTargetLabels[static_cast<size_t>(i)] : "";
}

const std::string& text() {
    static const std::string kText = build();
    return kText;
}

}  // namespace geode::viz::modcatalog
