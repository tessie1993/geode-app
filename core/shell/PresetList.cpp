#include "shell/PresetList.hpp"

#include <algorithm>
#include <set>

#include "shell/Text.hpp"

namespace geode::shell {

std::vector<PresetFilter> presetFilters(const std::vector<std::string>& folders) {
    std::vector<PresetFilter> out{{PresetFilter::Kind::All, ""},
                                  {PresetFilter::Kind::ThisStyle, ""},
                                  {PresetFilter::Kind::Mine, ""},
                                  {PresetFilter::Kind::BuiltIn, ""}};
    std::vector<std::string> seen;
    for (const std::string& f : folders) {
        if (text::isBlank(f)) continue;  // Kotlin isNotBlank
        if (std::find(seen.begin(), seen.end(), f) != seen.end()) continue;
        seen.push_back(f);
        out.push_back({PresetFilter::Kind::Folder, f});
    }
    return out;
}

std::vector<int> presetApply(const std::vector<PresetEntry>& entries, const PresetFilter& filter) {
    std::vector<int> shown;
    std::set<std::string> names;
    for (size_t i = 0; i < entries.size(); ++i) {
        const PresetEntry& e = entries[i];
        bool keep = false;
        switch (filter.kind) {
            case PresetFilter::Kind::All: keep = true; break;
            case PresetFilter::Kind::ThisStyle: keep = e.forThisStyle; break;
            case PresetFilter::Kind::Mine: keep = !e.builtIn; break;
            case PresetFilter::Kind::BuiltIn: keep = e.builtIn; break;
            case PresetFilter::Kind::Folder: keep = !e.builtIn && e.folder == filter.name; break;
        }
        // distinctBy runs after the filter, so a name already shown by an earlier kept row is the one skipped.
        if (keep && names.insert(e.name).second) shown.push_back(static_cast<int>(i));
    }
    // compareBy { builtIn }.thenBy { builtIn && !forThisStyle }, false before true.
    std::stable_sort(shown.begin(), shown.end(), [&](int a, int b) {
        const PresetEntry& x = entries[static_cast<size_t>(a)];
        const PresetEntry& y = entries[static_cast<size_t>(b)];
        if (x.builtIn != y.builtIn) return !x.builtIn;
        const bool xr = x.builtIn && !x.forThisStyle;
        const bool yr = y.builtIn && !y.forThisStyle;
        return !xr && yr;
    });
    return shown;
}

int presetApplied(const std::vector<PresetEntry>& entries, const std::optional<std::string>& lastTapped) {
    int first = -1;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!entries[i].onStage) continue;
        if (first < 0) first = static_cast<int>(i);
        if (lastTapped && entries[i].name == *lastTapped) return static_cast<int>(i);
    }
    return first;
}

}  // namespace geode::shell
