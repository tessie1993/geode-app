#include "viz/DirectorStyle.hpp"

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

int indexIn(const std::vector<std::string_view>& pool, std::string_view id) {
    if (id.empty()) return -1;
    for (size_t i = 0; i < pool.size(); ++i) {
        if (pool[i] == id) return static_cast<int>(i);
    }
    return -1;
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

// The next style along the catalogue's order from `from` that the pool holds and is not `from`.
int progress(const std::vector<std::string_view>& pool, const std::string& from) {
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
        const int i = indexIn(pool, next);
        if (i >= 0) return i;
    }
    return kKeep;
}

// The first style the pool holds in the family after `from`'s, family by family.
int leap(const std::vector<std::string_view>& pool, const std::string& from) {
    const auto& families = catalogue::pickerFamilies();
    const int count = static_cast<int>(families.size());
    const int at = familyOf(from);
    for (int k = 1; k <= count; ++k) {
        const auto& family = families[static_cast<size_t>(((at + k) % count + count) % count)];
        if (static_cast<int>(&family - families.data()) == at) continue;
        for (const std::string& id : family.ids) {
            const int i = indexIn(pool, id);
            if (i >= 0) return i;
        }
    }
    return progress(pool, from);
}

// The remembered style's family, `step` styles on from it (wrapping inside the family), the first one the
// pool holds; the remembered style itself when its family has nothing else.
int returnTo(const std::vector<std::string_view>& pool, const StyleIntent& intent, const std::string& from) {
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
            const int i = indexIn(pool, ids[static_cast<size_t>((at + intent.step + j) % n)]);
            if (i >= 0) return i;
        }
    }
    const int own = indexIn(pool, intent.remembered);
    return own >= 0 ? own : progress(pool, from);
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
        const int own = indexIn(pool, intent.remembered);
        return own >= 0 ? own : kKeep;
    }
    switch (intent.kind) {
        case StyleIntent::Kind::Return: return intent.remembered.empty() ? progress(pool, from) : returnTo(pool, intent, from);
        case StyleIntent::Kind::Leap: return leap(pool, from);
        case StyleIntent::Kind::Progress: return progress(pool, from);
        case StyleIntent::Kind::None: break;
    }
    return kNoIntent;
}

}  // namespace geode::viz::director
