#include "api/geode_api.h"
#include "sound/PlayerSignals.hpp"
#include "sound/Silence.hpp"
#include "sound/Tuning.hpp"

#include <algorithm>

extern "C" {

long long geode_capture_heard_at(float peak, long long now_ms, long long heard_at_ms) {
    return geode::sound::silence::captureHeardAt(peak, now_ms, heard_at_ms);
}

int geode_capture_quiet(long long now_ms, long long heard_at_ms) {
    return geode::sound::silence::captureQuiet(now_ms, heard_at_ms) ? 1 : 0;
}

long long geode_sound_track_boundary(long long seed) {
    return static_cast<long long>(geode::sound::player::trackBoundary(static_cast<uint64_t>(seed)));
}

void geode_sound_set_player_source(int on) { geode::sound::player::setPlayerIsSource(on != 0); }

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

int geode_sound_tuning_resolve(const float* saved, int count, float* out) {
    return geode::sound::tuning::resolve(saved, count, out) ? 1 : 0;
}

}  // extern "C"
