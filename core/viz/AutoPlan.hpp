#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "viz/AutoTimer.hpp"

// Auto visuals' decisions (geode_auto_*): when the playlist steps and to which entry, when the timed draw is
// due, whether a director moment is the user's (they changed the look since Auto's last change) or Auto's,
// and the pool a draw picks from. The shell lists its playlist, and saved looks, carries out the
// change native names, and polls. Customize being open or nothing playing holds every change. Host-tested in
// core/tests/auto_plan_test.cpp.
namespace geode::viz::auto_plan {

inline constexpr int kIntervalMinS = 5;
inline constexpr int kIntervalMaxS = 300;

inline int clampInterval(int seconds) { return std::clamp(seconds, kIntervalMinS, kIntervalMaxS); }

// What a director moment brings.
inline constexpr int kMomentNone = 0;
inline constexpr int kMomentPlaylist = 1;  // the playlist's next entry
inline constexpr int kMomentRandom = 2;    // a draw that keeps the look (styles only)

// A draw's sources, and the kinds of entry it names.
inline constexpr int kDrawStyles = 1;
inline constexpr int kDrawPresets = 2;
inline constexpr int kDrawKeepLook = 8;
inline constexpr int kKindStyle = 0;
inline constexpr int kKindPreset = 1;

// The look as Auto left it: style, params frame, saved palette id. Frames compare bit for bit, NaN equal to NaN.
struct Look {
    std::string style;
    std::vector<float> frame;
    std::string paletteId;

    bool operator==(const Look& o) const {
        if (style != o.style || paletteId != o.paletteId || frame.size() != o.frame.size()) return false;
        for (size_t i = 0; i < frame.size(); ++i) {
            const float a = frame[i];
            const float b = o.frame[i];
            if (std::isnan(a) && std::isnan(b)) continue;
            std::uint32_t ba = 0;
            std::uint32_t bb = 0;
            std::memcpy(&ba, &a, sizeof a);
            std::memcpy(&bb, &b, sizeof b);
            if (ba != bb) return false;
        }
        return true;
    }
};

class Plan {
public:
    // Customize opened or closed; closing starts both waits over.
    void setEditing(bool on, std::int64_t nowMs) {
        if (editing_ && !on) restartBoth(nowMs);
        editing_ = on;
    }

    // A style or a look picked by hand: both waits start over.
    void handPick(std::int64_t nowMs) { restartBoth(nowMs); }

    // The playlist's entry to put up now, or -1. Runs while it is on, has two entries or more and nothing holds.
    int playlistTick(int count, bool enabled, bool playing, int intervalS, std::int64_t nowMs) {
        const bool running = enabled && count >= 2 && !holding(playing);
        if (!auto_timer::due(auto_timer::kPlaylist, nowMs, intervalS, running)) return -1;
        index_ = (index_ + 1) % count;
        return index_;
    }

    // A step by hand (step < 0 back) or a moment's: the entry to put up, or -1 with fewer than two.
    int playlistStep(int count, int step, std::int64_t nowMs) {
        if (count < 2) return -1;
        auto_timer::restart(auto_timer::kPlaylist, nowMs);
        index_ = ((index_ + (step < 0 ? -1 : 1)) % count + count) % count;
        return index_;
    }

    // Whether the timed draw is due now.
    bool randomTick(bool enabled, bool playing, int intervalS, std::int64_t nowMs) {
        return auto_timer::due(auto_timer::kRandom, nowMs, intervalS, enabled && !holding(playing));
    }

    // A director moment on the look on screen. A look changed since Auto's last change is the user's: that
    // moment is skipped, and so is every one while they keep editing.
    int moment(Look now, bool playing, bool playlistEnabled) {
        if (holding(playing)) return kMomentNone;
        const bool touched = leftAs_ && !(*leftAs_ == now);
        leftAs_ = std::move(now);
        if (touched) return kMomentNone;
        return playlistEnabled ? kMomentPlaylist : kMomentRandom;
    }

    // Auto's own change went up (a moment, the timer, the playlist): this look is not the user's hand.
    void leftAs(Look look) { leftAs_ = std::move(look); }

private:
    bool holding(bool playing) const { return editing_ || !playing; }

    static void restartBoth(std::int64_t nowMs) {
        auto_timer::restart(auto_timer::kPlaylist, nowMs);
        auto_timer::restart(auto_timer::kRandom, nowMs);
    }

    int index_ = 0;
    bool editing_ = false;
    std::optional<Look> leftAs_;
};

// One draw's pool, in its order: the styles, then the saved looks. ids is each entry's style
// id a line (geode_auto_pick_among's pool); entries maps a pool index to {kind, index in its own list}.
struct Pool {
    std::string ids;
    int onScreen = -1;
    std::vector<std::pair<int, int>> entries;
};

// styles: Auto's draw list (Catalogue's AutoDraw trait); locked: the styles the entitlement refuses;
// presetStyles: each saved look's style; presetOnScreen: the first saved look equal to the look on screen
// (-1 none).
inline Pool pool(const std::vector<std::string>& styles, const std::set<std::string>& locked, const std::string& current,
                 const std::vector<std::string>& presetStyles, int presetOnScreen, int sources) {
    Pool p;
    auto add = [&](const std::string& id, int kind, int index, bool here) {
        if (here && p.onScreen < 0) p.onScreen = static_cast<int>(p.entries.size());
        p.ids += id;
        p.ids += '\n';
        p.entries.emplace_back(kind, index);
    };
    if (sources & kDrawStyles) {
        for (size_t i = 0; i < styles.size(); ++i) {
            if (locked.count(styles[i])) continue;
            add(styles[i], kKindStyle, static_cast<int>(i), styles[i] == current);
        }
    }
    if ((sources & kDrawPresets) && !(sources & kDrawKeepLook)) {
        for (size_t i = 0; i < presetStyles.size(); ++i) {
            if (locked.count(presetStyles[i])) continue;
            add(presetStyles[i], kKindPreset, static_cast<int>(i), static_cast<int>(i) == presetOnScreen);
        }
    }
    if (!p.ids.empty()) p.ids.pop_back();
    return p;
}

namespace detail {

struct Shared {
    std::mutex lock;
    Plan plan;
};

inline Shared& shared() {
    static Shared s;
    return s;
}

}  // namespace detail

}  // namespace geode::viz::auto_plan
