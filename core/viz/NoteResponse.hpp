#pragma once
#include <array>
#include <cstdint>

#include "sound/Emotion.hpp"
#include "sound/SoundFrame.h"
#include "viz/Params.hpp"

namespace geode::viz {

// Note & key reactions (Settings › Behaviour, free and on by default; its tuning card is Pro): the picture answering the music's notes, chords, key
// and mood as they happen, not only its drums. docs/research/note-key-response.md has the design and the
// reasons for every number below; the short version:
//
//   notes ─► a colour per note (pitch class on the circle of fifths around the key's colour, so a
//            melody paints a spread of hues, never one), a shape per height (high notes open and stretch
//            up, low ones pull in), a bloom per note onset (a paradoxical fractal flower drawn over every
//            style by NoteBloomPass), and motion per interval (leaps swing and twist, steps glide)
//   key, mood ─► the palette family when the auto-director is off (major warm and bright, minor cool and
//            deep); under the director, its look's hue is the anchor and this layer only adds the notes
//   harmony ─► tension bends the picture (warp, chromatic split, frayed petal edges) and its resolution
//            smooths it back; a chord change is a scene shift (fold count, bloom layout)
//   motion ─► a leap rolls and turns the camera, a build pushes it in (camRoll, camYaw, camPush, so the rig
//            springs toward them and the safety clamp and reduced motion act on top), and the blooms sit
//            at depths: near ones drift outward and grow past the viewer, far ones barely move, and all of
//            them slide against the camera's pan (parallax), so the field of flowers has a sense of travel
//   drums ─► weighted by how percussive the track is, measured continuously (hit and onset rate,
//            spectral flatness, high-frequency content against pitch confidence, key clarity, scale fit)
//
// It also reads six knobs by meaning (k0 PULSE … k5 SPACE), each from several fields of the frame and
// smoothed at a musical rate. No shader reads them any more (the neon set moved to the shared params,
// lib_neon), so they are the layer's own readings: TONE shapes morph, and the others stay in
// uniforms().knobs for the tests and for a style or the mod matrix that wants them later.
//
// Order in the frame (RendererFrame.cpp): the director's look, then this layer, then the macros, LFOs,
// envelopes, reactions, motion field and the safety clamp. So the user's own settings and the flash and
// motion limits always act on top, and the director keeps the scene-level palette and camera to itself.
//
// Variation: each play gets a personality (how many blooms at once, how fast they may come, how big, how
// strongly the notes tint the whole picture, how fast the field travels, how far the flowers tilt), and
// every new section and every drop rolls it again, eased over a couple of seconds. The user chose this to
// be truly random per play: the renderer mixes fresh entropy into the seed at each new play
// (Renderer::noteSeed), so the same song looks different every time, and an export is its own play.
//
// Real time only: everything reads the frame just heard; nothing anticipates. Given a seed it is a pure
// function of (seed, frames, dt): no clock, no randomness of its own (splitmix64 keyed by the seed and the
// note, chord and section counts), which is what the tests pin. Process-wide switch, like the director's.
struct NoteResponseConfig {
    bool enabled = false;
    uint64_t seed = 0;
};

namespace note_response {
void setGlobal(bool enabled);
bool globalEnabled();
}  // namespace note_response

// What the scenes and the bloom pass read this frame. Plain arrays so the GL side uploads them as is.
struct NoteUniforms {
    static constexpr int kKnobs = 6;
    static constexpr int kHues = 6;
    static constexpr int kBlooms = 8;
    // k0 PULSE, k1 TEXTURE, k2 FLOW, k3 TONE, k4 BUILD, k5 SPACE; 0.5 is the midpoint each style was tuned
    // around, and exactly 0.5 while the layer is off.
    std::array<float, kKnobs> knobs{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    // The last notes' colours, newest first, on the colour wheel (red 0, yellow 1/6, green 1/3, blue 2/3).
    std::array<float, kHues> hues{};
    // How much the layer drives right now: the switch, eased in and out over ~0.7 s.
    float amount = 0.0f;
    // Live blooms, packed: a = (x, y, radius, hue), b = (open, envelope, petals, depth),
    // c = (spin, stretch, seed, tension). x, y in 0..1 of the frame (y up); radius in short sides.
    std::array<std::array<float, 4>, kBlooms> bloomA{};
    std::array<std::array<float, 4>, kBlooms> bloomB{};
    std::array<std::array<float, 4>, kBlooms> bloomC{};
    // d = (tilt x, tilt y, depth, unused): the flower's disc turned in 3D (radians about the screen's x and
    // y axes, so it foreshortens into an ellipse) and how far it sits, 0 near .. 1 far.
    std::array<std::array<float, 4>, kBlooms> bloomD{};
    int bloomCount = 0;
    // Which of lib_bloom's four looks draws them (viz/Catalogue BloomLook: 1 flower, 2 glow, 3 ripple,
    // 4 burst), resolved from the style's own and the user's pick.
    int look = 1;
    // The whole picture's pull toward the notes' colour: rgb (unit luma) and amount, 0 while off. The
    // composite pass mixes every family's finished frame toward it, keeping each pixel's brightness.
    std::array<float, 4> tint{1.0f, 1.0f, 1.0f, 0.0f};
};

class NoteResponse {
public:
    // What the layer reads besides the frame: the director's anchor hue and how much it drives.
    struct Inputs {
        float anchorHue = 0.0f;
        float anchorWeight = 0.0f;
        // The style's Customize › Advanced › Note & key amounts (SceneParams::note*), 0..2, 1 as designed.
        float color = 1.0f;
        float pitch = 1.0f;
        float intervals = 1.0f;
        float tension = 1.0f;
        float chords = 1.0f;
        float drums = 1.0f;
        float bloom = 1.0f;
        // The resolved bloom look, 1..4 (Catalogue::resolveBloomLook).
        int look = 1;
        // Last frame's CameraRig flat pose (panX, panY, zoom, roll), for the blooms' parallax.
        std::array<float, 4> camFlat{0.0f, 0.0f, 1.0f, 0.0f};
    };

    // The play's character, rolled per play and again at every section and drop (see the class comment).
    struct Personality {
        float maxAlive = 8.0f;   // blooms on screen at once, 3..8
        float budget = 3.0f;     // new blooms a second before each is dimmed, 1.5..3 (never past WCAG's 3)
        float size = 1.0f;       // radius scale, 0.75..1.35
        float tint = 0.15f;      // whole-picture pull toward the notes' colour, 0.04..0.3
        float travel = 0.5f;     // how fast near blooms drift out past the viewer, 0.15..1
        float tilt = 0.4f;       // how far the flowers' discs turn in 3D, 0..0.9 radians
    };

    struct Bloom {
        bool alive = false;
        float x = 0.5f;
        float y = 0.5f;
        float radius = 0.15f;
        float hue = 0.0f;
        float stretch = 1.0f;
        float petals = 6.0f;
        float depth = 3.0f;
        float spin = 0.0f;
        float seed = 0.0f;
        float strength = 1.0f;
        float age = 0.0f;
        float release = -1.0f;  // seconds into its fade; < 0 while its note still sounds
        float releaseSeconds = 1.0f;
        float tension = 0.0f;
        float openSeconds = 0.7f;  // the look's unfurl time
        float depth3 = 0.5f;       // 0 near .. 1 far: parallax and travel
        float tiltX = 0.0f;
        float tiltY = 0.0f;
        int note = -1;          // the MIDI note it was born on
        int index = 0;          // the spawn count it was born at, for ordering
        float envelope() const;
        float open() const;
    };

    void configure(const NoteResponseConfig& config);
    void step(const GeodeSoundFrame& f, const sound::Emotion::State& e, const Inputs& in, float dt);
    // Between the director's look and the macros: the note- and chord-level life on shared params.
    SceneParams apply(const SceneParams& p) const;
    const NoteUniforms& uniforms() const { return out_; }
    void reset();

    // For tests and the doc.
    float amount() const { return amount_; }
    float percussiveness() const { return perc_; }
    float distortion() const { return distortion_; }
    float swing() const { return swing_; }
    float glide() const { return glide_; }
    int symmetryShift() const { return symShift_; }
    int sceneShifts() const { return shifts_; }
    int spawned() const { return spawned_; }
    int liveBlooms() const;
    float anchorHue() const { return anchor_; }
    const Personality& personality() const { return mood_; }
    int rolls() const { return rolls_; }
    const std::array<Bloom, NoteUniforms::kBlooms>& blooms() const { return blooms_; }

private:
    void measurePercussiveness(const GeodeSoundFrame& f, float dt);
    void stepKnobs(const GeodeSoundFrame& f, const sound::Emotion::State& e, float dt);
    void stepHarmony(const GeodeSoundFrame& f, const sound::Emotion::State& e, float dt);
    void stepIntervals(const GeodeSoundFrame& f, float dt);
    void stepBlooms(const GeodeSoundFrame& f, const sound::Emotion::State& e, float dt);
    void spawn(const GeodeSoundFrame& f, const sound::Emotion::State& e);
    void roll();
    void stepPersonality(const GeodeSoundFrame& f, float dt);
    float noteHue(float pitchClass, float pitchHeight) const;
    void publish();

    NoteResponseConfig config_;
    bool configured_ = false;
    float amount_ = 0.0f;
    Inputs in_;

    // The play's character: the current one easing toward the last roll.
    Personality mood_;
    Personality moodTarget_;
    int rolls_ = 0;
    float tintHue_ = 0.0f;  // the notes' colour, a circular mean of the last notes, smoothed
    bool tintPrimed_ = false;

    // Percussive vs tonal, continuously: leaky rates and slow means.
    float hitRate_ = 0.0f;     // drum triggers a second
    float onsetRate_ = 0.0f;   // onsets a second
    float flatness_ = 0.0f;
    float hfc_ = 1.0f;
    float pitchConf_ = 0.0f;
    float keyClarity_ = 0.0f;
    float scaleFit_ = 0.0f;
    float perc_ = 0.5f;

    std::array<float, NoteUniforms::kKnobs> knobs_{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    float tone_ = 0.5f;        // the held melodic height (k3 before smoothing)
    float drum_ = 0.0f;        // percussive drive this frame, already weighted
    float distortion_ = 0.0f;  // tension bend, 0..1, eased back on resolution
    float build_ = 0.0f;       // a build under way, eased: the camera's push-in
    float keyWarmth_ = 0.0f;   // -1 minor/cool .. 1 major/warm, times key clarity
    float valence_ = 0.0f;
    float anchor_ = 0.0f;      // the hue the notes spread around
    float anchorIn_ = 0.0f;    // the director's look hue, this frame
    float anchorWeight_ = 0.0f;
    float keyRoot_ = 0.0f;     // the last trusted tonic, C = 0 .. B = 11/12
    bool primed_ = false;

    // Chord changes.
    int shifts_ = 0;
    int symShift_ = 0;
    float layout_ = 0.0f;
    float sinceShift_ = 10.0f;

    // Intervals: a damped spring kicked by leaps, a leaky glide fed by steps.
    float swing_ = 0.0f;
    float swingVel_ = 0.0f;
    float glide_ = 0.0f;

    std::array<float, NoteUniforms::kHues> hues_{};
    int hueCount_ = 0;
    std::array<Bloom, NoteUniforms::kBlooms> blooms_{};
    int spawned_ = 0;
    float unvoiced_ = 0.0f;
    float recentSpawns_ = 0.0f;  // leaky count, for the flash budget

    NoteUniforms out_;
};

}  // namespace geode::viz
