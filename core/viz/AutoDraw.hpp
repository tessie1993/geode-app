#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>

// Auto's random draw (Visuals › Settings › Auto: styles, saved looks and .milk presets in one pool): which entry
// comes next. Never the one already on screen while there is anything else to draw, so a timer tick or a
// director moment always changes something; MilkDrop's own random pick skips the preset playing the same way.
// The old shell draw only re-rolled once, and only for a bare style, so the same .milk could come up twice
// running. Host-tested in core/tests/auto_timer_test.cpp.
namespace geode::viz::auto_draw {

class Draw {
public:
    explicit Draw(std::uint64_t seed = 0) : state_(seed) {}

    // An index in 0..count-1, never [onScreen] while count > 1 (onScreen < 0 or past the end: nothing in the
    // pool is on screen). -1 for an empty pool.
    int pick(int count, int onScreen) {
        if (count <= 0) return -1;
        const bool skip = count > 1 && onScreen >= 0 && onScreen < count;
        const int span = skip ? count - 1 : count;
        int idx = static_cast<int>(next() % static_cast<std::uint64_t>(span));
        if (skip && idx >= onScreen) ++idx;
        return idx;
    }

private:
    // splitmix64, as Auto's timer draws its spread.
    std::uint64_t next() {
        state_ += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    std::uint64_t state_;
};

namespace detail {

struct Shared {
    std::mutex lock;
    Draw draw{static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
};

inline Shared& shared() {
    static Shared s;
    return s;
}

}  // namespace detail

// The session's draw (geode_auto_pick).
inline int pick(int count, int onScreen) {
    auto& s = detail::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    return s.draw.pick(count, onScreen);
}

}  // namespace geode::viz::auto_draw
