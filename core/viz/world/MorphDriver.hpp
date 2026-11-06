#pragma once
#include <array>
#include <cstdint>

#include "sound/SoundFrame.h"
#include "viz/world/WorldGenome.hpp"

// Morph World's driver: the world is always on its way from the genome it was in toward a target
// genome, and the music decides where it goes, when it turns, and how fast it gets there.
//
// WHERE. A target is random(seed, constraints): the per-play seed (the auto-director's, so a replay
// is new and an export with the play's seed is the same film) drawn forward once per retarget, and
// the music's energy, mood and density picking which seed world it grows from; the Morph control
// (sampled at the retarget, never live: MotionField and the reaction DNA move Morph on every beat)
// says how far it mutates.
//
// WHEN. Only at musical moments. A drop always turns the world. A new section turns it with a
// chance that grows with the energy, so a calm song can stay in one world for all of it, as #302
// asks: no timer ever forces a change.
//
// HOW FAST. A verse glides over many seconds; a drop rushes in about a second and a half, still
// eased at both ends. The k2 FLOW knob speeds or slows every glide.
//
// THE VIEW. Each world rolls the view at its own rate (Roll) and looks through its own lens (Lens);
// on top, a slow sway that the energy deepens, a lean into the camera's sideways motion, and a
// small punch-in of the lens on the kick. Only the ray turns (lib_world's worldCamera), never the
// eye, so the corridor still holds.
//
// THE GAZE. The view also looks around: up at towers, down into canyons, to the sides. GazeX (yaw)
// and GazeY (pitch) stay within the live world's GazeYaw and GazePitch, so tunnels barely look away
// and halls look about freely. Each section draws a new look from the gaze's own rng stream (so the
// world sequence is the same with or without it), a build-up tilts the view up as it rises, and a
// drop brings it back toward centre in about a second, holding there while the drop lands. On top,
// a gentle drift: the chord turns the look sideways, the bass weighs it down a touch. Every look is
// eased and then rate-limited (kGazeRatePerSecond), so the gaze never snaps.
//
// ON TOP. Small live wobble from the sound and the controls: the bass breathes the inversion and
// the IFS scale, the chord turns the palette, the kick lifts the emission, Warp adds twist,
// Turbulence the IFS strength, the knobs their shares. All summed once and clamped once into the
// field bounds, then uploaded with the runtime values (uniforms()).
//
// BRIGHTNESS. A retarget can land in a darker or brighter world; the Gain envelope follows the
// inverse of the genome's rough brightness, rate-limited, so the frame's level glides rather than
// steps. The composite's safety::apply still clamps every frame after this.
//
// Pure: a function of (seed, the input sequence, dt). No clock, no allocation.
namespace geode::viz::world {

struct DriverInput {
    // The music, each 0..1 unless noted.
    float energy = 0.0f;     // loudness, presence and the section's intensity
    float mood = 0.5f;       // dark .. bright (the scale's brightness, the key's major-ness)
    float density = 0.5f;    // how busy: partials, note runs
    float bass = 0.0f;       // bassAtt, 0..~2
    float kick = 0.0f;       // the kick envelope
    float chordHue = 0.0f;   // 0..1 around the circle of fifths
    float buildUp = 0.0f;
    float presence = 0.0f;   // is there music at all
    bool section = false;    // the sectionChange trigger this frame
    bool drop = false;       // the drop trigger this frame
    // The user's controls.
    float morph = 0.0f;      // how far targets mutate
    float warp = 0.0f;       // twist
    float turbulence = 0.0f; // IFS strength, 0..1.5
    float detail = 1.0f;     // the IFS step cap (Detail, 0.25..1.5)
    float intensity = 1.0f;  // emission
    std::array<float, 6> knobs{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

    // The music fields from a sound frame; the controls are left for the caller.
    static DriverInput fromSound(const GeodeSoundFrame& f);
};

class MorphDriver {
public:
    static constexpr float kVerseGlideSlow = 16.0f;  // seconds, a calm section change
    static constexpr float kVerseGlideFast = 6.0f;   // a loud one
    static constexpr float kDropGlide = 1.6f;
    // The most the Gain envelope moves per second: well under any flash rule.
    static constexpr float kGainRatePerSecond = 0.35f;
    // The most the gaze turns per second, radians: a full GazeYaw swing (0.6) back in about a second.
    static constexpr float kGazeRatePerSecond = 0.65f;
    // After a drop the gaze holds centre this long before it looks away again.
    static constexpr float kDropGazeHold = 1.2f;

    // A new play (or a new scene): the rng restarts from the seed, the world from one of the seeds.
    void reset(uint64_t seed);
    uint64_t seedValue() const { return seed_; }

    void step(const DriverInput& in, float dt);
    // The camera this frame (CameraPose::pos): integrates the z phase and the wake. Call after step.
    void travel(const std::array<float, 3>& eye, float dt);

    // genome + runtime, kUniformCount floats, for glUniform4fv(uWorld, kVec4, ...).
    const std::array<float, kUniformCount>& uniforms() const { return uniforms_; }
    // What world() should see, matching uniforms(): the live genome and the camera's way.
    const Genome& live() const { return live_; }
    Way way() const;

    // Whether the scene bound the baked maps and shape volume (WorldAssets) for the shader.
    void setAssetsBound(bool bound) { texturesBound_ = bound; }

    const Genome& target() const { return to_; }
    int targetSeed() const { return toSeed_; }
    int retargets() const { return retargets_; }
    float progress() const { return progress_; }
    float glideSeconds() const { return glide_; }
    float gain() const { return gain_; }
    // The look now, radians (what uniforms()[GazeX] and [GazeY] carry).
    float gazeX() const { return gazeX_; }
    float gazeY() const { return gazeY_; }

private:
    void retarget(const DriverInput& in, bool drop);
    float draw();
    float drawGaze();
    void lookAround(float dt);
    void publish();

    uint64_t seed_ = 0;
    uint64_t rng_ = 0;
    bool started_ = false;

    Genome from_{};
    Genome to_{};
    Genome now_{};
    Genome live_{};
    int toSeed_ = 0;
    float progress_ = 1.0f;
    float glide_ = kVerseGlideSlow;
    int retargets_ = 0;

    bool lastSection_ = false;
    bool lastDrop_ = false;
    // Slow means of the music, so a retarget reads the song, not one frame.
    float energy_ = 0.0f;
    float mood_ = 0.5f;
    float density_ = 0.5f;
    // Smoothed controls and wobble sources.
    float morph_ = 0.0f;
    float warp_ = 0.0f;
    float turb_ = 0.0f;
    float bass_ = 0.0f;
    float kick_ = 0.0f;
    float hueX_ = 1.0f;  // the chord hue as a point on the circle, smoothed there (no wrap jumps)
    float hueY_ = 0.0f;
    float buildUp_ = 0.0f;
    float iterCap_ = static_cast<float>(kMaxIfsSteps);
    float gain_ = 1.0f;
    // The camera's feel: the roll integrated from the genome's Roll, the sway's phase.
    float roll_ = 0.0f;
    float swayPhase_ = 0.0f;
    // The gaze: its own rng stream, the section's drawn look (-1..1 of the world's range), the drop's
    // hold, the slow bass for the drift, and the look now in radians.
    uint64_t gazeRng_ = 0;
    float lookX_ = 0.0f;
    float lookY_ = 0.0f;
    float dropHold_ = 0.0f;
    float bassSlow_ = 0.0f;
    float gazeX_ = 0.0f;
    float gazeY_ = 0.0f;

    // Travel.
    float zCells_ = 0.0f;  // the phase along travel, in periods (two mirrored cells), kept in 0..1
    bool hasEye_ = false;
    std::array<float, 3> eye_{0.0f, 0.0f, 0.0f};
    std::array<float, 3> vel_{0.0f, 0.0f, 0.0f};

    bool texturesBound_ = false;
    std::array<float, kUniformCount> uniforms_{};
};

}  // namespace geode::viz::world
