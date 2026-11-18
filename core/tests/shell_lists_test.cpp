// Host-side checks for core/shell/StyleShelf and core/shell/PresetList: the Styles page's own shelf (favourites,
// recents, "yours") and the Presets list's filter, order and applied mark. Every case of the Kotlin
// StyleShelfTest and PresetShelfTest (app/src/test/java/dev/geode/model/) is here with the same expected result,
// and the C ABI the app calls them through (core/api/geode_shell_api.h).
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/shell_lists_test.cpp core/shell/*.cpp
//     core/api/geode_shell_api.cpp core/util/Json.cpp -o shell_lists_test && ./shell_lists_test
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "api/geode_shell_api.h"
#include "shell/PresetList.hpp"
#include "shell/StyleShelf.hpp"

using namespace geode::shell;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

using Ids = std::vector<std::string>;

StyleShelf push(StyleShelf s, const std::string& id) { return styleShelfApply(s, {StyleShelfEvent::PushRecent, id, false}); }
StyleShelf toggle(StyleShelf s, const std::string& id) {
    return styleShelfApply(s, {StyleShelfEvent::ToggleFavourite, id, false});
}

void recentsNewestFirstWithoutRepeats() {
    const StyleShelf s = push(push(push(StyleShelf{}, "a"), "b"), "a");
    check(s.recents == Ids({"a", "b"}), "a b");
}

void recentsForgetPastTheLimit() {
    StyleShelf s;
    for (int i = 0; i < kStyleRecentLimit + 3; ++i) s = push(s, "s" + std::to_string(i));
    check(static_cast<int>(s.recents.size()) == kStyleRecentLimit, "kept the limit");
    check(s.recents.front() == "s" + std::to_string(kStyleRecentLimit + 2), "newest first");
}

void favouriteToggles() {
    const StyleShelf on = toggle(StyleShelf{}, "x");
    check(styleShelfIsFavourite(on, "x"), "on");
    check(!styleShelfIsFavourite(toggle(on, "x"), "x"), "off again");
}

void removeRecentLeavesFavourites() {
    StyleShelf s;
    s.favourites = {"a"};
    s.recents = {"a", "b"};
    const StyleShelf r = styleShelfApply(s, {StyleShelfEvent::RemoveRecent, "a", false});
    check(r.recents == Ids({"b"}) && r.favourites == Ids({"a"}), "removed from Recent only");
    // Kotlin's `list - id` drops the first occurrence only.
    StyleShelf twice;
    twice.recents = {"a", "b", "a"};
    check(styleShelfApply(twice, {StyleShelfEvent::RemoveRecent, "a", false}).recents == Ids({"b", "a"}), "first only");
}

void yoursOrder() {
    const Ids order{"arbor_a", "arbor_b", "silk_a", "fluid_a"};
    StyleShelf shelf;
    shelf.favourites = {"silk_a", "arbor_a"};
    shelf.recents = {"fluid_a", "missing", "arbor_a"};
    check(styleShelfYours(shelf, order, std::string("arbor_b")) == Ids({"arbor_b", "arbor_a", "silk_a", "fluid_a"}), "on screen, favourites, recent");
    StyleShelf recentOnly;
    recentOnly.recents = {"fluid_a", "gone"};
    check(styleShelfYours(recentOnly, order, std::nullopt) == Ids({"fluid_a"}), "recent only");
    check(styleShelfYours(StyleShelf{}, order, std::string("gone")).empty(), "nothing in order");
}

// A text-out list as the ids it holds (each followed by a newline), read through a buffer of `room` bytes.
template <typename Call>
Ids listOut(Call call, int room = 256) {
    std::vector<char> buf(static_cast<size_t>(room), 'z');
    const int need = call(buf.data(), room);
    check(need < room, "the list fits the test's buffer");
    Ids out;
    std::string at;
    for (int i = 0; i < need && i < room - 1; ++i) {
        if (buf[static_cast<size_t>(i)] == '\n') {
            out.push_back(at);
            at.clear();
        } else {
            at += buf[static_cast<size_t>(i)];
        }
    }
    return out;
}

void styleShelfThroughTheAbi() {
    const char* two[] = {"a", "b"};
    check(listOut([&](char* o, int c) { return geode_style_shelf_toggle_favourite(two, 2, "c", o, c); }) == Ids({"a", "b", "c"}),
          "abi: star appends");
    check(listOut([&](char* o, int c) { return geode_style_shelf_toggle_favourite(two, 2, "a", o, c); }) == Ids({"b"}),
          "abi: unstar removes");
    check(listOut([&](char* o, int c) { return geode_style_shelf_push_recent(two, 2, "b", o, c); }) == Ids({"b", "a"}),
          "abi: push moves to the front");
    check(listOut([&](char* o, int c) { return geode_style_shelf_push_recent(nullptr, 0, "x", o, c); }) == Ids({"x"}),
          "abi: push onto none");
    check(listOut([&](char* o, int c) { return geode_style_shelf_remove_recent(two, 2, "a", o, c); }) == Ids({"b"}),
          "abi: remove");
    check(listOut([&](char* o, int c) { return geode_style_shelf_remove_recent(two, 2, "q", o, c); }) == Ids({"a", "b"}),
          "abi: remove what is not there");
    std::vector<std::string> many;
    for (int i = 0; i < kStyleRecentLimit; ++i) many.push_back("s" + std::to_string(i));
    std::vector<const char*> manyIds;
    for (const auto& m : many) manyIds.push_back(m.c_str());
    const Ids pushed = listOut([&](char* o, int c) {
        return geode_style_shelf_push_recent(manyIds.data(), static_cast<int>(manyIds.size()), "new", o, c);
    });
    check(static_cast<int>(pushed.size()) == kStyleRecentLimit && pushed.front() == "new" && pushed.back() == many[many.size() - 2],
          "abi: the limit kept");
    const char* favourites[] = {"silk_a", "arbor_a"};
    const char* recents[] = {"fluid_a", "missing", "arbor_a"};
    const char* order[] = {"arbor_a", "arbor_b", "silk_a", "fluid_a"};
    check(listOut([&](char* o, int c) { return geode_style_shelf_yours(favourites, 2, recents, 3, order, 4, "arbor_b", o, c); }) ==
              Ids({"arbor_b", "arbor_a", "silk_a", "fluid_a"}),
          "abi: yours");
    check(listOut([&](char* o, int c) { return geode_style_shelf_yours(nullptr, 0, recents, 1, order, 4, nullptr, o, c); }) ==
              Ids({"fluid_a"}),
          "abi: yours with nothing on screen");
    char small[4] = {'z', 'z', 'z', 'z'};
    check(geode_style_shelf_toggle_favourite(two, 2, "c", small, 4) == 6 && std::string(small) == "a\nb", "abi: a short buffer");
    check(geode_style_shelf_toggle_favourite(two, 2, "c", nullptr, 0) == 6, "abi: the length alone");
}

// Presets

std::vector<PresetEntry> entries() {
    std::vector<PresetEntry> e(5);
    e[0] = {"Glow", "cymatics", true, "", true, false};
    e[1] = {"Ink", "fluid", true, "", false, false};
    e[2] = {"Mine A", "cymatics", false, "", true, false};
    e[3] = {"Party", "fluid", false, "Live", false, false};
    e[4] = {"Party", "fluid", false, "Live", false, false};
    return e;
}

Ids names(const std::vector<PresetEntry>& e, const PresetFilter& f) {
    Ids out;
    for (int i : presetApply(e, f)) out.push_back(e[static_cast<size_t>(i)].name);
    return out;
}

PresetFilter shelf(PresetFilter::Kind k) { return {k, ""}; }

void allPutsMineFirst() { check(names(entries(), shelf(PresetFilter::Kind::All)) == Ids({"Mine A", "Party", "Glow", "Ink"}), "all"); }

void shelvesSplit() {
    const auto e = entries();
    check(names(e, shelf(PresetFilter::Kind::Mine)) == Ids({"Mine A", "Party"}), "mine");
    check(names(e, shelf(PresetFilter::Kind::BuiltIn)) == Ids({"Glow", "Ink"}), "built-in");
    check(names(e, shelf(PresetFilter::Kind::ThisStyle)) == Ids({"Mine A", "Glow"}), "this style");
}

void folderHoldsOnlyUsersPresets() {
    const auto e = entries();
    check(names(e, {PresetFilter::Kind::Folder, "Live"}) == Ids({"Party"}), "live");
    check(names(e, {PresetFilter::Kind::Folder, "Empty"}).empty(), "empty");
}

void filtersListShelvesThenFolders() {
    const auto f = presetFilters({"Live", "", "Slow", "Live"});
    const std::vector<PresetFilter> want{{PresetFilter::Kind::All, ""},     {PresetFilter::Kind::ThisStyle, ""},
                                         {PresetFilter::Kind::Mine, ""},    {PresetFilter::Kind::BuiltIn, ""},
                                         {PresetFilter::Kind::Folder, "Live"}, {PresetFilter::Kind::Folder, "Slow"}};
    check(f == want, "shelves then folders");
    // A folder of only whitespace is blank (Kotlin isNotBlank), a folder with some is kept as written.
    check(presetFilters({" \t", " x "}).size() == 5, "blank dropped, padded kept");
}

void appliedPrefersLastTapped() {
    std::vector<PresetEntry> staged(3);
    staged[0] = {"A", "s", true, "", false, true};
    staged[1] = {"B", "s", false, "", false, true};
    staged[2] = {"C", "s", false, "", false, false};
    check(presetApplied(staged, std::string("B")) == 1, "B while on stage");
    check(presetApplied(staged, std::string("C")) == 0, "C is not on stage: first on stage");
    check(presetApplied(staged, std::nullopt) == 0, "none tapped: first on stage");
    for (auto& s : staged) s.onStage = false;
    check(presetApplied(staged, std::string("B")) == -1, "nothing on stage");
}

void presetsThroughTheAbi() {
    const auto e = entries();
    std::vector<const char*> rowNames;
    std::vector<const char*> rowFolders;
    std::vector<int> flags;
    for (const PresetEntry& x : e) {
        rowNames.push_back(x.name.c_str());
        rowFolders.push_back(x.folder.c_str());
        flags.push_back((x.builtIn ? GEODE_PRESET_BUILT_IN : 0) | (x.forThisStyle ? GEODE_PRESET_FOR_THIS_STYLE : 0) |
                        (x.onStage ? GEODE_PRESET_ON_STAGE : 0));
    }
    const int n = static_cast<int>(e.size());
    auto shown = [&](int kind, const char* folder) {
        std::vector<int> rows(e.size(), -1);
        const int k = geode_preset_apply(rowNames.data(), rowFolders.data(), flags.data(), n, kind, folder, rows.data());
        Ids out;
        for (int i = 0; i < k; ++i) out.push_back(e[static_cast<size_t>(rows[static_cast<size_t>(i)])].name);
        return out;
    };
    check(shown(GEODE_PRESET_FILTER_ALL, nullptr) == Ids({"Mine A", "Party", "Glow", "Ink"}), "abi: all");
    check(shown(GEODE_PRESET_FILTER_MINE, nullptr) == Ids({"Mine A", "Party"}), "abi: mine");
    check(shown(GEODE_PRESET_FILTER_BUILT_IN, nullptr) == Ids({"Glow", "Ink"}), "abi: built-in");
    check(shown(GEODE_PRESET_FILTER_THIS_STYLE, nullptr) == Ids({"Mine A", "Glow"}), "abi: this style");
    check(shown(GEODE_PRESET_FILTER_FOLDER, "Live") == Ids({"Party"}), "abi: a folder");
    check(shown(GEODE_PRESET_FILTER_FOLDER, "Empty").empty(), "abi: an empty folder");

    const char* folders[] = {"Live", "", "Slow", "Live"};
    int chips[16];
    for (int& c : chips) c = -9;
    check(geode_preset_filters(folders, 4, chips, 8) == 6, "abi: six chips");
    const std::vector<int> want{GEODE_PRESET_FILTER_ALL,      -1, GEODE_PRESET_FILTER_THIS_STYLE, -1, GEODE_PRESET_FILTER_MINE,   -1,
                                GEODE_PRESET_FILTER_BUILT_IN, -1, GEODE_PRESET_FILTER_FOLDER,     0,  GEODE_PRESET_FILTER_FOLDER, 2};
    check(std::vector<int>(chips, chips + 12) == want && chips[12] == -9, "abi: shelves, then each folder by its first index");
    check(geode_preset_filters(nullptr, 0, nullptr, 0) == 4, "abi: the shelves alone");

    const char* staged[] = {"A", "B", "C"};
    int stage[] = {GEODE_PRESET_BUILT_IN | GEODE_PRESET_ON_STAGE, GEODE_PRESET_ON_STAGE, 0};
    check(geode_preset_applied(staged, stage, 3, "B") == 1, "abi: B while on stage");
    check(geode_preset_applied(staged, stage, 3, "C") == 0, "abi: C is not on stage");
    check(geode_preset_applied(staged, stage, 3, nullptr) == 0, "abi: none tapped");
    for (int& f : stage) f &= ~GEODE_PRESET_ON_STAGE;
    check(geode_preset_applied(staged, stage, 3, "B") == -1, "abi: nothing on stage");
}

}  // namespace

int main() {
    recentsNewestFirstWithoutRepeats();
    recentsForgetPastTheLimit();
    favouriteToggles();
    removeRecentLeavesFavourites();
    yoursOrder();
    styleShelfThroughTheAbi();
    allPutsMineFirst();
    shelvesSplit();
    folderHoldsOnlyUsersPresets();
    filtersListShelvesThenFolders();
    appliedPrefersLastTapped();
    presetsThroughTheAbi();
    if (failures == 0) std::printf("shell_lists_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
