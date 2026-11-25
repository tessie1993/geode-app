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
// these do it for the other clocks a style may want Speed on: the flow clocks
// (uFlowPhase, uFlowTime) and the sound engine's band clocks
// (uSoundTimeSped). tools/check_shaders.py rejects uTime * uSpeed outright, and
// uFlowPhase * uSpeed for the same reason.
namespace speedclock {

// One frame of a clock whose rate is known: rate (per second) times speed,
// over dt, wrapped. At speed 1 this is bit-for-bit the unscaled clock
// `fmod(clock + dt * rate, wrap)`, because 1 * rate == rate exactly.
//
// The clock is a double and is wrapped as one; only the consumer casts it to float. A float clock past
// 4096 s steps in 0.000488 s, so a slow rate (0.1 at 60 fps adds 0.0002 s a frame) stalls or lurches.
inline double advance(double clock, float speed, float rate, float dt, float wrap) {
    return std::fmod(clock + dt * (speed * rate), static_cast<double>(wrap));
}

// The flow clocks (uFlowPhase, uFlowTime): a steady travel phase that runs at one fixed base rate, in silence
// and in music alike, never faster or slower for how loud the music is. The rate is a frequency in Hz, not a
// tempo. phase runs at the base rate; time runs at the base rate times Speed, integrated, so it equals phase at
// Speed 1 and moving Speed never jumps it. Each scene owns one and resets it with the scene.
struct FlowClock {
    static constexpr float kBaseHz = 0.08f;
    static constexpr float kWrapSeconds = 7100.0f;
    double phase = 0.0;
    double time = 0.0;

    void reset() { phase = time = 0.0; }
    // `dt` is the scene clock's step (Clock control): it holds at 0 on a freeze and is negative on a reverse, and
    // both clocks follow it, wrapping into 0..kWrapSeconds either way.
    void step(float speed, float dt) {
        const float d = std::isfinite(dt) ? dt : 0.0f;
        phase = std::fmod(phase + d * kBaseHz, static_cast<double>(kWrapSeconds));
        time = advance(time, speed, kBaseHz, d, kWrapSeconds);
        if (phase < 0.0) phase += static_cast<double>(kWrapSeconds);
        if (time < 0.0) time += static_cast<double>(kWrapSeconds);
    }
};

// Follows a clock that something else integrates (the sound engine's band
// clocks, whose rate this side does not know) by its per-frame increment,
// times speed. It starts on the source's own value, so at speed 1 it reads the
// same as the source. A step backwards is the source wrapping or restarting
// (a new track); either way the true increment is at most one frame, so the
// follower takes the source's new value as the increment and never jumps.
struct Follower {
    double value = 0.0;
    float last = 0.0f;
    bool primed = false;

    double step(float source, float speed, float wrap) {
        if (!primed) {
            primed = true;
            value = source;
        } else {
            const float delta = source >= last ? source - last : source;
            value = std::fmod(value + static_cast<double>(delta * speed), static_cast<double>(wrap));
        }
        last = source;
        return value;
    }
};

}  // namespace speedclock
}  // namespace geode::viz
