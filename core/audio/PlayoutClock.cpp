#include "audio/PlayoutClock.hpp"

#include <algorithm>
#include <cmath>

namespace geode::audio {

namespace {

// Media microseconds the sink has played since the report at `fromNs`, if it kept playing.
double elapsedUs(int64_t fromNs, int64_t nowNs, double speed) {
    const int64_t ns = std::clamp<int64_t>(nowNs - fromNs, 0, PlayoutClock::kMaxExtrapolateNs);
    return static_cast<double>(ns) / 1000.0 * speed;
}

}  // namespace

void PlayoutClock::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    epoch_ = -1;
    rateHz_ = 0;
    started_ = false;
    positioned_ = false;
    playing_ = false;
    speed_ = 1.0;
}

void PlayoutClock::beginEpoch(int epoch, int rateHz) {
    std::lock_guard<std::mutex> lock(mutex_);
    epoch_ = epoch;
    rateHz_ = rateHz;
    started_ = false;
    // A position read before the boundary may come from the old stream's timeline (a seek); one read
    // after it is due within a playback-thread tick.
    positioned_ = false;
}

void PlayoutClock::bufferHandled(int64_t mediaUs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (epoch_ < 0 || started_) return;
    startUs_ = mediaUs;
    started_ = true;
}

void PlayoutClock::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    positioned_ = false;
}

void PlayoutClock::position(int64_t mediaUs, int64_t nowNs) {
    std::lock_guard<std::mutex> lock(mutex_);
    positionUs_ = mediaUs;
    positionNs_ = nowNs;
    positioned_ = true;
}

void PlayoutClock::playing(bool on, int64_t nowNs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (on == playing_) return;
    // Rebase the last report to now: on pause, count what played since it; on resume, the time spent
    // paused must not be extrapolated as play.
    if (positioned_ && playing_) positionUs_ += static_cast<int64_t>(std::llround(elapsedUs(positionNs_, nowNs, speed_)));
    positionNs_ = nowNs;
    playing_ = on;
}

void PlayoutClock::speed(float speed) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (speed > 0.0f && std::isfinite(speed)) speed_ = speed;
}

int64_t PlayoutClock::heardFrame(int epoch, int64_t written, int64_t nowNs) const {
    if (written <= 0) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    // Not the player's audio: a live source has no sink buffer in front of the listener.
    if (epoch != epoch_ || rateHz_ <= 0) return written;
    // The player's, but not timed yet: none of it has reached the speaker.
    if (!started_ || !positioned_) return 0;
    double heardUs = static_cast<double>(positionUs_ - startUs_);
    if (playing_) heardUs += elapsedUs(positionNs_, nowNs, speed_);
    const auto heard = static_cast<int64_t>(std::floor(heardUs * rateHz_ / 1e6));
    const int64_t maxLag = kMaxLagUs * rateHz_ / 1'000'000;
    return std::clamp<int64_t>(heard, std::max<int64_t>(0, written - maxLag), written);
}

}  // namespace geode::audio
