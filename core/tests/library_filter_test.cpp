// Host-side checks for the search sheet's text filter (core/library/Search, filterRows, geode_library_filter).
// Built and run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no device.
//
// What is pinned (#341, the port of the Kotlin SearchMatcher, and #395's rewrite of it): the query is trimmed
// and split on whitespace, every term must be inside some field, both sides folded (lower case without the
// locale, accents stripped, ß æ œ ø ł đ ð þ spelled out); hits rank title, artist, album, then the rest, typo
// hits (an option) last, stably inside a tier; rows that share a key (a track's uri) collapse to one at the
// first match's place, keeping a preferred row (the device copy) when one matches, with the better tier; the
// list is cut to the cap after ranking; a blank query lists nothing unless asked to list everything.
// Every case of #395's SearchMatcherTest and the search cases of its LibraryBrowseTest are below.
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "api/geode_api.h"
#include "library/Search.hpp"

using geode::library::FilterOptions;
using geode::library::FilterRow;
using geode::library::filterRows;
using geode::library::filterTerms;
using geode::library::filterTier;
using geode::library::foldText;

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
    check(run(kTracks, "beyonce") == std::vector<int>({2}), "accents are folded: \"beyonce\" finds \"Beyoncé\"");
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
    // "Radio Gaga" (title) outranks Radiohead (artist) although it comes last; the cap cuts after ranking.
    check(run(kTracks, "radio", 2) == std::vector<int>({5, 4}), "ranked, then cut to the cap, preferred copies in");
    check(run(kTracks, "radio", 0).empty(), "a zero cap lists nothing");
    std::vector<Row> many;
    for (int i = 0; i < 50; ++i) many.push_back({nullptr, false, {"Track"}});
    check(run(many, "track", 30).size() == 30, "the sheet's 30-track cap");
}

// --- #395's SearchMatcherTest and LibraryBrowseTest, against the native rule ---------------------------------

int tierOf(std::initializer_list<std::string_view> fields, std::string_view query, bool typos = false) {
    const std::vector<std::string_view> f(fields);
    FilterOptions options;
    options.typos = typos;
    return filterTier(f, query, options);
}

void foldStripsAccentsAndCase() {
    check(foldText("Beyoncé") == "beyonce", "fold: Beyoncé");
    check(foldText("Sigur Rós") == "sigur ros", "fold: Sigur Rós");
    check(foldText("MOTÖRHEAD") == "motorhead", "fold: MOTÖRHEAD");
    check(foldText("Café") == "cafe", "fold: Café");
    check(foldText("Beyonce\xCC\x81") == "beyonce", "fold: an already decomposed accent (U+0301) is dropped");
    check(foldText("Đường Phố") == "duong pho", "fold: Vietnamese");
    check(foldText("Dvořák Ångström Øresund") == "dvorak angstrom oresund", "fold: caron, ring, stroke");
    check(foldText("Şarkı Ţară") == "sarki tara", "fold: Turkish and Romanian cedilla, dotless i");
    check(foldText("Ǹǚ Ș Ț") == "nu s t", "fold: Latin Extended-B");
    check(foldText("Ά ά Ϋ ΐ") == "α α υ ι", "fold: Greek tonos and dialytika");
    check(foldText("Ёлка Йошкар") == "елка иошкар", "fold: Cyrillic breve and diaeresis, as NFD splits them");
    check(foldText("ガギグ パ ヴ が") == "カキク ハ ウ か", "fold: kana voicing marks");
}

void foldSpellsOutLettersThatDoNotDecompose() {
    check(foldText("Straße") == "strasse", "fold: ß");
    check(foldText("STRAẞE") == "strasse", "fold: capital sharp s");
    check(foldText("Røisin") == "roisin", "fold: ø");
    check(foldText("Łódź") == "lodz", "fold: ł");
    check(foldText("Æther") == "aether", "fold: æ");
    check(foldText("Œuvre") == "oeuvre", "fold: œ");
    check(foldText("Þór Đorđe Ðað") == "thor dorde dad", "fold: þ, đ, ð");
    check(foldText("Ǽ ǿ") == "ae o", "fold: æ and ø under an accent");
    check(foldText("ħ ŧ") == "ħ ŧ", "fold: letters NFD leaves whole stay, as in Kotlin");
}

void foldIgnoresThePhoneLocale() {
    // "I" folds to a dotted i whatever the phone's language, or "INXS" stops matching "inxs" in Turkish.
    check(foldText("INXS") == "inxs", "fold: INXS");
    check(foldText("İstanbul") == "istanbul", "fold: İstanbul");
    check(foldText("ıI") == "ii", "fold: dotless i");
    check(foldText("Plain ASCII 123") == "plain ascii 123", "fold: ASCII fast path");
}

void plainQueryFindsAccentedTitle() {
    const std::vector<Row> accented = {{nullptr, false, {"Beyoncé"}}};
    const std::vector<Row> plain = {{nullptr, false, {"beyonce"}}};
    check(run(accented, "beyonce") == std::vector<int>({0}), "\"beyonce\" finds \"Beyoncé\"");
    check(run(plain, "BEYONCÉ") == std::vector<int>({0}), "\"BEYONCÉ\" finds \"beyonce\"");
    check(run(accented, "adele").empty(), "\"adele\" does not");
    const std::vector<Row> cafe = {{"1", false, {"Café del Mar"}}, {"2", false, {"Tea"}}};
    check(run(cafe, "cafe") == std::vector<int>({0}), "voice search folds too (#395 voiceSearchFoldsToo)");
}

void everyTermMustMatch() {
    const std::vector<Row> halo = {{nullptr, false, {"Halo", "Beyoncé", "I Am... Sasha Fierce"}}};
    check(run(halo, "halo beyonce") == std::vector<int>({0}), "both terms land");
    check(run(halo, "halo adele").empty(), "one term in no field");
    check(run(halo, "   ").empty(), "a blank query is no search");
}

void tierIsTheFieldTheMatchNeeded() {
    const auto fields = {std::string_view("Halo"), std::string_view("Beyoncé"), std::string_view("Sasha Fierce"),
                         std::string_view("/music/pop")};
    check(tierOf(fields, "halo") == 0, "title");
    check(tierOf(fields, "beyonce") == 1, "artist");
    check(tierOf(fields, "fierce") == 2, "album");
    check(tierOf(fields, "pop") == 3, "the rest");
    check(tierOf(fields, "halo beyonce") == 1, "split across title and artist ranks as the artist");
    check(tierOf(fields, "adele") == -1, "no match");
    check(tierOf(fields, "") == 0, "blank is tier 0");
    check(tierOf({"Yesterday"}, "yesterdya", true) == 1 && tierOf({"Yesterday"}, "yesterdya") == -1,
          "a typo hit ranks at fields + field, and only with typos on");
    check(tierOf({"Cep"}, "cpe", true) == -1, "a three-letter word has no spare edits");
}

void rankTitleAboveArtistAboveAlbum() {
    // Input order is the caller's sort; title order alone would put "album" first.
    const std::vector<Row> rows = {
        {"album", false, {"Intro", "Someone", "Love Songs"}},
        {"artist", false, {"Intro", "Love Inc.", "Hits"}},
        {"title", false, {"Love Story", "Someone", "Hits"}},
    };
    check(run(rows, "love") == std::vector<int>({2, 1, 0}), "title, then artist, then album");
    const std::vector<Row> fourFields = {
        {"folder", false, {"Aaa", "Someone", "Something", "/music/love"}},
        {"album", false, {"Bbb", "Someone", "Love Songs", "/music"}},
        {"title", false, {"Zzz Love", "Someone", "Something", "/music"}},
    };
    check(run(fourFields, "love") == std::vector<int>({2, 1, 0}), "the folder after the album");
}

void sortHoldsWithinATier() {
    const std::vector<Row> rows = {
        {"a", false, {"Love A"}}, {"b", false, {"Love B"}}, {"x", false, {"Other", "Love Inc."}},
        {"c", false, {"Love C"}},
    };
    check(run(rows, "love") == std::vector<int>({0, 1, 3, 2}), "the caller's order inside each tier");
}

void typoStillFindsButBelowExactHits() {
    const std::vector<Row> rows = {
        {"typo", false, {"Yesterday", "Someone", "Something", "/music"}},
        {"exact", false, {"Other", "Someone", "Yesterdya Live", "/music"}},
    };
    FilterOptions typos;
    typos.typos = true;
    std::vector<FilterRow> table;
    for (const Row& r : rows) table.push_back(FilterRow{std::optional<std::string_view>(r.key), false, r.fields});
    check(filterRows(table, "yesterdya", 10, typos) == std::vector<int>({1, 0}),
          "the album spelled as typed beats the title one typo off");
    check(filterRows(table, "yesterdya", 10) == std::vector<int>({1}), "without typos only the exact hit");
}

void blankKeepsAllWhenAsked() {
    FilterOptions all;
    all.blankKeepsAll = true;
    std::vector<FilterRow> table;
    for (const Row& r : kTracks) {
        table.push_back(FilterRow{r.key ? std::optional<std::string_view>(r.key) : std::nullopt, r.preferred,
                                  r.fields});
    }
    check(filterRows(table, "  ", 100, all) == std::vector<int>({2, 4, 3, 5}),
          "the whole list, deduped to the preferred copies, in order");
    check(filterRows(table, "  ", 100).empty(), "and nothing without the option");
}

void preferredCopyKeepsTheBetterTier() {
    // The library row matches on its title; the device copy has a different title tag but the same artist.
    const std::vector<Row> rows = {
        {"u", false, {"Love Me Do", "The Beatles"}},
        {"v", false, {"Help", "Love Inc."}},
        {"u", true, {"Love Me Do (Remastered)", "The Beatles"}},
    };
    check(run(rows, "love") == std::vector<int>({2, 1}), "the device copy at title rank");
    const std::vector<Row> worse = {
        {"v", false, {"Help", "Love Inc."}},
        {"u", false, {"Love Me Do", "The Beatles"}},
        {"u", true, {"Track 01", "Love"}},
    };
    check(run(worse, "love") == std::vector<int>({2, 0}),
          "a preferred copy that needs the artist keeps the title rank");
}

void cAbiMarshalsRows() {
    // key, title, artist per row
    const char* cells[] = {"u1", "Creep", "Radiohead", "u2", "Teardrop", "Massive Attack", "u1", "Creep", "Radiohead",
                           nullptr, "Teardrop", "Cover Band"};
    const unsigned char preferred[] = {0, 0, 1, 0};
    int out[4] = {-1, -1, -1, -1};
    check(geode_library_filter(cells, 4, 2, preferred, "creep", 0, out, 4) == 1 && out[0] == 2,
          "a key's preferred row is the one written");
    check(geode_library_filter(cells, 4, 2, nullptr, "teardrop", 0, out, 4) == 2 && out[0] == 1 && out[1] == 3,
          "a null key stands alone");
    check(geode_library_filter(cells, 4, 2, nullptr, "teardrop", 0, out, 1) == 1 && out[0] == 1,
          "max_out is the cap, and the count is what was written");
    check(geode_library_filter(cells, 4, 2, nullptr, nullptr, 0, out, 4) == 0, "a null query lists nothing");
    check(geode_library_filter(nullptr, 0, 2, nullptr, "x", 0, nullptr, 0) == 0, "no rows, no crash");
    check(geode_library_filter(cells, 4, 2, nullptr, "radiohead teardrop", 0, out, 4) == 0, "every term, one row");
    check(geode_library_filter(cells, 4, 2, preferred, "", GEODE_LIBRARY_FILTER_BLANK_ALL, out, 4) == 3 &&
              out[0] == 2 && out[1] == 1 && out[2] == 3,
          "BLANK_ALL: a blank query keeps every row, deduped, in order");
    check(geode_library_filter(cells, 4, 2, nullptr, "teardorp", 0, out, 4) == 0, "no typos without the flag");
    check(geode_library_filter(cells, 4, 2, nullptr, "teardorp", GEODE_LIBRARY_FILTER_TYPOS, out, 4) == 2,
          "TYPOS: a swapped pair is two edits, inside an eight-letter word's budget");

    char folded[16];
    check(geode_library_fold("Beyoncé", folded, sizeof folded) == 7 && std::string(folded) == "beyonce",
          "geode_library_fold writes the folded UTF-8");
    char tiny[4];
    check(geode_library_fold("Straße", tiny, sizeof tiny) == 7 && std::string(tiny) == "str",
          "a short buffer is cut and NUL-terminated; the return is the full length");
    check(geode_library_fold(nullptr, folded, sizeof folded) == 0 && folded[0] == '\0', "null text folds to empty");
}

}  // namespace

int main() {
    termsAreTrimmedAndSplit();
    everyTermInSomeField();
    caseIsIgnoredLikeKotlin();
    dedupeKeepsThePreferredRow();
    capComesAfterDedupe();
    foldStripsAccentsAndCase();
    foldSpellsOutLettersThatDoNotDecompose();
    foldIgnoresThePhoneLocale();
    plainQueryFindsAccentedTitle();
    everyTermMustMatch();
    tierIsTheFieldTheMatchNeeded();
    rankTitleAboveArtistAboveAlbum();
    sortHoldsWithinATier();
    typoStillFindsButBelowExactHits();
    blankKeepsAllWhenAsked();
    preferredCopyKeepsTheBetterTier();
    cAbiMarshalsRows();
    if (failures == 0) std::printf("library_filter_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
