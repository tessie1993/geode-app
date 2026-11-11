#pragma once
#include <array>
#include <cstdint>

#include "sound/Emotion.hpp"
#include "sound/SoundFrame.h"
#include "viz/Params.hpp"

namespace geode::viz {

// Note & key reactions (Settings › Behaviour, free and on by default; its tuning card is Pro): the music's
// notes, chords, key and mood, not only its drums, read into the six style knobs (lib_scene_uniforms'
// uKnob, k0 PULSE … k5 SPACE). Each knob is several fields of the frame by meaning, smoothed at a musical
// rate; a style spends them on its own geometry (the neon set through lib_neon's neonKnob), so the same
// knob looks different on every style, and off they are exactly 0.5.
//
// That is all it does (owner's rule: no hardcoded reactions, no colour narrowing). It writes no scene
// param and no colour: the twist and turn on a leap, the bend on tension, the fold shift on a chord, the
// zoom on drums and the tint toward the notes' colour that it once laid over every style alike are gone.
// A style moves on those moments only through the reaction matrix (the user's routes, else the style's
// DNA), where the user can see and edit them.
//
// The style's Customize › Music › Notes & key amounts scale what feeds the knobs: Drums the pulse, Pitch
// height the tone, Tension the build, Leaps and steps and Chord changes the swing behind space.
//
// Real time only: everything reads the frame just heard; nothing anticipates. Given a seed it is a pure
// function of (seed, frames, dt): no clock, no randomness of its own (splitmix64 keyed by the seed and the
// chord count, which way a chord change swings), which is what the tests pin. A live renderer mixes fresh
// entropy into the seed at each new play (signal::SettingsLatch), an export none (#350). Process-wide
// switch, like the director's.
struct NoteResponseConfig {
    bool enabled = false;
    uint64_t seed = 0;
};

namespace note_response {
void setGlobal(bool enabled);
bool globalEnabled();
}  // namespace note_response

// What the scenes read this frame. A plain array so the GL side uploads it as is.
struct NoteUniforms {
    static constexpr int kKnobs = 6;
    // k0 PULSE, k1 TEXTURE, k2 FLOW, k3 TONE, k4 BUILD, k5 SPACE; 0.5 is the midpoint each style was tuned
    // around, and exactly 0.5 while the layer is off.
    std::array<float, kKnobs> knobs{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    // How much the layer drives right now: the switch, eased in and out over ~0.7 s.
    float amount = 0.0f;
};

class NoteResponse {
public:
    // The style's Customize › Music › Notes & key amounts (SceneParams::note*), 0..2, 1 as designed.
    struct Inputs {
        float pitch = 1.0f;
        float intervals = 1.0f;
        float tension = 1.0f;
        float chords = 1.0f;
        float drums = 1.0f;
    };

    void configure(const NoteResponseConfig& config);
    void step(const GeodeSoundFrame& f, const sound::Emotion::State& e, const Inputs& in, float dt);
    const NoteUniforms& uniforms() const { return out_; }
    void reset();

    // For tests and the doc.
    float amount() const { return amount_; }
    float percussiveness() const { return perc_; }
    float swing() const { return swing_; }
    float glide() const { return glide_; }
    int chordShifts() const { return shifts_; }

private:
    void measurePercussiveness(const GeodeSoundFrame& f, float dt);
    void stepKnobs(const GeodeSoundFrame& f, const sound::Emotion::State& e, float dt);
    void stepChords(const GeodeSoundFrame& f, float dt);
    void stepIntervals(const GeodeSoundFrame& f, float dt);
    void publish();

    NoteResponseConfig config_;
    bool configured_ = false;
    float amount_ = 0.0f;
    Inputs in_;

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

    // Chord changes.
    int shifts_ = 0;
    float sinceShift_ = 10.0f;

    // Intervals: a damped spring kicked by leaps, a leaky glide fed by steps.
    float swing_ = 0.0f;
    float swingVel_ = 0.0f;
    float glide_ = 0.0f;

    NoteUniforms out_;
};

}  // namespace geode::viz
