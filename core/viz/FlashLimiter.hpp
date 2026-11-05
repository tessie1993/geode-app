#pragma once
#include <array>
#include <cstdint>

namespace geode::viz::safety {

// The photosensitive flash limiter: measures what was actually on screen and holds the picture still
// when it starts to flash. Pure (no GL); OutputProbe measures, CompositePass and NoteBloomPass apply.
//
// Why measure the output: every earlier guard (VisualSafety's clamps, the LFO rate cap, the old
// FlashBudget) limited the *parameters* that could make a flash. The pictures themselves come from
// hundreds of style shaders, MilkDrop presets and simulations the params do not describe, and the old
// budget's one input was hard 0 (fosfora study, PR #300), so nothing limited a strobe a style drew
// itself. This reads the frames that went out instead.
//
// What counts, after WCAG 2.2's general and red flash thresholds (the guidance PEAT and the Harding
// test descend from): a *transition* is a swing in relative luminance of at least 0.1 (the darker side
// below 0.8), or a swing of at least 20/320 in saturated red ((R-G-B) where R/(R+G+B) >= 0.8); a flash
// is a pair of opposing transitions; at most three flashes in any second. Measured over the whole frame
// and over each quarter (the threshold's area is a quarter of the central field), so a strobing corner
// counts even when the whole frame's mean barely moves.
//
// The measurement lags the screen by one or two frames (it is read back asynchronously so it never
// stalls the GPU). So the limiter does not wait for the third flash: it engages at four transitions in a
// second (two flashes), which leaves the lag a flash of margin under the three a second allowed.
//
// What it does: the grade (Grade below) squeezes the picture toward one grey, in linear light, so that
// no frame-to-frame swing can reach a transition while held: every channel moves at most kHeldScale of
// what it would have. It engages at once, holds while the source would still flash (the measurement is
// unsqueezed through the grade it was drawn with, so the limiter keeps seeing the strobe it is hiding),
// and lets go over kReleaseSeconds once a whole second has passed with at most one transition. Slow
// fades, a pulse on the beat, a cut now and then never come near it and pass untouched (scale exactly 1).
//
// The drive (issue #363): the envelopes (ADSR) and LFO slots on a luminance target (Brightness,
// Intensity, Saturation, Bloom, Vignette, Colour shift, Palette mix, Warmth) are known the frame they are
// applied, so they are counted here too, one counter per target, beside the measured regions. A strobe
// they drive engages the hold the very frame it is applied, never waits out the readback's lag or a frame
// the probe skipped, and keeps the hold while they still swing. The hold itself is the one grade above:
// there is no second limiter, and the measurement still catches whatever the drive does not describe.

// The grade one frame is drawn with: lin' = anchor + (lin - anchor) * scale, per channel, in linear
// light. scale 1 is the picture untouched.
struct Grade {
    float scale = 1.0f;
    float anchor = 0.0f;
    bool untouched() const { return scale >= 1.0f; }
};

// One measured frame: each region's mean linear RGB. Region 0 is the whole frame, 1..4 its quarters.
struct FrameLight {
    static constexpr int kRegions = 5;
    std::array<std::array<float, 3>, kRegions> rgb{};
};

// The measuring grid OutputProbe reads back: side × side cells, RGBA8, each a box mean of the frame.
constexpr int kProbeGrid = 16;

// Display-encoded 0..1 to linear light and back; the grade and the measurement share this curve.
float toLinear(float encoded);
float toEncoded(float linear);
float luminance(const std::array<float, 3>& linearRgb);
// The saturated-red value WCAG's red flash counts: R-G-B where red is at least 80 % of R+G+B, else 0.
float saturatedRed(const std::array<float, 3>& linearRgb);

// Builds a FrameLight from an RGBA8 grid of side × side cells (rows in either order), cell by cell in
// linear light so a half-white half-black frame reads as half the light, not as mid-grey's quarter.
FrameLight measureGrid(const uint8_t* rgba, int side);

// A WCAG transition counter for one signal: counts each opposing swing of at least `threshold` once,
// the frame it crosses. A swing whose darker end is at or above `darkCeiling` does not count and does
// not turn the counter (WCAG ignores flashes between two bright levels, but the level may carry on into
// the dark, and that longer swing must still count).
class TransitionCounter {
public:
    void reset();
    // True when this sample completes a transition.
    bool step(float value, float threshold, float darkCeiling);

private:
    bool started_ = false;
    int dir_ = 0;  // the last counted swing: +1 rising, -1 falling, 0 none yet
    float lo_ = 0.0f;
    float hi_ = 0.0f;
};

class FlashLimiter {
public:
    static constexpr float kLumaThreshold = 0.1f;
    static constexpr float kLumaDarkCeiling = 0.8f;
    static constexpr float kRedThreshold = 20.0f / 320.0f;
    static constexpr float kWindowSeconds = 1.0f;
    // Four transitions are two flashes: the margin under three a second the measuring lag needs.
    static constexpr int kEngageTransitions = 4;
    // Let go only once the source has been all but still for a second (hysteresis, so a source near
    // the limit does not toggle the hold on and off).
    static constexpr int kReleaseTransitions = 1;
    // Held, a frame moves at most this much of what the source moved: under both thresholds above.
    static constexpr float kHeldScale = 0.05f;
    static constexpr float kReleaseSeconds = 1.5f;
    // How fast the held grey may follow the source's average: slow enough to never read as a flash.
    static constexpr float kAnchorDriftPerSecond = 0.2f;
    // A swing of a luminance target's modulation that counts as a transition, in the target's own units
    // (applyLfoTarget adds the modulation about 1:1 to brightness, intensity, saturation, bloom, ...). The
    // same depth VisualSafety lets any one flash reach (safety::kMaxFlashDepth); RendererFrame asserts it.
    static constexpr float kDriveThreshold = 0.25f;
    // One drive per luminance target (viz/Lfo.hpp kLuminanceTargets; RendererFrame asserts it).
    static constexpr int kDrives = 8;
    // The readback is 8-bit: unsqueezing a held frame multiplies its quantization by 1/scale, so the
    // swing that counts is raised to stay above that noise (a missed count only holds a little longer).
    static constexpr float kQuantizationFloor = 0.008f;

    void reset();

    // One measured frame: what was on screen (`shown`), the grade it was drawn with and its time.
    void observe(const FrameLight& shown, const Grade& drawnWith, double drawnAt);

    // The grade for the frame drawn at `now` (seconds, never decreasing; a step back resets).
    Grade next(double now);

    // The luminance modulation the envelopes and LFOs apply to the frame drawn at `now`, per target
    // (summed when several drive one). Call before next(now) so the frame it is applied to is graded by it.
    void drive(const std::array<float, kDrives>& modulation, double now);

    bool held() const { return held_; }
    // The most transitions any region's luminance or red, or any drive, counted in the last second, as of `now`.
    int transitionsInWindow(double now);

private:
    static constexpr int kSignals = FrameLight::kRegions * 2;  // luminance and red per region
    static constexpr int kCapacity = 32;

    struct Signal {
        TransitionCounter counter;
        std::array<double, kCapacity> times{};
        int head = 0;
        int count = 0;
        void record(double t);
        void dropOlderThan(double cutoff);
    };

    std::array<Signal, kSignals> signals_{};
    std::array<Signal, kDrives> drives_{};
    double lastDriveAt_ = -1.0;
    bool held_ = false;
    float scale_ = 1.0f;
    float anchor_ = 0.0f;
    // The source's whole-frame luminance: the last one, and a one-second average the held grey follows.
    float lastSource_ = 0.0f;
    float sourceMean_ = 0.0f;
    bool seen_ = false;
    double lastNow_ = -1.0;
    double lastSeenAt_ = -1.0;
};

}  // namespace geode::viz::safety
