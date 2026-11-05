#include "api/geode_api.h"
#include "viz/frame/FixedRateClock.hpp"

using geode::viz::frame::CastFormat;
using geode::viz::frame::FixedRateClock;

namespace {

FixedRateClock load(const int64_t* state) {
    FixedRateClock c;
    c.startNs = state[0];
    c.next = state[1];
    c.fps = state[2] > 0 ? static_cast<int>(state[2]) : CastFormat::kFps;
    return c;
}

void store(const FixedRateClock& c, int64_t* state) {
    state[0] = c.startNs;
    state[1] = c.next;
    state[2] = c.fps;
}

}  // namespace

extern "C" {

void geode_frame_clock_start(int64_t* state, int fps, int64_t now_ns) {
    if (!state) return;
    store(FixedRateClock::started(fps, now_ns), state);
}

int64_t geode_frame_clock_wait_ns(const int64_t* state, int64_t now_ns) {
    return state ? load(state).waitNs(now_ns) : 0;
}

int64_t geode_frame_clock_take(int64_t* state, int64_t now_ns) {
    if (!state) return 0;
    FixedRateClock c = load(state);
    const int64_t pts = c.take(now_ns);
    store(c, state);
    return pts;
}

int64_t geode_frame_clock_audio_until_us(const int64_t* state) {
    return state ? load(state).audioUntilUs() : 0;
}

void geode_cast_format(int* out) {
    if (!out) return;
    out[0] = CastFormat::kWidth;
    out[1] = CastFormat::kHeight;
    out[2] = CastFormat::kFps;
}

}  // extern "C"
