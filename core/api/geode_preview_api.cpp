#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "studio/PreviewClock.hpp"

using geode::studio::PreviewClock;

extern "C" {

geode_preview* geode_preview_create(void) { return reinterpret_cast<geode_preview*>(new PreviewClock()); }

void geode_preview_destroy(geode_preview* p) { delete reinterpret_cast<PreviewClock*>(p); }

void geode_preview_set_spans(geode_preview* p, const long long* starts_ms, const long long* durations_ms, int count) {
    std::vector<PreviewClock::Span> spans;
    if (starts_ms && durations_ms && count > 0) {
        spans.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) spans.push_back({starts_ms[i], durations_ms[i]});
    }
    reinterpret_cast<PreviewClock*>(p)->setSpans(std::move(spans));
}

void geode_preview_set_rate(geode_preview* p, int fps, long long length_ms) {
    reinterpret_cast<PreviewClock*>(p)->setRate(fps, length_ms);
}

void geode_preview_play(geode_preview* p, long long programme_ms, long long now_ns) {
    reinterpret_cast<PreviewClock*>(p)->play(programme_ms, now_ns);
}

void geode_preview_pause(geode_preview* p, long long now_ns) { reinterpret_cast<PreviewClock*>(p)->pause(now_ns); }

void geode_preview_seek(geode_preview* p, long long programme_ms, long long now_ns) {
    reinterpret_cast<PreviewClock*>(p)->seek(programme_ms, now_ns);
}

void geode_preview_audio(geode_preview* p, int span, long long span_pos_ms, long long now_ns) {
    reinterpret_cast<PreviewClock*>(p)->audio(span, span_pos_ms, now_ns);
}

long long geode_preview_now(const geode_preview* p, long long now_ns) {
    return reinterpret_cast<const PreviewClock*>(p)->now(now_ns);
}

int geode_preview_ended(const geode_preview* p, long long now_ns) {
    return reinterpret_cast<const PreviewClock*>(p)->ended(now_ns) ? 1 : 0;
}

int geode_preview_locate(const geode_preview* p, long long programme_ms, long long* offset_ms) {
    int64_t offset = 0;
    const int span = reinterpret_cast<const PreviewClock*>(p)->locate(programme_ms, &offset);
    if (offset_ms) *offset_ms = offset;
    return span;
}

long long geode_preview_frame_of(long long programme_ms, int fps) { return PreviewClock::frameOf(programme_ms, fps); }

}  // extern "C"
