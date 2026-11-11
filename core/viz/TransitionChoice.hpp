#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Which transition a style change plays and for how long. MilkDrop picks its blend at random on every
// preset change and gives a change by hand a shorter fade than an automatic one (1.7 s and 2.7 s,
// plugin.cpp), which is much of why it feels alive. Here:
// - the transition id "random" draws one of the catalogue's transitions per change, never the same one
//   twice in a row (and never "cut", which safety plays as a fade anyway);
// - an automatic change (Auto's timer, the visual playlist, a director moment) fades for MilkDrop's
//   ratio of the user's Transition length (auto = length × 2.7 / 1.7), at most kMaxMs; a change by hand
//   fades for the length itself. No tempo enters either. Until Settings has a row for the automatic time,
//   the ratio is the rule.
// Auto marks its changes here (geode_auto_mark_change) before it makes them; each renderer reads the mark
// for the style it switches to, each MilkDrop scene for the preset it loads. Host-tested in
// core/tests/transition_choice_test.cpp.
namespace geode::viz::transition_choice {

inline constexpr const char* kRandomId = "random";
inline constexpr const char* kCutId = "cut";
inline constexpr int64_t kMaxMs = 5000;
// MilkDrop's own two fades (plugin.cpp: m_fBlendTimeUser, m_fBlendTimeAuto).
inline constexpr double kManualSeconds = 1.7;
inline constexpr double kAutoSeconds = 2.7;

inline bool isRandom(const std::string& id) { return id == kRandomId; }

// The fade of an automatic change for the user's Transition length (ms).
inline int64_t autoMs(int64_t manualMs) {
    const int64_t m = std::max<int64_t>(manualMs, 1);
    const auto scaled = static_cast<int64_t>(static_cast<double>(m) * (kAutoSeconds / kManualSeconds) + 0.5);
    return std::max(m, std::min(scaled, kMaxMs));
}

inline int64_t durationMs(int64_t manualMs, bool automatic) {
    return automatic ? autoMs(manualMs) : std::max<int64_t>(manualMs, 1);
}

// splitmix64, one draw per change.
inline uint64_t nextRandom(uint64_t& state) {
    state += 0x9E3779B97F4A7C15ull;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// One of `ids`, uniformly, leaving out "cut", "random" and `last` (unless nothing else is left); "fade"
// when there is nothing to draw from.
inline std::string pick(const std::vector<std::string>& ids, const std::string& last, uint64_t& state) {
    std::vector<const std::string*> choices;
    const std::string* lastChoice = nullptr;
    for (const auto& id : ids) {
        if (id == kCutId || id == kRandomId || id.empty()) continue;
        if (id == last) {
            lastChoice = &id;
            continue;
        }
        choices.push_back(&id);
    }
    if (choices.empty()) return lastChoice ? *lastChoice : std::string("fade");
    return *choices[nextRandom(state) % choices.size()];
}

// Auto's marks: which style or preset its next change brings, and when. A mark is good for kMarkWindowS
// (a new style is staged over a few frames before it switches) and is read once by each reader.
inline constexpr double kMarkWindowS = 5.0;
inline constexpr size_t kMaxMarks = 8;

class Marks {
public:
    struct Mark {
        std::string key;
        uint64_t seq = 0;
        double atS = 0.0;
    };

    void mark(const std::string& key, double nowS) {
        if (key.empty()) return;
        marks_.erase(std::remove_if(marks_.begin(), marks_.end(), [&](const Mark& m) { return m.key == key || nowS - m.atS > kMarkWindowS; }),
                     marks_.end());
        if (marks_.size() >= kMaxMarks) marks_.erase(marks_.begin());
        marks_.push_back(Mark{key, ++seq_, nowS});
    }

    // Whether `key` was marked within the window and after `seenSeq`, the last mark this reader took.
    bool take(const std::string& key, double nowS, uint64_t& seenSeq) const {
        for (const Mark& m : marks_) {
            if (m.key != key || m.seq <= seenSeq || nowS - m.atS > kMarkWindowS || nowS < m.atS) continue;
            seenSeq = m.seq;
            return true;
        }
        return false;
    }

private:
    std::vector<Mark> marks_;
    uint64_t seq_ = 0;
};

namespace detail {

struct Shared {
    std::mutex lock;
    Marks marks;
};

inline Shared& shared() {
    static Shared s;
    return s;
}

inline double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace detail

// Auto is about to bring `key` (a style id, or a .milk path) on. Any thread.
inline void markAuto(const std::string& key) {
    auto& s = detail::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    s.marks.mark(key, detail::nowSeconds());
}

// Whether the change to `key` a reader is making now is Auto's. `seenSeq` is the reader's own.
inline bool takeAuto(const std::string& key, uint64_t& seenSeq) {
    auto& s = detail::shared();
    std::lock_guard<std::mutex> lock(s.lock);
    return s.marks.take(key, detail::nowSeconds(), seenSeq);
}

}  // namespace geode::viz::transition_choice
