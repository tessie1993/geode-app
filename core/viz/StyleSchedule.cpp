#include "viz/StyleSchedule.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace geode::viz {

namespace {
constexpr int kBeatCountWrap = 4096;
}

void StyleSchedule::set(std::vector<StyleCue> cues, std::vector<std::string> beatCutIds, int everyBeats,
                        const std::vector<std::string>& lockedIds) {
    const auto locked = [&lockedIds](const std::string& id) {
        return std::find(lockedIds.begin(), lockedIds.end(), id) != lockedIds.end();
    };
    std::erase_if(cues, [&locked](const StyleCue& cue) { return locked(cue.sceneId); });
    std::erase_if(beatCutIds, locked);
    std::stable_sort(cues.begin(), cues.end(), [](const StyleCue& a, const StyleCue& b) { return a.atMs < b.atMs; });
    cues_ = std::move(cues);
    cut_ = std::move(beatCutIds);
    everyBeats_ = std::max(1, everyBeats);
    nextCue_ = 0;
    cutIndex_ = 0;
    beatsSinceCut_ = 0;
    started_ = false;
    lastBeatCount_ = 0.0f;
    clockMs_ = 0.0;
    current_ = StyleCue{};
}

void StyleSchedule::clear() { set({}, {}, everyBeats_); }

int StyleSchedule::beatsBetween(float before, float after) {
    const int a = static_cast<int>(std::floor(before));
    const int b = static_cast<int>(std::floor(after));
    return ((b - a) % kBeatCountWrap + kBeatCountWrap) % kBeatCountWrap;
}

const StyleCue* StyleSchedule::change(const StyleCue& cue) {
    if (cue.sceneId.empty()) return nullptr;
    if (cue.sceneId == current_.sceneId && cue.milkPath == current_.milkPath) return nullptr;
    current_ = cue;
    pending_ = cue;
    return &pending_;
}

const StyleCue* StyleSchedule::step(double timeMs, float beatCount) {
    if (empty()) return nullptr;
    const int beats = started_ ? beatsBetween(lastBeatCount_, beatCount) : 0;
    started_ = true;
    lastBeatCount_ = beatCount;

    // Timed cues: every cue passed since the last frame, keeping only the last, so a frame that
    // jumps over several switches lands on the look that was on screen at that moment.
    const StyleCue* passed = nullptr;
    while (nextCue_ < cues_.size() && cues_[nextCue_].atMs <= timeMs) {
        passed = &cues_[nextCue_];
        nextCue_++;
    }
    if (passed) {
        // A take's switch restarts the beat cut's count, so the cut keeps its spacing from there.
        beatsSinceCut_ = 0;
        return change(*passed);
    }

    if (cut_.empty() || beats == 0) return nullptr;
    beatsSinceCut_ += beats;
    if (beatsSinceCut_ < everyBeats_) return nullptr;
    beatsSinceCut_ %= everyBeats_;
    cutIndex_ = (cutIndex_ + 1) % cut_.size();
    StyleCue cue;
    cue.atMs = timeMs;
    cue.sceneId = cut_[cutIndex_];
    return change(cue);
}

const StyleCue* StyleSchedule::advance(float frameStepS, float motionRate, float beatCount) {
    if (empty()) return nullptr;
    if (!(motionRate > 0.0f) || !std::isfinite(motionRate)) {
        // Still: the picture holds, so does the schedule. Only the beat count is followed, so the beats
        // ticked through the silence are never counted when the music comes back.
        if (started_) lastBeatCount_ = beatCount;
        return nullptr;
    }
    const StyleCue* cue = step(clockMs_, beatCount);
    const double stepMs = std::isfinite(frameStepS) && frameStepS > 0.0f ? static_cast<double>(frameStepS) * 1000.0 : 0.0;
    clockMs_ += stepMs * static_cast<double>(std::min(motionRate, 1.0f));
    return cue;
}

} // namespace geode::viz
