#include "studio/PreviewClock.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace geode::studio {

void PreviewClock::setSpans(std::vector<Span> spans) {
    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.startMs < b.startMs; });
    spans_ = std::move(spans);
}

void PreviewClock::setRate(int fps, int64_t lengthMs) {
    fps_ = std::clamp(fps, 1, 240);
    lengthMs_ = std::max<int64_t>(0, lengthMs);
}

double PreviewClock::rawMs(int64_t nowNs) const {
    if (!playing_) return anchorMs_;
    return anchorMs_ + static_cast<double>(nowNs - anchorNs_) / 1.0e6;
}

void PreviewClock::anchor(double programmeMs, int64_t nowNs) {
    anchorMs_ = std::max(0.0, programmeMs);
    anchorNs_ = nowNs;
}

void PreviewClock::play(int64_t programmeMs, int64_t nowNs) {
    anchor(static_cast<double>(programmeMs), nowNs);
    playing_ = true;
}

void PreviewClock::pause(int64_t nowNs) {
    anchor(rawMs(nowNs), nowNs);
    playing_ = false;
}

void PreviewClock::seek(int64_t programmeMs, int64_t nowNs) { anchor(static_cast<double>(programmeMs), nowNs); }

void PreviewClock::audio(int span, int64_t spanPosMs, int64_t nowNs) {
    if (!playing_ || span < 0 || span >= static_cast<int>(spans_.size())) return;
    const double heard = static_cast<double>(spans_[static_cast<size_t>(span)].startMs + spanPosMs);
    const double ours = rawMs(nowNs);
    const double error = heard - ours;
    // A large error is a jump the player made (or a stall): follow it at once. A small one is
    // jitter between the two clocks: take part of it out, so the picture never visibly skips.
    anchor(std::abs(error) > static_cast<double>(kSnapMs) ? heard : ours + error * kSlew, nowNs);
}

int64_t PreviewClock::now(int64_t nowNs) const {
    auto ms = static_cast<int64_t>(rawMs(nowNs));
    if (lengthMs_ > 0) ms = std::min(ms, lengthMs_);
    return frameOf(ms, fps_);
}

bool PreviewClock::ended(int64_t nowNs) const { return lengthMs_ > 0 && rawMs(nowNs) >= static_cast<double>(lengthMs_); }

int PreviewClock::locate(int64_t programmeMs, int64_t* offsetMs) const {
    for (size_t i = 0; i < spans_.size(); ++i) {
        const Span& s = spans_[i];
        if (programmeMs >= s.startMs && programmeMs < s.startMs + s.durationMs) {
            if (offsetMs) *offsetMs = programmeMs - s.startMs;
            return static_cast<int>(i);
        }
    }
    if (offsetMs) *offsetMs = 0;
    return -1;
}

int64_t PreviewClock::frameOf(int64_t programmeMs, int fps) {
    if (programmeMs <= 0 || fps <= 0) return std::max<int64_t>(0, programmeMs);
    // Integer maths: the frame index is floor(ms * fps / 1000), and its start is index * 1000 / fps
    // rounded up, so frameOf(frameOf(t)) == frameOf(t) and every time in a frame maps to one value.
    const int64_t index = programmeMs * fps / 1000;
    return (index * 1000 + fps - 1) / fps;
}

}  // namespace geode::studio
