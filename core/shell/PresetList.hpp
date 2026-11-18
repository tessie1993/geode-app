#pragma once

#include <optional>
#include <string>
#include <vector>

// The Presets list's rules, ported from Kotlin model/PresetShelf.kt (PresetFilter, PresetFiltering): which
// filter chips show, which rows a chip leaves and in what order, and the one row that gets the check. The
// list never needs the look itself, only what decides which rows show and with which marks; the page builds
// the entries (forThisStyle is worked out by the page, since the style families live in the catalogue;
// onStage says the stage shows this preset's look right now). Host-tested in core/tests/shell_lists_test.cpp.
namespace geode::shell {

struct PresetEntry {
    std::string name;
    std::string sceneId;
    bool builtIn = false;
    std::string folder;
    bool forThisStyle = false;
    bool onStage = false;
};

// One filter chip: a shelf, or one of the user's folders (name set).
struct PresetFilter {
    enum class Kind { All, ThisStyle, Mine, BuiltIn, Folder };
    Kind kind = Kind::All;
    std::string name;
    bool operator==(const PresetFilter& o) const { return kind == o.kind && name == o.name; }
};

// The chips in order: the four shelves, then the user's folders as the store lists them (blank ones dropped,
// each once). (PresetFiltering.filters)
std::vector<PresetFilter> presetFilters(const std::vector<std::string>& folders);

// The rows the filter leaves, as indices into `entries`, in list order: the user's own first, then the
// built-ins, those made for the style on stage ahead of the rest; a name shows once (its first row), and
// rows of equal rank keep their order (Kotlin's sortedWith is stable). (PresetFiltering.apply)
std::vector<int> presetApply(const std::vector<PresetEntry>& entries, const PresetFilter& filter);

// The one row that gets the check, as an index into `entries`, or -1: the preset the user last tapped wins
// while the stage still shows its look; otherwise the first preset whose look the stage shows. -1 when none
// is on stage. (PresetFiltering.applied)
int presetApplied(const std::vector<PresetEntry>& entries, const std::optional<std::string>& lastTapped);

}  // namespace geode::shell
