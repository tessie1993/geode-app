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
//            melody paints a spread of hues, never one) that the whole picture leans toward, a shape per
//            height (high notes open the morph, low ones close it), and motion per interval (leaps swing
//            and twist, steps glide). The note blooms that once flowered over every style are gone (the
//            owner dropped them); the notes reach the picture only through the style itself.
//   key, mood ─► the palette family when the auto-director is off (major warm and bright, minor cool and
//            deep); under the director, its look's hue is the anchor and this layer only adds the notes
//   harmony ─► tension bends the picture (warp, chromatic split, frayed petal edges) and its resolution
//            smooths it back; a chord change is a scene shift (fold count, a turn)
//   motion ─► a leap rolls and turns the camera, a build pushes it in (camRoll, camYaw, camPush, so the rig
//            springs toward them and the safety clamp and reduced motion act on top)
//   drums ─► weighted by how percussive the track is, measured continuously (hit and onset rate,
//            spectral flatness, high-frequency content against pitch confidence, key clarity, scale fit)
//
// It also drives the six style knobs (lib_scene_uniforms' uKnob, k0 PULSE … k5 SPACE) by meaning, each
// from several fields of the frame, smoothed at a musical rate; a style reads them as its own offsets
// (the neon set through lib_neon's neonKnob), and off they are exactly 0.5.
//
// Order in the frame (signal/Resolver, the scene params resolver): the director's look, then this layer, then the director's axes and the macro bank, LFOs,
// envelopes, reactions, motion field and the safety clamp. So the user's own settings and the flash and
// motion limits always act on top, and the director keeps the scene-level palette and camera to itself.
//
// Variation: each play gets a personality (how strongly the notes tint the whole picture), and
// every new section and every drop rolls it again, eased over a couple of seconds. The user chose this to
// be truly random per live play: a live renderer mixes fresh entropy into the seed at each new play
// (signal::SettingsLatch, EngineSettings::noteEntropy), so the same song looks different every time. An
// export pins its settings with no entropy (#350), so it is deterministic: the same play exported twice
// draws the same note response, though not the one the live screen drew.
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

// What the scenes and the composite read this frame. Plain arrays so the GL side uploads them as is.
struct NoteUniforms {
    static constexpr int kKnobs = 6;
    static constexpr int kHues = 6;
    // k0 PULSE, k1 TEXTURE, k2 FLOW, k3 TONE, k4 BUILD, k5 SPACE; 0.5 is the midpoint each style was tuned
    // around, and exactly 0.5 while the layer is off.
    std::array<float, kKnobs> knobs{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    // The last notes' colours, newest first, on the colour wheel (red 0, yellow 1/6, green 1/3, blue 2/3).
    std::array<float, kHues> hues{};
    // How much the layer drives right now: the switch, eased in and out over ~0.7 s.
    float amount = 0.0f;
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
    };

    // The play's character, rolled per play and again at every section and drop (see the class comment).
    struct Personality {
        float tint = 0.15f;  // whole-picture pull toward the notes' colour, 0.04..0.3
    };

    void configure(const NoteResponseConfig& config);
    void step(const GeodeSoundFrame& f, const sound::Emotion::State& e, const Inputs& in, float dt);
    // Between the director's look and its axes: the note- and chord-level life on shared params.
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
    int notesHeard() const { return notes_; }
    float anchorHue() const { return anchor_; }
    const Personality& personality() const { return mood_; }
    int rolls() const { return rolls_; }

private:
    void measurePercussiveness(const GeodeSoundFrame& f, float dt);
    void stepKnobs(const GeodeSoundFrame& f, const sound::Emotion::State& e, float dt);
    void stepHarmony(const GeodeSoundFrame& f, const sound::Emotion::State& e, float dt);
    void stepIntervals(const GeodeSoundFrame& f, float dt);
    void hearNotes(const GeodeSoundFrame& f);
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
    float gain_ = 1.0f;        // core/sound/DriveGain: the music's absolute reach, scales the pulse, flow, build and space knobs
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
    float sinceShift_ = 10.0f;

    // Intervals: a damped spring kicked by leaps, a leaky glide fed by steps.
    float swing_ = 0.0f;
    float swingVel_ = 0.0f;
    float glide_ = 0.0f;

    std::array<float, NoteUniforms::kHues> hues_{};
    int hueCount_ = 0;
    int notes_ = 0;  // note onsets heard this play

    NoteUniforms out_;
};

}  // namespace geode::viz
