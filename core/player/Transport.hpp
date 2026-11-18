#pragma once

#include <cstdint>

// The transport half of the native player: the position in the current track, whether it is meant to play,
// whether the queue has ended, the seek rules and the A-B loop. Pure logic; the position is whatever the
// decode thread last reported. Not for callers: they use Player.hpp.
namespace geode::player {

// A previous that comes more than this far into a track restarts the track instead of going back.
inline constexpr double kPrevRestartSeconds = 3.0;

// An A-B loop point further than this past A marks B; an earlier one moves A.
inline constexpr std::int64_t kAbLoopMinMs = 1000;

inline constexpr std::int64_t kNoPoint = -1;

struct AbLoop {
    std::int64_t startMs = kNoPoint;
    std::int64_t endMs = kNoPoint;
};

class Transport {
public:
    // A track became current: from `startMs` (held inside it), not ended, no loop.
    void load(std::int64_t durationMs, std::int64_t startMs);
    void unload();
    void setDuration(std::int64_t durationMs) { durationMs_ = durationMs; }

    // False when ignored: a track of unknown length cannot be sought. Brings an ended queue back.
    bool seek(std::int64_t positionMs);
    void restart();
    // The decode thread's progress. Returns where the loop sent the position, or kNoPoint.
    std::int64_t tick(std::int64_t positionMs);
    void finish();  // the last track ran out

    void cycleLoop();
    void clearLoop() { loop_ = {}; }

    void setPlayWhenReady(bool on) { playWhenReady_ = on; }

    bool playWhenReady() const { return playWhenReady_; }
    bool ended() const { return ended_; }
    bool playing() const { return playWhenReady_ && !ended_; }
    std::int64_t positionMs() const { return positionMs_; }
    std::int64_t durationMs() const { return durationMs_; }
    AbLoop loop() const { return loop_; }
    bool pastRestartPoint() const;

private:
    bool playWhenReady_ = false;
    bool ended_ = false;
    std::int64_t positionMs_ = 0;
    std::int64_t durationMs_ = 0;
    AbLoop loop_;
};

}  // namespace geode::player
