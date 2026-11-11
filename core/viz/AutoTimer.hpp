#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>

// Auto's style timer (Visuals › Settings: the random draw and the visual playlist): when the next look is
// due. MilkDrop waits its time between presets plus a random spread (16 s + up to 10 s, 5 s on its F3
// steps), so changes never tick like a metronome; here the wait is the user's interval plus up to a
// quarter of it, 10 s at most, drawn afresh for every look. The interval stays the floor: "every 20 s" is
// never sooner. Pure, host-tested in core/tests/auto_timer_test.cpp; the shell only polls it.
namespace geode::viz::auto_timer {

inline constexpr double kSpreadShare = 0.25;
inline constexpr double kSpreadMaxS = 10.0;

inline double spreadSeconds(double intervalS) {
    return std::clamp(intervalS * kSpreadShare, 0.0, kSpreadMaxS);
}

class Timer {
public:
    explicit Timer(std::uint64_t seed = 0) : state_(seed) { draw(); }

    // A look went up at nowMs (Auto's step, a step by hand, the timer switched on): the wait starts over
    // with a new spread.
    void restart(std::int64_t nowMs) {
        startMs_ = nowMs;
        draw();
    }

    // Whether the next look is due; true restarts the wait. The interval is read on every poll, so moving
    // its slider reaches the wait in progress. Not running (off, paused, nothing to step to) is never due.
    bool due(std::int64_t nowMs, double intervalS, bool running) {
        intervalS_ = intervalS;
        running_ = running;
        if (!running || static_cast<double>(nowMs - startMs_) < waitSeconds(intervalS) * 1000.0) return false;
        restart(nowMs);
        return true;
    }

    // How long the look on screen is expected to show, by the last poll's interval and the wait drawn
    // for this look: 0 while the timer is not running.
    double holdSeconds() const { return running_ ? waitSeconds(intervalS_) : 0.0; }

    double waitSeconds(double intervalS) const {
        const double base = std::max(intervalS, 0.0);
        return base + spreadSeconds(base) * share_;
    }

private:
    // splitmix64: one draw per look, 0 <= share < 1.
    void draw() {
        state_ += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        share_ = static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);
    }

    std::uint64_t state_;
    double share_ = 0.0;
    std::int64_t startMs_ = 0;
    double intervalS_ = 0.0;
    bool running_ = false;
};

// The session's two timers (geode_auto_timer_*): only one runs at a time, the shell's switches see to that.
inline constexpr int kPlaylist = 0;
inline constexpr int kRandom = 1;
inline constexpr int kTimerCount = 2;

namespace detail {

struct Shared {
    std::mutex lock;
    std::array<Timer, kTimerCount> timers;
    Shared() {
        const auto seed = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        for (int i = 0; i < kTimerCount; ++i) timers[static_cast<size_t>(i)] = Timer(seed + static_cast<std::uint64_t>(i));
    }
};

inline Shared& shared() {
    static Shared s;
    return s;
}

inline bool valid(int timer) { return timer >= 0 && timer < kTimerCount; }

}  // namespace detail

inline void restart(int timer, std::int64_t nowMs) {
    if (!detail::valid(timer)) return;
    auto& s = detail::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    s.timers[static_cast<size_t>(timer)].restart(nowMs);
}

inline bool due(int timer, std::int64_t nowMs, double intervalS, bool running) {
    if (!detail::valid(timer)) return false;
    auto& s = detail::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    return s.timers[static_cast<size_t>(timer)].due(nowMs, intervalS, running);
}

// How long Auto expects the look on screen to show, 0 when no timer runs. Any thread.
inline double liveHoldSeconds() {
    auto& s = detail::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    double hold = 0.0;
    for (const Timer& t : s.timers) hold = std::max(hold, t.holdSeconds());
    return hold;
}

}  // namespace geode::viz::auto_timer
