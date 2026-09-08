#pragma once
#include "api/geode_api.h"
#include "viz/Smoothing.hpp"

namespace geode::viz {

class UniformCache;

// The music-shaped half of the smoothed motion layer.
//
// ShaderScene::stepMotion smooths the LEVEL of the music: how loud, how
// bright, whether something just hit. That is one axis, and a picture driven
// by one axis has one kind of reaction - it gets bigger, or brighter, or it
// jumps. The analyzer already measures a great deal more than level, and none
// of it reached a shader: which drum hit, whether the track is building
// toward something or has just dropped, whether the passage is tonal or
// percussive, what key it is in, where in the bar we are. This class turns
// those into signals a style can read, so a kick and a hi-hat can do two
// DIFFERENT things to the picture instead of both meaning "louder".
//
// Every value here is either slew-limited or an attack/release envelope: none
// of them can step in a single frame, for the same reason nothing in
// lib_scene_motion can. The one-hop impulses the analyzer reports (kick,
// snare, hat, drop, arrival, section boundary, downbeat) are fed through
// HitEnvelope, which is what makes them reach their peak at all - see the note
// on that class.
//
// The uniform each value arrives as is documented in lib_scene_motion.glsl.
class MusicSignals {
public:
    MusicSignals();

    void reset();
    void step(const GeodeFeatureFrame& f, float dt);
    void upload(UniformCache& uniforms) const;

    // ---- drums: three envelopes with three different time signatures -------
    float kick() const { return kick_.value(); }
    float snare() const { return snare_.value(); }
    float hat() const { return hat_.value(); }

    // ---- structure ----------------------------------------------------------
    float build() const { return build_; }
    float drop() const { return drop_.value(); }
    float arrival() const { return arrival_.value(); }
    float novelty() const { return novelty_; }
    // 0..1, a plateau on the golden-ratio walk, re-stepped at every section
    // boundary and every drop. "Which section is this", as a dial.
    float sectionPhase() const { return sectionPhase_; }

    // ---- rhythm ---------------------------------------------------------------
    float barPhase() const { return barPhase_; }
    float beatInBar() const { return beatInBar_; }
    // How much the bar clock can be trusted: pulse confidence times tempo
    // stability, slewed. A style fades bar-locked motion in with this.
    float rhythmLock() const { return rhythmLock_; }

    // ---- tonality -------------------------------------------------------------
    float harmonic() const { return harmonic_; }
    float brightness() const { return brightness_; }
    // 0..1 around the circle of pitch classes (C = 0, C# = 1/12, ...), as an
    // angle that glides the short way round, so a modulation to the dominant
    // reads as a small turn rather than a wrap.
    float keyHue() const { return keyHue_; }
    float keyStrength() const { return keyStrength_; }

    // ---- stereo ---------------------------------------------------------------
    float pan() const { return pan_; }
    float width() const { return width_; }

    // ---- what fired THIS frame, after the envelopes have been fed --------------
    //
    // For choreography that wants the event rather than the envelope: the
    // superformula re-picks a lobe count on a snare and resets on a drop.
    bool snareFired() const { return snareFired_; }
    bool dropFired() const { return dropFired_; }
    bool arrivalFired() const { return arrivalFired_; }
    bool sectionFired() const { return sectionFired_; }
    bool downbeatFired() const { return downbeatFired_; }

private:
    // Drum envelopes. Rise is the same for all three; the FALL is what tells
    // them apart on screen - a kick thuds and lingers, a snare cracks, a hat
    // is over almost before it started.
    static constexpr float kDrumRiseHz = 16.0f;
    static constexpr float kKickFallHz = 3.0f;
    static constexpr float kSnareFallHz = 5.0f;
    static constexpr float kHatFallHz = 9.0f;
    // A drum below this strength is ignored: the pickers fire on soft ghost
    // notes too, and a shape that re-aims on every one of them dithers.
    static constexpr float kDrumFloor = 0.15f;

    // Structure. A drop is a STATE the picture stays in for a while, not a hit,
    // so its release is the slowest of the envelopes here. An arrival - music
    // returning after silence - is somewhere between the two.
    static constexpr float kBuildHz = 0.5f;
    static constexpr float kDropRiseHz = 6.0f;
    static constexpr float kDropFallHz = 0.35f;
    static constexpr float kArrivalRiseHz = 3.0f;
    static constexpr float kArrivalFallHz = 0.6f;
    static constexpr float kNoveltyHz = 2.0f;
    static constexpr float kSectionStep = 0.6180339f;
    static constexpr float kSectionGlideHz = 0.7f;

    // Rhythm.
    static constexpr float kRhythmLockRiseHz = 0.8f;
    static constexpr float kRhythmLockFallHz = 2.0f;

    // Tonality. The chroma is already attack/release smoothed in the analyzer;
    // the hue glide here is what stops a momentary bass note from swinging the
    // colour of the whole frame.
    static constexpr float kHarmonicHz = 1.5f;
    static constexpr float kBrightnessHz = 4.0f;
    static constexpr float kKeyHueHz = 1.2f;
    static constexpr float kKeyStrengthRiseHz = 2.0f;
    static constexpr float kKeyStrengthFallHz = 0.8f;
    // Below this chroma confidence the key is not read at all: a noise floor
    // has a "dominant" pitch class too, and it is random.
    static constexpr float kKeyConfidenceFloor = 0.2f;

    // Stereo.
    static constexpr float kStereoHz = 2.0f;

    // The circular mean of the chroma bins, as a hue 0..1; -1 when there is
    // not enough energy to call one.
    static float chromaHue(const float* chroma);

    HitEnvelope kick_;
    HitEnvelope snare_;
    HitEnvelope hat_;
    float build_ = 0.0f;
    HitEnvelope drop_;
    HitEnvelope arrival_;
    float novelty_ = 0.0f;
    float sectionPhase_ = 0.0f;
    float sectionTarget_ = 0.0f;
    float barPhase_ = 0.0f;
    float beatInBar_ = 0.0f;
    float rhythmLock_ = 0.0f;
    float harmonic_ = 0.5f;
    float brightness_ = 0.0f;
    float keyHue_ = 0.0f;
    float keyStrength_ = 0.0f;
    float pan_ = 0.0f;
    float width_ = 0.0f;
    bool snareFired_ = false;
    bool dropFired_ = false;
    bool arrivalFired_ = false;
    bool sectionFired_ = false;
    bool downbeatFired_ = false;
};

}  // namespace geode::viz
