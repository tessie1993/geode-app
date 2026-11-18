#include "viz/DirectorStyle.hpp"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "viz/Catalogue.hpp"

namespace geode::viz::director {

namespace {

std::mutex gLock;
std::optional<StyleIntent> gIntent;  // guarded by gLock

// The pool as the shell sent it, one id a line, every line kept so an index is the shell's own.
std::vector<std::string_view> poolOf(std::string_view ids) {
    std::vector<std::string_view> out;
    if (ids.empty()) return out;
    while (true) {
        const size_t nl = ids.find('\n');
        std::string_view line = ids.substr(0, nl);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        out.push_back(line);
        if (nl == std::string_view::npos) break;
        ids.remove_prefix(nl + 1);
    }
    return out;
}

// splitmix64's finaliser, which spreads the intent's draw over a style's entries.
uint64_t spread(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// How many entries the pool holds for `id`.
int entriesOf(const std::vector<std::string_view>& pool, std::string_view id) {
    int n = 0;
    for (std::string_view line : pool) n += (!id.empty() && line == id) ? 1 : 0;
    return n;
}

// The pool entry a step to `id` lands on, or -1. A style the shell lists once is that entry. MilkDrop is listed
// once per .milk file, all under its one id: the step lands on one of them from the intent's `draw`, never the one
// on screen while another is there. The first entry used to stand for them all, so every moment that reached
// MilkDrop put up the same .milk.
int entryOf(const std::vector<std::string_view>& pool, std::string_view id, int onScreen, uint64_t draw) {
    if (id.empty()) return -1;
    std::vector<int> at;
    for (size_t i = 0; i < pool.size(); ++i) {
        if (pool[i] == id) at.push_back(static_cast<int>(i));
    }
    if (at.empty()) return -1;
    if (at.size() > 1) at.erase(std::remove(at.begin(), at.end(), onScreen), at.end());
    return at[static_cast<size_t>(spread(draw) % at.size())];
}

int familyOf(const std::string& id) {
    if (id.empty()) return -1;
    const std::string& known = catalogue::resolveId(id);
    const auto& families = catalogue::pickerFamilies();
    for (size_t f = 0; f < families.size(); ++f) {
        for (const std::string& member : families[f].ids) {
            if (member == known) return static_cast<int>(f);
        }
    }
    return -1;
}

// The next style along the catalogue's order from `from` that the pool holds and is not `from`. With none, and
// `from` listed more than once (a pool of .milk files alone), another of its own entries.
int progress(const std::vector<std::string_view>& pool, const std::string& from, int onScreen, uint64_t draw) {
    const auto& all = catalogue::allIds();
    const int n = static_cast<int>(all.size());
    if (n == 0) return kKeep;
    const std::string& known = from.empty() ? from : catalogue::resolveId(from);
    int at = -1;
    for (int i = 0; i < n; ++i) {
        if (all[static_cast<size_t>(i)] == known) {
            at = i;
            break;
        }
    }
    for (int j = 1; j <= n; ++j) {
        const std::string& next = all[static_cast<size_t>(((at + j) % n + n) % n)];
        if (next == known) continue;
        const int i = entryOf(pool, next, onScreen, draw);
        if (i >= 0) return i;
    }
    return entriesOf(pool, known) > 1 ? entryOf(pool, known, onScreen, draw) : kKeep;
}

// The first style the pool holds in the family after `from`'s, family by family.
int leap(const std::vector<std::string_view>& pool, const std::string& from, int onScreen, uint64_t draw) {
    const auto& families = catalogue::pickerFamilies();
    const int count = static_cast<int>(families.size());
    const int at = familyOf(from);
    for (int k = 1; k <= count; ++k) {
        const auto& family = families[static_cast<size_t>(((at + k) % count + count) % count)];
        if (static_cast<int>(&family - families.data()) == at) continue;
        for (const std::string& id : family.ids) {
            const int i = entryOf(pool, id, onScreen, draw);
            if (i >= 0) return i;
        }
    }
    return progress(pool, from, onScreen, draw);
}

// The remembered style's family, `step` styles on from it (wrapping inside the family), the first one the
// pool holds; the remembered style itself when its family has nothing else.
int returnTo(const std::vector<std::string_view>& pool, const StyleIntent& intent, const std::string& from, int onScreen) {
    const int f = familyOf(intent.remembered);
    if (f >= 0) {
        const auto& ids = catalogue::pickerFamilies()[static_cast<size_t>(f)].ids;
        const int n = static_cast<int>(ids.size());
        const std::string& known = catalogue::resolveId(intent.remembered);
        int at = 0;
        for (int i = 0; i < n; ++i) {
            if (ids[static_cast<size_t>(i)] == known) at = i;
        }
        for (int j = 0; j < n; ++j) {
            const int i = entryOf(pool, ids[static_cast<size_t>((at + intent.step + j) % n)], onScreen, intent.draw);
            if (i >= 0) return i;
        }
    }
    const int own = entryOf(pool, intent.remembered, onScreen, intent.draw);
    return own >= 0 ? own : progress(pool, from, onScreen, intent.draw);
}

}  // namespace

void publishStyleIntent(const StyleIntent& intent) {
    std::lock_guard<std::mutex> guard(gLock);
    gIntent = intent;
}

int pickStyle(std::string_view ids, int onScreen) {
    std::optional<StyleIntent> intent;
    {
        std::lock_guard<std::mutex> guard(gLock);
        intent.swap(gIntent);
    }
    if (!intent) return kNoIntent;
    return pickFor(*intent, ids, onScreen);
}

int pickFor(const StyleIntent& intent, std::string_view ids, int onScreen) {
    if (intent.kind == StyleIntent::Kind::None) return kNoIntent;
    const std::vector<std::string_view> pool = poolOf(ids);
    if (pool.empty()) return kKeep;
    // The style on screen: the director's own word for it, else the shell's.
    const bool shown = onScreen >= 0 && onScreen < static_cast<int>(pool.size());
    const std::string from = !intent.from.empty() ? intent.from : (shown ? std::string(pool[static_cast<size_t>(onScreen)]) : std::string());
    if (!intent.progress) {
        // The style stays; a return brings back its own.
        if (intent.kind != StyleIntent::Kind::Return) return kKeep;
        const int own = entryOf(pool, intent.remembered, onScreen, intent.draw);
        return own >= 0 ? own : kKeep;
    }
    switch (intent.kind) {
        case StyleIntent::Kind::Return:
            return intent.remembered.empty() ? progress(pool, from, onScreen, intent.draw) : returnTo(pool, intent, from, onScreen);
        case StyleIntent::Kind::Leap: return leap(pool, from, onScreen, intent.draw);
        case StyleIntent::Kind::Progress: return progress(pool, from, onScreen, intent.draw);
        case StyleIntent::Kind::None: break;
    }
    return kNoIntent;
}

}  // namespace geode::viz::director
