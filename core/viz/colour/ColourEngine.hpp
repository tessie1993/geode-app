#pragma once
#include <array>
#include <cstdint>

#include "sound/Harmony.hpp"
#include "sound/SoundFrame.h"
#include "viz/colour/Oklab.hpp"
#include "viz/colour/PaletteLibrary.hpp"

namespace geode::viz::colour {

// The user's colour controls (the Colour tab). Global, like the macros: one set for every style, and
// export reads the same set, so a render is coloured as the screen was.
struct ColourSettings {
    static constexpr int kStyleOwn = -1;  // start from the style's own palette, walk away from it on big moments
    static constexpr int kAuto = -2;      // the music's mood picks library palettes
    static constexpr int kCustom = -3;    // the user's own stops (the colour picker)
    static constexpr int kMaxCustomStops = 8;
    // What turns the hue inside the palette, on top of the key (which always sets the home hue).
    enum class HueDriver : int { Melody = 0, Chord = 1, Key = 2, Timbre = 3, Blend = 4 };

    int palette = kStyleOwn;  // or a library index (PaletteLibrary)
    float follow = 0.75f;     // 0 the style as it was … 1 the music moves every colour
    HueDriver hueDriver = HueDriver::Melody;
    float variety = 1.0f;     // 0 one palette, bent … 1 its mood group … 2 the whole library
    uint32_t moods = kAllMoods;  // which moods Auto and the walk may use, one bit per Mood
    int customCount = 0;
    std::array<Rgb, kMaxCustomStops> custom{};

    // The wire of geode_viz_set_colour: palette, follow, hue driver, variety, mood mask, custom count,
    // then kMaxCustomStops × (r, g, b). Missing trailing values keep their defaults.
    static constexpr int kWireFloats = 6 + 3 * kMaxCustomStops;
    static ColourSettings fromWire(const float* values, int count);
    void toWire(float* out) const;
};

// What the colour engine hands every scene for one frame (lib_palette / lib_scene_grade uniforms).
struct ColourFrame {
    static constexpr int kRamp = 16;
    bool active = false;  // false: pal() and grade() behave exactly as before the engine existed
    // The music's palette, display sRGB, kRamp entries read by uColourRamp with the mirror wrap.
    std::array<Rgb, kRamp> ramp{};
    float away = 0.0f;   // how far pal() leans from the style's own palette toward the ramp, 0..1
    float drift = 0.0f;  // the hue driver's offset along the ramp, about ±1/6
    // The style's own palette, turned: hue rotation and saturation as row-major 3x3s on display RGB
    // (luminance-preserving, the CSS filter matrices). `clash` is the same turned further toward the
    // complement; pal() blends it in on alternate bands by clashAmount (tension).
    std::array<float, 9> hue{1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::array<float, 9> clash{1, 0, 0, 0, 1, 0, 0, 0, 1};
    float clashAmount = 0.0f;
    // grade(): the highlight colour (rgb) and how strongly the brightest pixels take it (a).
    std::array<float, 4> highlight{};
    // grade(): shadow depth from tension (0..1), contrast and saturation offsets from energy (about ±0.3).
    float shade = 0.0f;
    float contrast = 0.0f;
    float saturation = 0.0f;
};

// The live colour engine: hears the sound frame and its harmony each frame and moves the palette.
//
// One per renderer (screen, wallpaper, dream, export), stepped once per frame on the GL thread right
// after the sound engine, before any scene draws. Everything it does is a function of the frames it
// has been fed, the settings and the seed: no wall clock, no rand(), every smoothing step scaled by dt.
// So export, which feeds the same audio at 1/fps, colours a song exactly as it does on every other run.
class ColourEngine {
public:
    void setSettings(const ColourSettings& settings);
    const ColourSettings& settings() const { return settings_; }
    // The track's seed (a hash of its identity): where the palette walk starts and which way it goes.
    // A new seed restarts the walk; the same seed replays it.
    void setSeed(uint32_t seed);
    void step(const GeodeSoundFrame& sound, const sound::GeodeHarmony& harmony, float dt);
    const ColourFrame& frame() const { return frame_; }
    void reset();

    // Exposed for the tests.
    int currentPalette() const { return to_; }
    int previousPalette() const { return from_; }
    int sectionCount() const { return sections_; }

private:
    void choosePalette(float arousal, float valence);
    std::array<Lab, 5> stopsOf(int index) const;
    float tonicRotation(const sound::GeodeHarmony& h) const;
    float driverTarget(const GeodeSoundFrame& f, const sound::GeodeHarmony& h) const;
    void buildRamp(float rotation, float chromaGain, float lift);
    uint32_t hash(uint32_t a, uint32_t b) const;

    ColourSettings settings_;
    uint32_t seed_ = 0x51EDu;
    ColourFrame frame_;
    // The walk: crossfading from `from_` to `to_` over kFadeSeconds; `last_` is the one before, never
    // chosen twice in a row.
    int from_ = 0;
    int to_ = 0;
    float fade_ = 1.0f;
    float sinceSwitch_ = 0.0f;
    int sections_ = 0;
    bool started_ = false;
    // Smoothed drivers.
    float presence_ = 0.0f;
    float arousal_ = 0.5f;
    float valence_ = 0.5f;
    float big_ = 0.0f;      // a held big moment (hard cut, surge, return), decays over ~8 s
    float awayEase_ = 0.0f;
    float drift_ = 0.0f;
    float tension_ = 0.0f;
    float light_ = 0.0f;    // melody height and timbre brightness, -1..1
    float hit_ = 0.0f;
    float highlightTurn_ = 0.0f;
};

}  // namespace geode::viz::colour
