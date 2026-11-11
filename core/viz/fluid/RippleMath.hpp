#pragma once
#include <array>

#include "api/geode_api.h"

// Water ripples as a bounded list of drops. Each drop is one ring train that runs out from its centre at a
// steady speed, loses height with distance and with age, and is exactly flat at the end of its lifetime.
// The surface is the sum of the rings (so they interfere) plus each ring's one soft echo off the screen
// edges. It is a function of the list alone: nothing is fed back, so nothing can sustain itself.
namespace geode::viz::fluid::ripple {

constexpr int kMaxDrops = 12;
constexpr int kEdges = 4;
constexpr int kMaxWavelets = kMaxDrops * (1 + kEdges);
constexpr float kMaxAmplitude = 1.0f;
constexpr float kMinLifetime = 1.2f;
constexpr float kMaxLifetime = 4.0f;
constexpr float kReflection = 0.3f;
constexpr float kTouchRadius = 0.11f;

struct Drop {
    float x = 0.0f;
    float y = 0.0f;
    float wavelength = 0.1f;
    float amplitude = 0.0f;  // signed crest height at birth
    float age = 0.0f;
    float lifetime = kMaxLifetime;
    float front = 0.0f;  // radius of the leading crest
};

// One ring as the surface pass draws it: a drop, or its echo off one edge.
struct Wavelet {
    float x;
    float y;
    float front;
    float amplitude;
    float invWavelength;
};

// Customize's Damping (0.9..0.999) as a ring's lifetime in seconds, and Wave speed (0.2..2) in half-heights a second.
float lifetimeFor(float damping);
float speedFor(float waveSpeed);
float wavelengthFor(float radius);

// The crest height left: 1 at birth, falling all the way, 0 from the lifetime on.
float envelope(float age, float lifetime);
// The loss with distance run.
float spread(float front, float wavelength);
float peak(const Drop& d);

// The ring's cross-section, s in wavelengths ahead of the leading crest: a crest with a short lip in
// front and a fading train behind. |profile| <= 1.
float profile(float s);
float profileSlope(float s);
// The height and its rate of change one wavelet gives a point; ripple_surface_frag is this, line for line.
float height(const Wavelet& w, float x, float y);
float heightRate(const Wavelet& w, float x, float y, float speed);

// The ink layer's share kept over one frame.
float inkDissipation(float dissipation, float dt);

class Surface {
public:
    void configure(float speed, float lifetime);
    // False when the drop was not taken: too weak, too close to one that just fell, or the list is
    // full of rings still too tall to drop unseen.
    bool add(float x, float y, float radius, float amplitude);
    void step(float dt);
    void clear() { count_ = 0; }

    int size() const { return count_; }
    bool flat() const { return count_ == 0; }
    const Drop& drop(int i) const { return drops_[static_cast<size_t>(i)]; }
    float speed() const { return speed_; }
    // The sum of the squared crest heights: falls on every step that adds no drop.
    float energy() const;
    // Fills out[kMaxWavelets]; an echo joins once its ring has reached that edge.
    int wavelets(float aspect, float gain, Wavelet* out) const;

private:
    std::array<Drop, kMaxDrops> drops_{};
    int count_ = 0;
    float speed_ = 0.6f;
    float lifetime_ = 3.6f;
};

// Fingers on the surface: a ring where one lands, then one each stretch it travels, taller the faster it moves.
class Wake {
public:
    static constexpr int kMaxFingers = 5;
    using Emit = void (*)(void* ctx, float x, float y, float amplitude);

    void reset();
    // xy: the fingers down now in surface units (x in -aspect..aspect, y in -1..1), index-stable while held.
    void step(const float* xy, int fingers, float dt, void* ctx, Emit emit);

private:
    std::array<float, kMaxFingers> x_{};
    std::array<float, kMaxFingers> y_{};
    std::array<float, kMaxFingers> run_{};
    int down_ = 0;
};

// The overlay's music: a drop on each onset the sound engine measured, taller with the hit and the bass,
// none in silence. Places walk a golden-angle spiral so neighbours in time are apart in space.
class OnsetDrops {
public:
    struct Out {
        bool drop = false;
        float x = 0.0f;
        float y = 0.0f;
        float radius = 0.055f;
        float amplitude = 0.0f;
    };
    void reset() { index_ = 0; }
    Out step(const GeodeSoundFrame& frame, float aspect);

private:
    int index_ = 0;
};

}  // namespace geode::viz::fluid::ripple
