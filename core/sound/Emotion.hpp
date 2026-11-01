#pragma once
#include <array>

#include "sound/SoundFrame.h"

namespace geode::sound {

// What the music feels like right now, from the sound frame the musical layer already fills. It never
// names the feeling to the user; the auto-director (viz/Director) turns it into palette, motion, camera
// and routing decisions.
//
//   key + scale ─► fingerprint (EmotionTable.inc: three quarters the mode's measured emotion, a quarter
//                  the key's historical character) ─► glides over ~3 s
//   loudness, tempo, onset rate, brightness, harmony tension, build-up ─► live energy and tension
//   the strongest note against the tonic ─► scale-degree accent (Huron's qualia)
//
// The cues add (Eerola, Friberg & Bresin 2013: no interaction among six cues, linear models R² .77-.89),
// so each axis is a weighted sum. The feeling has two layers: three axes (valence, energy, tension) say
// how much, and GEMS-9 (Zentner et al. 2008), the music-specific emotion scale, says what kind.
class Emotion {
public:
    enum Gems { kWonder, kTranscendence, kTenderness, kNostalgia, kPeace, kPower, kJoy, kTension, kSadness };
    static constexpr int kGems = 9;
    using GemsVec = std::array<float, kGems>;

    struct Mode {
        float valence;  // -1..1
        float energy;   // 0..1
        float tension;  // 0..1
        bool majorThird;
        GemsVec gems;   // sums to 1
    };
    struct Key {
        float sharpness;  // -1 six flats .. 1 six sharps
        GemsVec gems;
    };
    static int modeCount();
    static const Mode& mode(int scale);
    // root 0..11; the major or minor list by the scale's third.
    static const Key& key(int root, bool major);
    static constexpr float kModeWeight = 0.75f;
    // The fingerprint of one key + scale: the table's mix, before any live cue.
    struct Fingerprint {
        float valence = 0.0f;
        float energy = 0.5f;
        float tension = 0.3f;
        float sharpness = 0.0f;
        GemsVec gems{};
    };
    static Fingerprint fingerprint(int scale, int root);

    struct State {
        float valence = 0.0f;   // -1 sad .. 1 happy
        float energy = 0.0f;    // 0 still .. 1 driving
        float tension = 0.0f;   // 0 at rest .. 1 strained
        float sharpness = 0.0f; // the key's side of the circle of fifths, -1..1: mellow .. brilliant
        GemsVec gems{};         // what kind of feeling, sums to 1
        int scale = -1;         // the fingerprint's scale and root, -1 while unsure
        int root = 0;
        float confidence = 0.0f; // how much the fingerprint is trusted, 0..1
        int degree = -1;          // the leading note against the tonic, 0..11, -1 none
        float degreeValence = 0.0f; // its pull, held while it sounds
        float degreeTension = 0.0f;
        float degreeAccent = 0.0f;  // trigger: the leading degree changed
    };

    void step(const GeodeSoundFrame& f, float dt);
    const State& state() const { return state_; }
    void reset() { *this = Emotion(); }

private:
    void stepFingerprint(const GeodeSoundFrame& f, float dt);
    void stepDegree(const GeodeSoundFrame& f, float dt);

    State state_;
    Fingerprint print_;       // the gliding fingerprint
    bool primed_ = false;
    int candidateScale_ = -1;
    int candidateRoot_ = 0;
    float candidateSeconds_ = 0.0f;
    float onsetRate_ = 0.0f;  // onsets a second, smoothed
    float lastOnset_ = 0.0f;
    float valence_ = 0.0f;    // the axes before smoothing reaches the state
    int degreeCandidate_ = -1;
    float degreeSeconds_ = 0.0f;
};

}  // namespace geode::sound
