// Host-side checks for the search sheet's text filter (core/library/Search, filterRows, geode_library_filter).
// Built and run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no device.
//
// What is pinned (#341, the port of the Kotlin SearchMatcher): the query is trimmed and split on whitespace,
// every term must be inside some field, case-insensitive like Kotlin's contains(ignoreCase = true); rows that
// share a key (a track's uri) collapse to one at the first match's place, keeping a preferred row (the device
// copy) when one matches; the list is cut to the cap after that; a blank query lists nothing.
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "api/geode_api.h"
#include "library/Search.hpp"

using geode::library::FilterRow;
using geode::library::filterRows;
using geode::library::filterTerms;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

struct Row {
    const char* key;  // nullptr: stands alone
    bool preferred;
    std::vector<std::string_view> fields;
};

std::vector<int> run(const std::vector<Row>& rows, std::string_view query, std::size_t max = 100) {
    std::vector<FilterRow> table;
    for (const Row& r : rows) {
        table.push_back(FilterRow{r.key ? std::optional<std::string_view>(r.key) : std::nullopt, r.preferred,
                                  r.fields});
    }
    return filterRows(table, query, max);
}

const std::vector<Row> kTracks = {
    {"a", false, {"Halo", "Beyoncé", "I Am... Sasha Fierce", "Pop"}},      // 0
    {"b", false, {"Creep", "Radiohead", "Pablo Honey", "Rock"}},          // 1
    {"a", true, {"Halo", "Beyoncé", "I Am... Sasha Fierce", "/Music"}},    // 2: device copy of 0
    {"c", true, {"Airbag", "Radiohead", "OK Computer", "/Music/Radio"}},  // 3
    {"b", true, {"Creep", "Radiohead", "Pablo Honey", "/Music"}},         // 4: device copy of 1
    {"d", false, {"Radio Gaga", "Queen", "The Works", "Rock"}},           // 5
};

void termsAreTrimmedAndSplit() {
    check(filterTerms("  Radio   Head\t") == std::vector<std::string>({"radio", "head"}),
          "trimmed, split on runs of whitespace, folded");
    check(filterTerms("").empty() && filterTerms(" \t\n ").empty(), "blank has no terms");
    // A no-break space trims like Kotlin's trim() but does not split, as `\s+` did not.
    check(filterTerms("\xC2\xA0" "ab\xC2\xA0" "cd ") == std::vector<std::string>({"ab\xC2\xA0" "cd"}),
          "NBSP is trimmed at the ends, kept inside");
    check(filterTerms("x \xC2\xA0 y") == std::vector<std::string>({"x", "y"}), "an NBSP-only piece is blank");
}

void everyTermInSomeField() {
    check(run(kTracks, "radiohead") == std::vector<int>({4, 3}), "an artist, deduped, in first-match order");
    check(run(kTracks, "radiohead creep") == std::vector<int>({4}), "more words narrow");
    check(run(kTracks, "radio rock") == std::vector<int>({1, 5}), "terms may land in different fields");
    check(run(kTracks, "adele").empty(), "nothing matches");
    check(run(kTracks, "").empty() && run(kTracks, "   ").empty(), "a blank query lists nothing");
    check(run(kTracks, "dio gag") == std::vector<int>({5}), "a term is a substring, not a word");
}

void caseIsIgnoredLikeKotlin() {
    check(run(kTracks, "HALO") == std::vector<int>({2}), "ASCII upper case");
    check(run(kTracks, "BEYONCÉ") == std::vector<int>({2}), "Latin-1 upper case");
    check(run(kTracks, "beyonce").empty(), "no accent folding: Kotlin's ignoreCase keeps accents");
    const std::vector<Row> world = {
        {"1", false, {"Ąžuolas"}},           // Latin Extended-A
        {"2", false, {"Đường Phố"}},         // Vietnamese, Latin Extended Additional
        {"3", false, {"КИНО"}},              // Cyrillic
        {"4", false, {"ΣΩΚΡ\xCE\x86ΤΗΣ"}},  // Greek with tonos (U+0386) and a final sigma
        {"5", false, {"INXS"}},              // Turkish dotless and dotted i fold to i
        {"6", false, {"Ｆｕｌｌ"}},           // full-width
        {"7", false, {"Արամ"}},              // Armenian
        {"8", false, {"Lǚ"}},                // Pinyin
    };
    check(run(world, "ąŽUOLAS") == std::vector<int>({0}), "Latin Extended-A");
    check(run(world, "ĐƯỜNG") == std::vector<int>({1}), "Vietnamese");
    check(run(world, "кино") == std::vector<int>({2}), "Cyrillic");
    check(run(world, "σωκρ\xCE\xACτης") == std::vector<int>({3}), "Greek, final sigma");
    check(run(world, "\xC4\xB1nxs") == std::vector<int>({4}) && run(world, "\xC4\xB0NXS") == std::vector<int>({4}),
          "dotless and dotted I");
    check(run(world, "ｆＵＬＬ") == std::vector<int>({5}), "full-width");
    check(run(world, "ԱՐԱՄ") == std::vector<int>({6}), "Armenian");
    check(run(world, "LǙ") == std::vector<int>({7}), "Pinyin");
}

void dedupeKeepsThePreferredRow() {
    check(run(kTracks, "halo") == std::vector<int>({2}), "the device copy replaces the library row in place");
    const std::vector<Row> rows = {
        {"x", false, {"Song"}},
        {"y", false, {"Song Two"}},
        {"x", true, {"Other"}},  // preferred but no match: does not replace
        {"x", true, {"Song"}},   // preferred and a match: replaces, keeps slot 0
        {"x", true, {"Song"}},   // a second preferred match: the first one stays
        {"x", false, {"Song"}},  // not preferred: never replaces
    };
    check(run(rows, "song") == std::vector<int>({3, 1}), "first preferred match, at the first match's place");
    const std::vector<Row> loose = {{nullptr, false, {"Mix"}}, {nullptr, false, {"Mix"}}};
    check(run(loose, "mix") == std::vector<int>({0, 1}), "rows without a key are never merged");
}

void capComesAfterDedupe() {
    check(run(kTracks, "radio", 2) == std::vector<int>({4, 3}), "cut to the cap, preferred copies still swap in");
    check(run(kTracks, "radio", 0).empty(), "a zero cap lists nothing");
    std::vector<Row> many;
    for (int i = 0; i < 50; ++i) many.push_back({nullptr, false, {"Track"}});
    check(run(many, "track", 30).size() == 30, "the sheet's 30-track cap");
}

void cAbiMarshalsRows() {
    // key, title, artist per row
    const char* cells[] = {"u1", "Creep", "Radiohead", "u2", "Teardrop", "Massive Attack", "u1", "Creep", "Radiohead",
                           nullptr, "Teardrop", "Cover Band"};
    const unsigned char preferred[] = {0, 0, 1, 0};
    int out[4] = {-1, -1, -1, -1};
    check(geode_library_filter(cells, 4, 2, preferred, "creep", out, 4) == 1 && out[0] == 2,
          "a key's preferred row is the one written");
    check(geode_library_filter(cells, 4, 2, nullptr, "teardrop", out, 4) == 2 && out[0] == 1 && out[1] == 3,
          "a null key stands alone");
    check(geode_library_filter(cells, 4, 2, nullptr, "teardrop", out, 1) == 1 && out[0] == 1,
          "max_out is the cap, and the count is what was written");
    check(geode_library_filter(cells, 4, 2, nullptr, nullptr, out, 4) == 0, "a null query lists nothing");
    check(geode_library_filter(nullptr, 0, 2, nullptr, "x", nullptr, 0) == 0, "no rows, no crash");
}

}  // namespace

int main() {
    termsAreTrimmedAndSplit();
    everyTermInSomeField();
    caseIsIgnoredLikeKotlin();
    dedupeKeepsThePreferredRow();
    capComesAfterDedupe();
    cAbiMarshalsRows();
    if (failures == 0) std::printf("library_filter_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
