#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

// How other threads hand commands to a stage thread or the vsync thread (docs/agents/native-stage.md). Pure, with
// no Android, so the host can test it (core/tests/command_queue_test.cpp).
//
// A bounded ring of fixed-size commands under one mutex: nothing is allocated after construction, so a frame
// request from the vsync thread or a size from the main thread never touches the heap. A command pushed as
// LastWins overwrites a pending one of the same kind and target (sizes, frame requests, a cap: only the last value
// matters), but never merges across an Ordered one (a window hand-over, quit), so those keep their place.
//
// The consumer is woken through a plain callback when the queue turns non-empty: the platform layer passes
// ALooper_wake. A consumer drains until empty each time it wakes, so one wake per empty-to-non-empty is enough.
namespace geode::stage {

struct Command {
    uint16_t kind = 0;
    int32_t a = 0;
    int32_t b = 0;
    float f = 0.0f;
    float g = 0.0f;
    int64_t t = 0;
    void* target = nullptr;
};

enum class Merge : uint8_t { Ordered, LastWins };
enum class Pushed : uint8_t { Queued, Merged, Full };

template <size_t N>
class CommandQueue {
    static_assert(N > 0, "a queue holds at least one command");

public:
    using Wake = void (*)(void* context);

    CommandQueue() = default;
    CommandQueue(Wake wake, void* context) : wake_(wake), context_(context) {}
    CommandQueue(const CommandQueue&) = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;

    // Set before the first push, or while no other thread pushes.
    void setWake(Wake wake, void* context) {
        std::lock_guard<std::mutex> lock(lock_);
        wake_ = wake;
        context_ = context;
    }

    Pushed push(const Command& command, Merge merge) {
        Wake wake = nullptr;
        void* context = nullptr;
        {
            std::lock_guard<std::mutex> lock(lock_);
            if (merge == Merge::LastWins) {
                // Newest first, stopping at the first Ordered command: merging past it would reorder the two.
                for (size_t i = count_; i > 0; --i) {
                    Slot& slot = slots_[(head_ + i - 1) % N];
                    if (slot.merge != Merge::LastWins) break;
                    if (slot.command.kind == command.kind && slot.command.target == command.target) {
                        slot.command = command;
                        return Pushed::Merged;
                    }
                }
            }
            if (count_ == N) return Pushed::Full;
            slots_[(head_ + count_) % N] = Slot{command, merge};
            if (++count_ == 1) {
                wake = wake_;
                context = context_;
            }
        }
        // Outside the lock: the wake may take the looper's own lock.
        if (wake) wake(context);
        return Pushed::Queued;
    }

    // Takes up to `max` commands, oldest first. Returns how many.
    size_t drain(Command* out, size_t max) {
        std::lock_guard<std::mutex> lock(lock_);
        size_t n = 0;
        while (n < max && count_ > 0) {
            out[n++] = slots_[head_].command;
            head_ = (head_ + 1) % N;
            --count_;
        }
        return n;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(lock_);
        return count_;
    }

    static constexpr size_t capacity() { return N; }

private:
    struct Slot {
        Command command;
        Merge merge = Merge::Ordered;
    };

    mutable std::mutex lock_;
    std::array<Slot, N> slots_{};
    size_t head_ = 0;
    size_t count_ = 0;
    Wake wake_ = nullptr;
    void* context_ = nullptr;
};

}  // namespace geode::stage
