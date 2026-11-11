#pragma once
#include <cmath>

namespace geode::viz {

// Speed-integrated clocks: the one way a style's clock follows the Speed dial.
//
// Speed is a rate, so it is integrated, never multiplied onto an accumulated
// clock. `clock * speed` is elapsed time times the live dial: every Speed
// change (the slider, an LFO, a macro knob, a reaction route) moves it by
// clock * delta-speed in one frame, which an hour into a set is a different
// picture. ShaderScene already does this for uTime (shaderTime_ += speed * dt);
// these do it for the other clocks a style may want Speed on: the motion
// field's flow phase (uFlowTime) and the sound engine's band clocks
// (uSoundTimeSped). tools/check_shaders.py rejects uTime * uSpeed outright, and
// uFlowPhase * uSpeed for the same reason.
namespace speedclock {

// One frame of a clock whose rate is known: rate (per second) times speed,
// over dt, wrapped. At speed 1 this is bit-for-bit the unscaled clock
// `fmod(clock + dt * rate, wrap)`, because 1 * rate == rate exactly.
inline float advance(float clock, float speed, float rate, float dt, float wrap) {
    return std::fmod(clock + dt * (speed * rate), wrap);
}

// Follows a clock that something else integrates (the sound engine's band
// clocks, whose rate this side does not know) by its per-frame increment,
// times speed. It starts on the source's own value, so at speed 1 it reads the
// same as the source. A step backwards is the source wrapping or restarting
// (a new track); either way the true increment is at most one frame, so the
// follower takes the source's new value as the increment and never jumps.
struct Follower {
    float value = 0.0f;
    float last = 0.0f;
    bool primed = false;

    float step(float source, float speed, float wrap) {
        if (!primed) {
            primed = true;
            value = source;
        } else {
            const float delta = source >= last ? source - last : source;
            value = std::fmod(value + delta * speed, wrap);
        }
        last = source;
        return value;
    }
};

}  // namespace speedclock
}  // namespace geode::viz
