#pragma once
// The gradient palette (Customize › Look › Palette): up to eight colours the user picked, painted across a
// style as ONE smooth gradient in row order, plus the palette sequence that steps through saved palettes on
// a beat cycle. Pure maths, no GL, so core/tests/palette_test.cpp pins it on the host; the renderer bakes
// the result into a small uniform array (ShaderScene, lib_palette) and the shell only passes colours across.
//
// Colours cross the ABI as packed 0xRRGGBB ints (sRGB). Gradients interpolate in OKLab, so a blue-to-yellow
// ramp passes through a clean grey instead of the muddy mid a straight sRGB blend gives, and the lightness
// between two stops is even.
#include <array>
#include <cstdint>

namespace geode::viz::palette {

constexpr int kMaxStops = 8;
constexpr int kMaxSequence = 8;
constexpr int kBakedSamples = 32;  // gradient samples handed to the shader (uPalGrad)

struct Rgb {
    float r = 0.0f, g = 0.0f, b = 0.0f;
};

struct Gradient {
    int count = 0;
    std::array<Rgb, kMaxStops> stops{};
    bool operator==(const Gradient& o) const;
};

using Baked = std::array<float, kBakedSamples * 3>;  // rgb per sample, t = i / (kBakedSamples - 1)

Rgb fromPacked(int32_t rgb);
int32_t toPacked(Rgb c);

// The gradient at t in 0..1 (stops evenly spaced in order); black when empty.
Rgb sample(const Gradient& g, float t);
void bake(const Gradient& g, Baked& out);

// Harmony helpers: the colours to add beside `base`, found by turning its OKLCH hue at the same lightness and
// chroma (pulled in just enough to stay in gamut, so the hue holds).
enum class Harmony { Complementary = 0, Triad = 1, Analogous = 2, SplitComplementary = 3 };
// Writes up to 3 colours to out; returns how many (not counting base).
int harmony(Rgb base, Harmony kind, Rgb* out);

// OKLCH hue of c in degrees 0..360 (0 for greys); exposed for the host test.
float oklchHue(Rgb c);

// An approximate hue base and span (0..1, SceneParams' units) for the families that colour by hue alone
// (the simulations, MilkDrop's tint): the first stop's hue and the forward turn to the last stop's.
void approximateHues(const Baked& baked, float& base, float& range);

// The palette sequence: palettes in order, stepping to the next every `beats` beats with a crossfade.
struct Sequence {
    int beats = 8;  // 4, 8, 16 or 32; 0 = off
    int count = 0;
    std::array<Gradient, kMaxSequence> palettes{};
    bool operator==(const Sequence& o) const;
};

// Everything the shell hands across in one int array:
//   [stopCount, rgb x stopCount, beats, paletteCount, (stopCount, rgb x stopCount) x paletteCount]
// Out-of-range counts clamp; a short array reads as "no gradient, no sequence".
struct Wire {
    Gradient gradient;
    Sequence sequence;
};
Wire decodeWire(const int32_t* values, int count);
int encodeWire(const Wire& wire, int32_t* out, int capacity);

class SequenceRunner {
public:
    static constexpr float kLockedConfidence = 0.35f;  // beatConf above which the rhythm engine's beat is trusted
    static constexpr float kFreeBeatsPerSecond = 2.0f; // 120 BPM, the clock with no beat
    static constexpr float kFadeBeats = 2.0f;

    // A changed sequence restarts at its first palette; an equal one is ignored.
    void set(const Sequence& seq);
    bool active() const { return seq_.beats > 0 && seq_.count > 0; }
    // beatCount / beatPhase / beatConf are GeodeSoundFrame's (count wraps at 4096).
    void step(float beatCount, float beatPhase, float beatConf, float dt);
    const Baked& current() const { return out_; }
    int index() const { return index_; }
    float mix() const { return mix_; }

    static int beatsBetween(float before, float after);

private:
    Sequence seq_;
    Baked from_{}, to_{}, out_{};
    int index_ = 0;
    float mix_ = 1.0f;
    double pos_ = 0.0;  // beats on the sequence's own clock
    long slot_ = 0;
    bool started_ = false;
    bool wasLocked_ = false;
    float lastCount_ = 0.0f, lastPhase_ = 0.0f;
    void blend();
};

}  // namespace geode::viz::palette
