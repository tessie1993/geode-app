#include "viz/LayerState.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <utility>

namespace geode::viz::layer_state {

namespace {

std::mutex& tableLock() {
    static std::mutex m;
    return m;
}

Layer& stored() {
    static Layer l;
    return l;
}

// Polled every frame by every renderer, so it is an atomic outside the lock; bumped under the lock, so
// a reader that sees the new value and then takes the lock reads the new layer.
std::atomic<uint64_t>& counter() {
    static std::atomic<uint64_t> c{0};
    return c;
}

}  // namespace

void set(const Layer& layer) {
    Layer clean = layer;
    clean.mix = std::isfinite(layer.mix) ? std::clamp(layer.mix, 0.0f, 1.0f) : 0.0f;
    std::lock_guard<std::mutex> guard(tableLock());
    if (stored() == clean) return;
    stored() = std::move(clean);
    counter().fetch_add(1, std::memory_order_acq_rel);
}

Layer get() {
    std::lock_guard<std::mutex> guard(tableLock());
    return stored();
}

uint64_t generation() { return counter().load(std::memory_order_acquire); }

void reset() { set(Layer{}); }

bool Follower::poll(Layer& out) {
    if (pinned_) {
        if (!pinDirty_) return false;
        pinDirty_ = false;
        out = pinnedLayer_;
        return true;
    }
    // The generation is read before the value: a set landing in between is simply seen again next poll.
    const uint64_t g = generation();
    if (seen_ && *seen_ == g) return false;
    seen_ = g;
    out = get();
    return true;
}

void Follower::pin() {
    pinnedLayer_ = get();
    pinned_ = true;
    pinDirty_ = true;
}

void Follower::unpin() {
    if (!pinned_) return;
    pinned_ = false;
    pinDirty_ = false;
    seen_.reset();
}

}  // namespace geode::viz::layer_state
