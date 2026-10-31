#pragma once
#include <array>
#include <cstdint>

namespace geode::sound {

// What the harmony stage hears in one frame: where the music sits harmonically, as continuous values.
//
// There is no key LABEL here on purpose. The sound engine never commits to a tempo or a key it might
// have guessed wrong (docs/research/music-visual-mapping.md); a colour that snaps between C major and
// A minor every few seconds would show the guess, not the music. Instead the slow pitch-class profile
// is scored against all 24 Krumhansl-Kessler key profiles and the scores are blended: the tonal centre
// glides round the circle of fifths as the harmony drifts, and an ambiguous passage reads as a weak
// clarity rather than a confident wrong answer. A clear key still lands exactly on its fifth.
//
// Plain floats only, like GeodeSoundFrame, so it can cross JNI as one array if a surface needs it.
struct GeodeHarmony {
    // Pitch-class profiles, C = 0 … B = 11, each normalised so its largest class is 1.
    std::array<float, 12> chroma{};  // the sound now (~0.15 s): the chord being played
    std::array<float, 12> home{};    // the song so far (~10 s): what the key is made of
    // The home key, blended across all 24 keys by how well each fits.
    float fifths = 0.0f;   // its key signature on the circle of fifths, in turns: C/Am 0, G/Em 1/12, D 2/12 …
    float tonic = 0.0f;    // its tonic pitch class, in turns (C 0, C# 1/12 …): A minor reads 9/12, not 0
    float mode = 0.5f;     // 0 minor … 1 major
    float clarity = 0.0f;  // 0 no key (noise, drums, atonal) … 1 one key clearly wins
    // The current harmony against the home key.
    float chordFifths = 0.0f;    // the sound now on the circle of fifths, in turns
    float chordStrength = 0.0f;  // 0 spread over every class … 1 a few classes a fifth apart (a triad)
    float tension = 0.0f;        // 0 at home (diatonic, consonant) … 1 far from it (chromatic, clashing)
    float roughness = 0.0f;      // 0..1, semitone and tritone clashes in the sound now
    float change = 0.0f;         // trigger: the harmony just moved (a chord change), strength 0..1
    float motion = 0.0f;         // chord changes per second over ~8 s, halved: 0 static … 1 two a second
    float tonal = 0.0f;          // 0..1, how much of the sound now is pitched at all (gates everything above)
};

// The harmony stage: spectral peaks of the sound engine's long FFT folded into pitch classes, smoothed at
// three speeds (now, the chord, the song), then scored against the key profiles.
//
// Frame-locked like every other stage: each smoothing step scales by dt, so export at 1/fps hears what
// the screen heard, and the same audio gives the same values on every run.
class Harmony {
public:
    // `mag`: the long window's magnitudes, bins 0..count-1, `binHz` apart. `flatness` and `silent` come
    // from the frame (tonal 0 … noise 1, and digital silence): noise and silence hold the last harmony.
    void step(const float* mag, int count, float binHz, float flatness, bool silent, float dt);
    const GeodeHarmony& frame() const { return out_; }
    void reset() { *this = Harmony(); }

    // Krumhansl & Kessler (1982) probe-tone ratings, tonic first. Public for the tests.
    static const std::array<float, 12>& majorProfile();
    static const std::array<float, 12>& minorProfile();

private:
    void scoreKeys(float dt);
    void chord(float dt);

    std::array<float, 12> raw_{};    // this frame's chroma, unnormalised
    std::array<float, 12> fast_{};   // ~0.15 s
    std::array<float, 12> mid_{};    // ~0.6 s, the chord
    std::array<float, 12> slow_{};   // ~10 s, the key
    std::array<float, 12> homeProfile_{};  // the 24 key profiles blended by fit: what "at home" sounds like
    float fifthsX_ = 1.0f;           // the home key as a unit vector on the circle of fifths, eased
    float fifthsY_ = 0.0f;
    float tonicX_ = 1.0f;
    float tonicY_ = 0.0f;
    float heardSeconds_ = 0.0f;      // pitched sound heard so far: the key warms up over the first few seconds
    float sinceChange_ = 1e9f;
    float changeRate_ = 0.0f;
    float lastDistance_ = 0.0f;
    GeodeHarmony out_;
};

}  // namespace geode::sound
