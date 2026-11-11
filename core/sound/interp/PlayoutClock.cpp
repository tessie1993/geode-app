#include "sound/interp/PlayoutClock.hpp"

#include <algorithm>
#include <cmath>

namespace geode::sound {

void PlayoutClock::bufferHandled(int64_t presentationUs, int64_t ringFrame, int64_t frames) {
    frames = std::max<int64_t>(frames, 0);
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ > 0) {
        const Anchor& newest = anchor(count_ - 1);
        // The sink hands the same buffer again when the AudioTrack could not take it whole: one anchor.
        if (presentationUs == newest.presentationUs && ringFrame >= newest.ringFrame) {
            // Each try reports what it added to the ring: the buffer's samples run to the newest try's end.
            Anchor& kept = anchors_[static_cast<std::size_t>((first_ + count_ - 1) % kAnchors)];
            kept.frames = std::max(kept.frames, ringFrame + frames - kept.ringFrame);
            newestWritten_ = std::max(newestWritten_, ringFrame + frames);
            return;
        }
        // Without a flush, media time and the ring only move forward. If either went back, the old map
        // describes a stream that is gone: start again here rather than interpolate across the jump.
        if (presentationUs < newest.presentationUs || ringFrame < newest.ringFrame) {
            first_ = 0;
            count_ = 0;
            newestWritten_ = 0;
        }
    }
    if (count_ == kAnchors) {
        first_ = (first_ + 1) % kAnchors;
        --count_;
    }
    anchors_[static_cast<std::size_t>((first_ + count_) % kAnchors)] = Anchor{presentationUs, ringFrame, frames};
    ++count_;
    newestWritten_ = std::max(newestWritten_, ringFrame + frames);
}

void PlayoutClock::position(int64_t positionUs, int64_t nowNs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!positioned_) {
        // The first report of a new map: what played before it belongs to audio this map does not
        // know, so a positive offset holds here instead of running back across the seek.
        positioned_ = true;
        playStartNs_ = nowNs;
        playEndNs_ = nowNs;
    }
    positionUs_ = positionUs;
    positionNs_ = nowNs;
}

void PlayoutClock::playing(bool on, int64_t nowNs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (on == playing_) return;
    if (on) {
        // The held position is where the speaker resumes; the time spent paused is not play.
        playStartNs_ = nowNs;
        if (positioned_) positionNs_ = nowNs;
    } else {
        // Rebase the last report to the pause: count what played since it (up to the stall cap).
        if (positioned_) positionUs_ = std::llround(positionAt(nowNs, nowNs));
        positionNs_ = nowNs;
        playEndNs_ = nowNs;
        if (!positioned_) playStartNs_ = nowNs;
    }
    playing_ = on;
}

void PlayoutClock::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    first_ = 0;
    count_ = 0;
    newestWritten_ = 0;
    positioned_ = false;
}

void PlayoutClock::speed(float speed) {
    if (!(speed > 0.0f) || !std::isfinite(speed)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (speed_ == static_cast<double>(speed)) return;
    // Behind the last report the position ran at the old speed; rather than keep a history of speeds,
    // a positive offset holds at the change for its length and then follows the new speed.
    if (positioned_) playStartNs_ = playing_ ? std::max(playStartNs_, positionNs_) : playEndNs_;
    speed_ = speed;
}

void PlayoutClock::setOffsetMs(int ms) {
    const int clamped = std::clamp(ms, -kMaxOffsetMs, kMaxOffsetMs);
    std::lock_guard<std::mutex> lock(mutex_);
    offsetNs_ = static_cast<int64_t>(clamped) * 1'000'000;
}

double PlayoutClock::positionAt(int64_t tNs, int64_t nowNs) const {
    if (!playing_) {
        // Paused: the speaker played along the last span and has stood at positionUs_ since its end.
        const int64_t t = std::clamp(tNs, std::min(playStartNs_, playEndNs_), playEndNs_);
        return static_cast<double>(positionUs_) + static_cast<double>(t - playEndNs_) / 1000.0 * speed_;
    }
    // A report this stale means the sink stopped reporting (a stall, a starved decoder): stop moving
    // at the cap instead of running on past what it actually played.
    const int64_t staleNs = std::max<int64_t>(0, nowNs - (positionNs_ + kMaxExtrapolateNs));
    const int64_t t = std::max(tNs - staleNs, playStartNs_);
    return static_cast<double>(positionUs_) + static_cast<double>(t - positionNs_) / 1000.0 * speed_;
}

int64_t PlayoutClock::ringAt(double us) const {
    const Anchor& oldest = anchor(0);
    if (us <= static_cast<double>(oldest.presentationUs)) return oldest.ringFrame;
    // The newest anchor at or before us.
    int lo = 0;
    int hi = count_ - 1;
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (static_cast<double>(anchor(mid).presentationUs) <= us) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    const Anchor& a = anchor(lo);
    const double into = us - static_cast<double>(a.presentationUs);
    if (lo + 1 < count_) {
        const Anchor& b = anchor(lo + 1);
        const auto dr = static_cast<double>(b.ringFrame - a.ringFrame);
        const auto dp = static_cast<double>(b.presentationUs - a.presentationUs);
        return a.ringFrame + static_cast<int64_t>(std::floor(into * dr / dp));
    }
    // Inside the newest buffer: no anchor after it yet, so run on at the last pair's slope (the sink
    // only gets here when its buffer has drained, at the end of a stream), and never past the samples
    // that buffer wrote. With a single anchor there is no slope to trust: hold at its start.
    if (count_ < 2) return a.ringFrame;
    const Anchor& prev = anchor(lo - 1);
    const auto dr = static_cast<double>(a.ringFrame - prev.ringFrame);
    const auto dp = static_cast<double>(a.presentationUs - prev.presentationUs);
    const auto ran = static_cast<int64_t>(std::floor(into * dr / dp));
    return a.ringFrame + std::min(ran, a.frames);
}

int64_t PlayoutClock::speakerFrame(int64_t nowNs) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ == 0) return kUnknown;
    if (!positioned_) return anchor(0).ringFrame;
    const int64_t frame = ringAt(positionAt(nowNs - offsetNs_, nowNs));
    return std::clamp(frame, anchor(0).ringFrame, newestWritten_);
}

int PlayoutClock::lagMs(int64_t nowNs) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ == 0) return 0;
    const Anchor& newest = anchor(count_ - 1);
    // Media time at the end of the newest buffer: its start, plus its frames at the last pair's slope.
    auto endUs = static_cast<double>(newest.presentationUs);
    if (count_ >= 2) {
        const Anchor& prev = anchor(count_ - 2);
        const auto dr = static_cast<double>(newest.ringFrame - prev.ringFrame);
        const auto dp = static_cast<double>(newest.presentationUs - prev.presentationUs);
        if (dr > 0.0) endUs += static_cast<double>(newest.frames) * dp / dr;
    }
    const double heardUs = positioned_ ? positionAt(nowNs, nowNs) : static_cast<double>(anchor(0).presentationUs);
    const double ms = (endUs - heardUs) / 1000.0 / speed_;
    return static_cast<int>(std::clamp(ms, 0.0, static_cast<double>(kMaxLagMs)));
}

}  // namespace geode::sound
