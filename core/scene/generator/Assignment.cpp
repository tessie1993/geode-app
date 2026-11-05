#include "scene/generator/Assignment.hpp"

#include <mutex>

namespace geode::scene::generator {

namespace {

// SplitMix64's finaliser: a well-mixed 64-bit hash, the same on every platform and compiler.
std::uint64_t mix(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// One draw per (seed, moment, slot, purpose): purposes keep "is this slot in the subset" and "which id"
// independent of each other.
std::uint64_t draw(std::uint64_t seed, std::uint32_t moment, int slot, std::uint32_t purpose) {
    return mix(mix(mix(seed) ^ moment) ^ ((static_cast<std::uint64_t>(slot) << 32) | purpose));
}

constexpr std::uint32_t kInSubset = 1;
constexpr std::uint32_t kPick = 2;
constexpr std::uint32_t kFallback = 3;

// A slot the director may swap: assigned, unpinned, and the pool holds an id other than its own.
bool swappable(const SlotState& s, const std::vector<std::string>& pool) {
    if (s.pinned || s.id.empty()) return false;
    for (const auto& id : pool) {
        if (id != s.id) return true;
    }
    return false;
}

// Another pool id for this slot, chosen by the draw among the ids that are not its own.
void swapSlot(SlotState& s, const std::vector<std::string>& pool, std::uint64_t r) {
    std::size_t others = 0;
    for (const auto& id : pool) others += id != s.id ? 1 : 0;
    std::size_t k = static_cast<std::size_t>(r % others);
    for (const auto& id : pool) {
        if (id == s.id) continue;
        if (k-- == 0) {
            s.id = id;
            return;
        }
    }
}

}  // namespace

bool onMoment(Assignment& assignment, Moment moment, std::uint64_t playSeed, std::uint32_t momentIndex,
              const std::vector<std::string>& pool) {
    if (assignment.policy != Policy::DirectorSwaps) return false;

    std::array<bool, kSlotCount> chosen{};
    int candidates = 0;
    int picked = 0;
    for (int i = 0; i < kSlotCount; ++i) {
        if (!swappable(assignment.slots[i], pool)) continue;
        ++candidates;
        // A drop takes every candidate; a section each with even odds.
        chosen[i] = moment == Moment::Drop || (draw(playSeed, momentIndex, i, kInSubset) & 1u) != 0;
        picked += chosen[i] ? 1 : 0;
    }
    if (candidates == 0) return false;
    if (picked == 0) {
        // A section always swaps something: one candidate, by the draw.
        int k = static_cast<int>(draw(playSeed, momentIndex, kSlotCount, kFallback) % static_cast<std::uint64_t>(candidates));
        for (int i = 0; i < kSlotCount; ++i) {
            if (swappable(assignment.slots[i], pool) && k-- == 0) {
                chosen[i] = true;
                break;
            }
        }
    }
    for (int i = 0; i < kSlotCount; ++i) {
        if (chosen[i]) swapSlot(assignment.slots[i], pool, draw(playSeed, momentIndex, i, kPick));
    }
    return true;
}

namespace {
std::mutex& publishedLock() {
    static std::mutex lock;
    return lock;
}
Assignment& publishedSlot() {
    static Assignment assignment;
    return assignment;
}
}  // namespace

void publish(const Assignment& assignment) {
    std::lock_guard<std::mutex> lock(publishedLock());
    publishedSlot() = assignment;
}

Assignment published() {
    std::lock_guard<std::mutex> lock(publishedLock());
    return publishedSlot();
}

}  // namespace geode::scene::generator
