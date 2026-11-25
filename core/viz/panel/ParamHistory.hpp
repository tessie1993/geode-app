#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The Customize panel's undo and redo (geode_param_history_*): every panel write records the look it
// replaces. A slider drag is one gesture and one undo, so edits closer than kCheckpointGapMs fold into one
// entry; a discrete write (a preset, a roll, a reset) is always its own. Host-tested in
// core/tests/param_history_test.cpp.
namespace geode::viz::param_history {

inline constexpr std::size_t kDepth = 64;
inline constexpr std::int64_t kCheckpointGapMs = 700;

// One look as the shell holds it: the params frame and the saved palette's id ("" for none).
struct Look {
    std::vector<float> frame;
    std::string paletteId;
    // The style the look was made on: an entry is put up only on that style, since a frame's params mean what the
    // style shows of them.
    std::string styleId;
};

inline constexpr int kCanUndo = 1;
inline constexpr int kCanRedo = 2;

class History {
public:
    // [before] is about to be replaced at nowMs. discrete: a preset, a roll, a reset.
    void record(Look before, std::int64_t nowMs, bool discrete) {
        if (discrete) {
            push(std::move(before));
            last_.reset();
            return;
        }
        if (!last_ || nowMs - *last_ > kCheckpointGapMs) push(std::move(before));
        last_ = nowMs;
    }

    // The look to put up instead of [current]: the last undone one with redo, else the last recorded; empty
    // when there is none.
    std::optional<Look> step(bool redo, Look current) {
        auto& from = redo ? redo_ : undo_;
        auto& to = redo ? undo_ : redo_;
        // Entries recorded on another style are not this look's past: they are dropped, not applied across.
        while (!from.empty() && from.back().styleId != current.styleId) from.pop_back();
        if (from.empty()) return std::nullopt;
        Look back = std::move(from.back());
        from.pop_back();
        to.push_back(std::move(current));
        trim(to);
        last_.reset();
        return back;
    }

    int state() const { return (undo_.empty() ? 0 : kCanUndo) | (redo_.empty() ? 0 : kCanRedo); }

private:
    void push(Look look) {
        undo_.push_back(std::move(look));
        trim(undo_);
        redo_.clear();
    }

    static void trim(std::deque<Look>& stack) {
        while (stack.size() > kDepth) stack.pop_front();
    }

    std::deque<Look> undo_;
    std::deque<Look> redo_;
    std::optional<std::int64_t> last_;
};

namespace detail {

struct Shared {
    std::mutex lock;
    History history;
};

inline Shared& shared() {
    static Shared s;
    return s;
}

}  // namespace detail

}  // namespace geode::viz::param_history
