#pragma once

#include <optional>
#include <string>
#include <vector>

// The user's own shelf on the Styles page, ported from Kotlin model/StyleShelf.kt: the styles they starred,
// the ones they chose last, and whether Random stays in the family on screen. The rules are newest first, no
// repeats, a short memory. The prefs file that keeps it stays in the shell. Host-tested in
// core/tests/shell_lists_test.cpp.
namespace geode::shell {

// About two rows of the grid on a phone: more than that is not "recent" any more.
inline constexpr int kStyleRecentLimit = 12;

// favourites is a set that keeps the order it was added in; recents is newest first.
struct StyleShelf {
    std::vector<std::string> favourites;
    std::vector<std::string> recents;
    bool randomFamilyOnly = false;
};

struct StyleShelfEvent {
    enum Kind {
        ToggleFavourite,     // id: starred if it was not, unstarred if it was
        PushRecent,          // id moves to the front; no repeats; kStyleRecentLimit kept
        RemoveRecent,        // id leaves Recent (its first place only, as Kotlin's `list - id`); favourites untouched
        SetRandomFamilyOnly  // flag
    };
    Kind kind = ToggleFavourite;
    std::string id;
    bool flag = false;
};

// The shelf after the event (StyleShelf.toggleFavourite / pushRecent / removeRecent / withRandomFamilyOnly).
StyleShelf styleShelfApply(const StyleShelf& shelf, const StyleShelfEvent& event);

bool styleShelfIsFavourite(const StyleShelf& shelf, const std::string& id);

// Your styles: the one on screen, then favourites in `order`, then recent, each of `order`'s ids once.
std::vector<std::string> styleShelfYours(const StyleShelf& shelf, const std::vector<std::string>& order,
                                         const std::optional<std::string>& onScreen);

}  // namespace geode::shell
