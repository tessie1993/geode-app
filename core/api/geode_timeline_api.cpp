#include <algorithm>
#include <cstring>
#include <string>

#include "api/geode_api.h"
#include "signal/timeline/TakeTimeline.hpp"

using geode::signal::TakeTimeline;

struct geode_take_timeline {
    TakeTimeline timeline;
};

namespace {

size_t copyOut(const std::string& s, char* out, size_t capacity) {
    if (out && capacity > 0) {
        const size_t n = std::min(s.size(), capacity - 1);
        std::memcpy(out, s.data(), n);
        out[n] = '\0';
    }
    return s.size();
}

}  // namespace

extern "C" {

geode_take_timeline* geode_take_timeline_create(uint64_t seed, int song_time) {
    return new geode_take_timeline{TakeTimeline(seed, song_time != 0)};
}

geode_take_timeline* geode_take_timeline_decode(const char* text) {
    if (!text) return nullptr;
    auto t = TakeTimeline::decode(text);
    return t ? new geode_take_timeline{std::move(*t)} : nullptr;
}

void geode_take_timeline_destroy(geode_take_timeline* t) { delete t; }

void geode_take_timeline_note(geode_take_timeline* t, int64_t at_ms, const char* scene_id, const char* milk_path,
                              const float* params, int count) {
    if (t) t->timeline.note(at_ms, scene_id ? scene_id : "", milk_path ? milk_path : "", params, count);
}

int geode_take_timeline_full(const geode_take_timeline* t) { return t && t->timeline.full() ? 1 : 0; }

size_t geode_take_timeline_encode(const geode_take_timeline* t, char* out, size_t capacity) {
    return t ? copyOut(t->timeline.encode(), out, capacity) : 0;
}

uint64_t geode_take_timeline_seed(const geode_take_timeline* t) { return t ? t->timeline.seed() : 0; }

int geode_take_timeline_renders_again(const geode_take_timeline* t) { return t && t->timeline.rendersAgain() ? 1 : 0; }

int64_t geode_take_timeline_end_ms(const geode_take_timeline* t) { return t ? t->timeline.endMs() : 0; }

int geode_take_timeline_state_at(const geode_take_timeline* t, int64_t ms, float* params, int capacity) {
    if (!t) return -1;
    const auto* c = t->timeline.stateAt(ms);
    if (!c) return -1;
    if (params && capacity > 0) {
        const size_t n = std::min(c->params.size(), static_cast<size_t>(capacity));
        std::copy(c->params.begin(), c->params.begin() + static_cast<std::ptrdiff_t>(n), params);
    }
    return static_cast<int>(c - t->timeline.choices().data());
}

int64_t geode_take_timeline_choice_at_ms(const geode_take_timeline* t, int index) {
    if (!t || index < 0 || static_cast<size_t>(index) >= t->timeline.choices().size()) return 0;
    return t->timeline.choices()[static_cast<size_t>(index)].atMs;
}

size_t geode_take_timeline_choice_style(const geode_take_timeline* t, int index, char* out, size_t capacity) {
    if (!t || index < 0 || static_cast<size_t>(index) >= t->timeline.choices().size()) return copyOut("", out, capacity);
    const auto& c = t->timeline.choices()[static_cast<size_t>(index)];
    return copyOut(c.sceneId + "\t" + c.milkPath, out, capacity);
}

int geode_take_timeline_style_changes(const geode_take_timeline* t, int* indices, int capacity) {
    if (!t) return 0;
    const auto changes = t->timeline.styleChanges();
    const auto* base = t->timeline.choices().data();
    for (size_t i = 0; indices && i < changes.size() && i < static_cast<size_t>(std::max(capacity, 0)); ++i) {
        indices[i] = static_cast<int>(changes[i] - base);
    }
    return static_cast<int>(changes.size());
}

}
