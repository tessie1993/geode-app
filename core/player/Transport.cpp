#include "player/Transport.hpp"

#include <algorithm>

namespace geode::player {

void Transport::load(std::int64_t durationMs, std::int64_t startMs) {
    durationMs_ = durationMs;
    positionMs_ = durationMs > 0 ? std::clamp<std::int64_t>(startMs, 0, durationMs) : std::max<std::int64_t>(startMs, 0);
    ended_ = false;
    loop_ = {};
}

void Transport::unload() {
    playWhenReady_ = false;
    load(0, 0);
}

bool Transport::seek(std::int64_t positionMs) {
    if (durationMs_ <= 0) return false;
    positionMs_ = std::clamp<std::int64_t>(positionMs, 0, durationMs_);
    ended_ = false;
    return true;
}

void Transport::restart() {
    positionMs_ = 0;
    ended_ = false;
}

std::int64_t Transport::tick(std::int64_t positionMs) {
    positionMs_ = std::max<std::int64_t>(positionMs, 0);
    if (loop_.startMs < 0 || loop_.endMs < 0 || positionMs_ < loop_.endMs) return kNoPoint;
    positionMs_ = loop_.startMs;
    return positionMs_;
}

void Transport::finish() {
    if (durationMs_ > 0) positionMs_ = durationMs_;
    ended_ = true;
    loop_ = {};
}

void Transport::cycleLoop() {
    const std::int64_t at = std::max<std::int64_t>(positionMs_, 0);
    if (loop_.startMs < 0) {
        loop_ = {at, kNoPoint};
    } else if (loop_.endMs >= 0) {
        loop_ = {};
    } else if (at > loop_.startMs + kAbLoopMinMs) {
        loop_.endMs = at;
    } else {
        loop_ = {at, kNoPoint};
    }
}

bool Transport::pastRestartPoint() const {
    return static_cast<double>(positionMs_) > kPrevRestartSeconds * 1000.0;
}

}  // namespace geode::player
