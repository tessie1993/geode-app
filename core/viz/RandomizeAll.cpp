#include "viz/RandomizeAll.hpp"

#include "viz/Catalogue.hpp"
#include "viz/CustomizeMap.hpp"

namespace geode::viz::randomize_all {
namespace {

// splitmix64: one stream per seed, the same on every device and in the tests.
uint64_t mix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// The pick and the roll each take their own stream from the one seed, so the shell can hand the
// same seed to both calls and neither repeats the other's draws.
constexpr uint64_t kPickStream = 0x5348554646ull;  // "SHUFF"
constexpr uint64_t kRollStream = 0x524F4C4Cull;    // "ROLL"

int below(uint64_t& state, int n) {
    state = mix(state);
    return n <= 1 ? 0 : static_cast<int>(state % static_cast<uint64_t>(n));
}

bool isMilk(const std::string& id) { return catalogue::kindOf(id) == catalogue::Kind::Milkdrop; }

}  // namespace

Pick pick(uint64_t seed, const std::string& current, const std::set<std::string>& lockedStyles,
          const std::vector<std::string>& lookStyles, int milkCount) {
    std::vector<std::string> open;
    for (const auto& id : catalogue::allIds()) {
        if (lockedStyles.count(id)) continue;
        if (isMilk(id) && milkCount <= 0) continue;
        open.push_back(id);
    }
    Pick out;
    if (open.empty()) return out;
    // A roll that lands on the style already on screen reads as nothing happened.
    std::vector<std::string> fresh;
    for (const auto& id : open) {
        if (id != current) fresh.push_back(id);
    }
    const auto& from = fresh.empty() ? open : fresh;
    uint64_t state = seed ^ kPickStream;
    out.style = from[static_cast<size_t>(below(state, static_cast<int>(from.size())))];

    std::vector<int> fits;
    for (int i = 0; i < static_cast<int>(lookStyles.size()); ++i) {
        if (lookStyles[static_cast<size_t>(i)] == out.style) fits.push_back(i);
    }
    if (!fits.empty()) {
        out.look = fits[static_cast<size_t>(below(state, static_cast<int>(fits.size())))];
    } else if (isMilk(out.style)) {
        out.milk = below(state, milkCount);
    }
    return out;
}

SceneParams roll(const SceneParams& before, const SceneParams& base, uint64_t seed, const std::string& styleId,
                 const std::set<std::string>& locked, bool rollCategories) {
    const std::set<std::string> kept = customize::expandLocks(locked);
    SceneParams out = base;
    for (const auto& key : kept) {
        for (const char* field : customize::fieldsOf(key)) out.set(field, before.get(field));
    }
    if (!rollCategories) return out;
    uint64_t state = seed ^ kRollStream;
    for (int i = 0; i < customize::kCategoryCount; ++i) {
        state = mix(state);
        out = customize::randomize(out, state, static_cast<customize::Category>(i), styleId, kept);
    }
    return out;
}

}  // namespace geode::viz::randomize_all
