#include "api/geode_api.h"
#include "sound/Silence.hpp"
#include "sound/Tuning.hpp"

#include <algorithm>
#include "sound/SoundSource.hpp"


namespace {
namespace src = geode::sound::source;
static_assert(src::kStateFields == GEODE_SOUND_SOURCE_STATE_LONGS, "state size crosses the ABI");
static_assert(src::kUseCapture == GEODE_SOUND_SOURCE_USE_CAPTURE && src::kStartCapture == GEODE_SOUND_SOURCE_START_CAPTURE &&
                  src::kStopCapture == GEODE_SOUND_SOURCE_STOP_CAPTURE,
              "answer bits cross the ABI");

// The caller's long array in and out of the struct, one slot per field in declaration order.
src::State load(const long long* state) {
    src::State s;
    s.mode = state[0];
    s.ringSeen = state[1];
    s.ringId = state[2];
    s.ringWritten = state[3];
    s.fedAtMs = state[4];
    s.fedSinceMs = state[5];
    s.retryAtMs = state[6];
    s.permitted = state[7];
    return s;
}
void save(const src::State& s, long long* state) {
    state[0] = s.mode;
    state[1] = s.ringSeen;
    state[2] = s.ringId;
    state[3] = s.ringWritten;
    state[4] = s.fedAtMs;
    state[5] = s.fedSinceMs;
    state[6] = s.retryAtMs;
    state[7] = s.permitted;
}
}  // namespace

extern "C" {

long long geode_silence_heard_at(const float* const* channels, int channel_count, int frames, long long now_ms,
                                 long long heard_at_ms) {
    return geode::sound::silence::heardAt(channels, channel_count, frames, now_ms, heard_at_ms);
}

int geode_silence_resting(long long now_ms, long long heard_at_ms) {
    return geode::sound::silence::resting(now_ms, heard_at_ms) ? 1 : 0;
}

int geode_sound_source_step(long long* state, long long now_ms, int ring_present, long long ring_id,
                            long long ring_written, int capture_permitted, int capture_running) {
    if (!state) return 0;
    src::State s = load(state);
    const int out = src::step(s, now_ms, ring_present != 0, ring_id, ring_written, capture_permitted != 0,
                              capture_running != 0);
    save(s, state);
    return out;
}

void geode_sound_source_start_failed(long long* state, long long now_ms) {
    if (!state) return;
    src::State s = load(state);
    src::startFailed(s, now_ms);
    save(s, state);
}

int geode_sound_tuning_slots(void) { return geode::sound::tuning::kSlots; }

void geode_sound_tuning_defaults(float* out, int count) {
    if (!out) return;
    const auto& slots = geode::sound::tuning::slots();
    const int n = std::min(count, geode::sound::tuning::kSlots);
    for (int i = 0; i < n; ++i) out[i] = slots[static_cast<size_t>(i)].def;
}

void geode_sound_tuning_ranges(float* min_out, float* max_out, int count) {
    const auto& slots = geode::sound::tuning::slots();
    const int n = std::min(count, geode::sound::tuning::kSlots);
    for (int i = 0; i < n; ++i) {
        if (min_out) min_out[i] = slots[static_cast<size_t>(i)].lo;
        if (max_out) max_out[i] = slots[static_cast<size_t>(i)].hi;
    }
}

void geode_sound_tuning_clamp(float* values, int count) { geode::sound::tuning::clamp(values, count); }

}  // extern "C"
