#include <mutex>
#include <string>

#include "api/geode_api.h"
#include "viz/ParamHistory.hpp"

namespace history = geode::viz::param_history;

static_assert(history::kCanUndo == GEODE_PARAM_HISTORY_UNDO);
static_assert(history::kCanRedo == GEODE_PARAM_HISTORY_REDO);

namespace {

history::Look lookOf(const float* frame, int count, const char* palette_id, const char* style_id) {
    history::Look look;
    if (frame && count > 0) look.frame.assign(frame, frame + count);
    look.paletteId = palette_id ? palette_id : "";
    look.styleId = style_id ? style_id : "";
    return look;
}

}  // namespace

extern "C" {

int geode_param_history_record(const float* before, int count, const char* before_palette, const char* style_id, long long now_ms,
                               int discrete) {
    auto& s = history::detail::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    s.history.record(lookOf(before, count, before_palette, style_id), now_ms, discrete != 0);
    return s.history.state();
}

const char* geode_param_history_step(int redo, const float* current, int count, const char* current_palette, const char* style_id,
                                     float* out) {
    thread_local std::string palette;
    auto& s = history::detail::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    auto back = s.history.step(redo != 0, lookOf(current, count, current_palette, style_id));
    if (!back) return nullptr;
    if (out) {
        for (int i = 0; i < count; ++i) out[i] = i < static_cast<int>(back->frame.size()) ? back->frame[static_cast<size_t>(i)] : 0.0f;
    }
    palette = back->paletteId;
    return palette.c_str();
}

int geode_param_history_state(void) {
    auto& s = history::detail::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    return s.history.state();
}

}  // extern "C"
