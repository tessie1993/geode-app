// Host-side checks for the search sheet's text filter (core/library/Search, filterRows, geode_library_filter).
// Built and run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no device.
//
// What is pinned (#341, the port of the Kotlin SearchMatcher): the query is trimmed and split on whitespace,
// every term must be inside some field, case-insensitive like Kotlin's contains(ignoreCase = true); rows that
// share a key (a track's uri) collapse to one at the first match's place, keeping a preferred row (the device
// copy) when one matches; the list is cut to the cap after that; a blank query lists nothing.
#include <cstdint>
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

// --- Mutant-driven checks ----------------------------------------------------------------------------------
// Seams: filterRows / filterTerms (library/Search.hpp) and geode_library_filter (api/geode_api.h). Expected
// values are hand-worked literals, Unicode's own case pairs as Kotlin's contains(ignoreCase = true) answers
// them, or the deleted Kotlin SearchMatcher.filterTracks (kotlinFilterTracks below). Each check names the
// mutant of #507's code it fails against.

// Test-side UTF-8 writer, so the cases below can be spelled as code points.
std::string utf8(std::u32string_view s) {
    std::string out;
    for (char32_t c : s) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// Whether a track whose one field is `field` is found by `query`.
bool finds(std::u32string_view field, std::u32string_view query) {
    const std::string f = utf8(field);
    const std::string q = utf8(query);
    return run({{nullptr, false, {f}}}, q) == std::vector<int>({0});
}

std::vector<std::string> termsOf(std::u32string_view query) { return filterTerms(utf8(query)); }

// Kotlin's trim() strips every Char.isWhitespace (Zs, Zl, Zp, U+0009..U+000D, U+001C..U+001F) at the ends,
// but `\s+` splits only on space and U+0009..U+000D.
void queryEdgesTrimLikeKotlinAndSplitLikeJavaRegex() {
    // Kills: the 0x1C..0x20 trim range narrowed to the space alone.
    check(termsOf(U"\x1C" U"ab\x1F") == std::vector<std::string>({"ab"}), "U+001C and U+001F trim like trim()");
    // Kills: U+3000 dropped from the whitespace set.
    check(termsOf(U"　ab　") == std::vector<std::string>({"ab"}), "an ideographic space trims");
    // Kills: the U+2000..U+200A run cut short at either end.
    check(termsOf(U" ab ") == std::vector<std::string>({"ab"}), "U+2000 and U+200A trim");
    // Kills: the regex split widened to every whitespace (U+3000 and U+001C are not `\s`).
    check(termsOf(U"a　b") == std::vector<std::string>({utf8(U"a　b")}), "U+3000 inside does not split");
    check(termsOf(U"a\x1C" U"b") == std::vector<std::string>({utf8(U"a\x1C" U"b")}),
          "U+001C inside does not split");
    // Kills: the `\s` run 0x09..0x0D cut short (vertical tab, carriage return).
    check(termsOf(U"a\vb\rc") == std::vector<std::string>({"a", "b", "c"}), "VT and CR split like \\s");
    // Kills: the blank-piece drop testing emptiness instead of Kotlin's isNotBlank.
    check(termsOf(U"x 　 y") == std::vector<std::string>({"x", "y"}), "a piece of U+3000 alone is blank");
}

struct CasePair {
    std::u32string_view field;
    std::u32string_view query;
    const char* what;  // the mutant it kills
};

// Each pair matches under Kotlin's contains(ignoreCase = true) (equal upper case, or equal lower case of the
// upper case), checked both ways. Each is a range end or a special case of the fold.
void caseFoldingMatchesKotlinAtEveryBoundary() {
    const CasePair pairs[] = {
        {U"µ", U"μ", "micro sign to Greek mu removed"},
        {U"Μ", U"µ", "micro sign to Greek mu removed (capital mu)"},
        {U"Sing", U"ſing", "long s to s removed"},
        {U"k-pop", U"K-POP", "Kelvin sign to k removed"},
        {U"å", U"Å", "Angstrom sign to a-ring removed"},
        {U"ω", U"Ω", "Ohm sign to omega removed"},
        {U"ß", U"ẞ", "capital sharp s to sharp s removed"},
        {U"ÿ", U"Ÿ", "Y with diaeresis to y removed"},
        {U"à", U"À", "Latin-1 range starting past 0xC0"},
        {U"þ", U"Þ", "Latin-1 range ending before 0xDE (thorn)"},
        {U"ā", U"Ā", "Latin Extended-A even/odd rule flipped"},
        {U"ĺ", U"Ĺ", "odd-upper run starting past 0x139"},
        {U"ŀ", U"Ŀ", "odd-upper run broken inside (L with middle dot)"},
        {U"ň", U"Ň", "odd-upper run ending before 0x148"},
        {U"ź", U"Ź", "second odd-upper run starting past 0x179"},
        {U"ơ", U"Ơ", "Vietnamese O with horn dropped"},
        {U"ǎ", U"Ǎ", "Pinyin run starting past 0x1CD"},
        {U"ǜ", U"Ǜ", "Pinyin run ending before 0x1DC"},
        {U"ǟ", U"Ǟ", "Extended-B run starting past 0x1DE"},
        {U"ǹ", U"Ǹ", "Extended-B run starting past 0x1F8"},
        {U"ș", U"Ș", "Romanian S with comma below"},
        {U"ȟ", U"Ȟ", "Extended-B run ending before 0x21F"},
        {U"ȳ", U"Ȳ", "Extended-B run ending before 0x233"},
        {U"ά", U"Ά", "Greek alpha with tonos removed"},
        {U"έ", U"Έ", "Greek tonos run 0x388..0x38A offset off by one"},
        {U"ί", U"Ί", "Greek tonos run ending before 0x38A"},
        {U"ό", U"Ό", "Greek omicron with tonos removed"},
        {U"ύ", U"Ύ", "Greek upsilon with tonos offset off by one"},
        {U"ώ", U"Ώ", "Greek tonos run ending before 0x38F"},
        {U"α", U"Α", "Greek range starting past 0x391"},
        {U"ϋ", U"Ϋ", "Greek range ending before 0x3AB"},
        {U"Σ", U"ς", "final sigma to sigma removed"},
        {U"ѐ", U"Ѐ", "Cyrillic 0x400 run starting past 0x400"},
        {U"ё", U"Ё", "Cyrillic Io (+80) offset"},
        {U"џ", U"Џ", "Cyrillic 0x400 run ending before 0x40F"},
        {U"а", U"А", "Cyrillic A starting past 0x410"},
        {U"я", U"Я", "Cyrillic Ya ending before 0x42F"},
        {U"ѡ", U"Ѡ", "Cyrillic 0x460 run starting past 0x460"},
        {U"ҋ", U"Ҋ", "Cyrillic 0x48A run starting past 0x48A"},
        {U"ұ", U"Ұ", "Kazakh straight u (0x48A run broken inside)"},
        {U"ӏ", U"Ӏ", "palochka to its small form removed"},
        {U"ӂ", U"Ӂ", "Cyrillic odd-upper run 0x4C1 rule flipped"},
        {U"ӎ", U"Ӎ", "Cyrillic odd-upper run ending before 0x4CE"},
        {U"ӑ", U"Ӑ", "Cyrillic 0x4D0 run starting past 0x4D0"},
        {U"ә", U"Ә", "Cyrillic schwa (0x4D0 run broken inside)"},
        {U"ԯ", U"Ԯ", "Cyrillic 0x4D0 run ending before 0x52F"},
        {U"ֆ", U"Ֆ", "Armenian range ending before 0x556"},
        {U"ḁ", U"Ḁ", "Extended Additional starting past 0x1E00"},
        {U"ẕ", U"Ẕ", "Extended Additional first run ending before 0x1E95"},
        {U"ạ", U"Ạ", "Extended Additional second run starting past 0x1EA0"},
        {U"ỿ", U"Ỿ", "Extended Additional second run ending before 0x1EFF"},
        {U"ⅰ", U"Ⅰ", "Roman numerals starting past 0x2160"},
        {U"ⅿ", U"Ⅿ", "Roman numerals ending before 0x216F"},
        {U"ⓐ", U"Ⓐ", "circled letters starting past 0x24B6"},
        {U"ⓩ", U"Ⓩ", "circled letters ending before 0x24CF"},
        {U"ａ", U"Ａ", "full-width starting past 0xFF21"},
        {U"ｚ", U"Ｚ", "full-width ending before 0xFF3A"},
        {U"ÉCOLE", U"école", "ASCII letters left unfolded on the non-ASCII path"},
    };
    for (const CasePair& p : pairs) check(finds(p.field, p.query) && finds(p.query, p.field), p.what);
}

// Pairs Kotlin's ignoreCase keeps apart, so the folding must too.
void distinctLettersStayDistinct() {
    // Kills: the 0xD7 exclusion removed (the multiplication sign would fold onto the division sign).
    check(!finds(U"÷", U"×"), "multiplication sign is not division sign");
    // Kills: the Extended Additional run extended into 0x1E96..0x1E9D (h with line below onto t with diaeresis).
    check(!finds(U"ẗ", U"ẖ") && !finds(U"ẖ", U"ẗ"), "lower-only letters past 0x1E95 stay");
    // Kills: a 3-byte UTF-8 sequence decoded as 2 bytes (U+4E6C would land on 0x139 and fold onto U+4EAC's 0x13A).
    check(!finds(U"京", U"乬"), "two CJK ideographs that share bytes do not match");
    check(finds(U"東京事変", U"京事"), "a CJK substring matches");
    // Kills: a one-to-many fold (sharp s to "ss"): Kotlin compares one char against one char.
    check(!finds(U"Straße", U"STRASSE") && !finds(U"Strasse", U"straße"), "sharp s is not ss");
    check(finds(U"STRAẞE", U"straße"), "capital sharp s is sharp s");
    // Kills: an accent-stripping fold; Kotlin's ignoreCase keeps accents.
    check(!finds(U"Café", U"cafe"), "e is not e-acute");
}

// A row with fewer fields than the row before it is matched on its own fields only.
void aRowIsMatchedOnlyOnItsOwnFields() {
    // Kills: the term loop bounded by the reused fold buffer's size instead of the row's field count.
    const std::vector<Row> rows = {
        {nullptr, false, {"Alpha", "Beta"}},
        {nullptr, false, {"Gamma"}},
        {nullptr, false, {}},
    };
    check(run(rows, "beta") == std::vector<int>({0}), "a short row does not inherit the long row's field");
    check(run(rows, "gamma") == std::vector<int>({1}), "a short row still matches its own field");
}

void dedupeAndCapHandWorked() {
    // Kills: a key's slot taken by a row that does not match (the first x does not match, the second does).
    const std::vector<Row> late = {{"x", false, {"Other"}}, {"x", false, {"Song"}}, {"y", false, {"Song"}}};
    check(run(late, "song") == std::vector<int>({1, 2}), "a key's first matching row holds its place");
    // Kills: keyless or keyed rows ignoring the cap (`<` to `<=`, `>=` to `>`).
    const std::vector<Row> mixed = {
        {nullptr, false, {"Mix"}},
        {"k", false, {"Mix"}},
        {nullptr, false, {"Mix"}},
        {"k", true, {"Mix"}},
    };
    // By hand, as Kotlin did it: dedupe gives [0, 3 (in k's place), 2], then take(n).
    check(run(mixed, "mix", 2) == std::vector<int>({0, 3}), "dedupe then cap over keyed and keyless rows");
    check(run(mixed, "mix", 1) == std::vector<int>({0}), "a cap of one");
    check(run(mixed, "mix", 3) == std::vector<int>({0, 3, 2}), "a cap at the list's length leaves it whole");
}

// --- Parity with the deleted Kotlin SearchMatcher.filterTracks, over seeded random libraries ---------------
// The oracle is the old Kotlin, line for line: every term in some field ignoring case (ASCII only here, where
// Kotlin's ignoreCase is plain ASCII lower case), a LinkedHashMap by key that a preferred match replaces in
// place, then take(max). It shares nothing with filterRows' slot table and early cap.

struct Lcg {
    std::uint32_t state;
    std::uint32_t next() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    std::size_t below(std::size_t n) { return static_cast<std::size_t>(next() % static_cast<std::uint32_t>(n)); }
};

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// query.trim().split(Regex("\\s+")).filter { it.isNotBlank() }, for ASCII queries.
std::vector<std::string> kotlinTerms(std::string_view query) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : query) {
        if (c == ' ' || (c >= '\t' && c <= '\r')) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::vector<int> kotlinFilterTracks(const std::vector<Row>& rows, std::string_view query, std::size_t max) {
    const std::vector<std::string> terms = kotlinTerms(query);
    if (terms.empty()) return {};
    std::vector<std::string> keys;  // the LinkedHashMap's insertion order
    std::vector<int> values;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row& r = rows[i];
        bool all = true;
        for (const std::string& t : terms) {
            bool any = false;
            for (std::string_view f : r.fields) any = any || lowerAscii(f).find(lowerAscii(t)) != std::string::npos;
            all = all && any;
        }
        if (!all) continue;
        const std::string key = r.key ? std::string("k:") + r.key : "row:" + std::to_string(i);
        std::size_t at = 0;
        while (at < keys.size() && keys[at] != key) ++at;
        if (at == keys.size()) {
            keys.push_back(key);
            values.push_back(static_cast<int>(i));
        } else if (!rows[static_cast<std::size_t>(values[at])].preferred && r.preferred) {
            values[at] = static_cast<int>(i);
        }
    }
    if (values.size() > max) values.resize(max);
    return values;
}

const char* const kWords[] = {"ab", "AB", "Ba", "cd", "abc", "x", "Cab", "dX"};
const char* const kTerms[] = {"a", "B", "ab", "c", "CAB", "x", "d"};
const char* const kGaps[] = {" ", "\t", "  ", "\n ", " \r", "\v"};
const char* const kKeys[] = {"k0", "k1", "k2", nullptr};

// fieldsPerRow 0: each row gets 0..3 fields.
std::vector<Row> randomRows(Lcg& rng, std::size_t fieldsPerRow) {
    std::vector<Row> rows(rng.below(13));
    for (Row& r : rows) {
        r.key = kKeys[rng.below(4)];
        r.preferred = rng.below(3) == 0;
        const std::size_t n = fieldsPerRow ? fieldsPerRow : rng.below(4);
        for (std::size_t f = 0; f < n; ++f) r.fields.emplace_back(kWords[rng.below(8)]);
    }
    return rows;
}

std::string randomQuery(Lcg& rng) {
    std::string q = rng.below(2) ? kGaps[rng.below(6)] : "";
    const std::size_t n = rng.below(4);
    for (std::size_t t = 0; t < n; ++t) {
        if (t > 0) q += kGaps[rng.below(6)];
        q += kTerms[rng.below(7)];
    }
    if (rng.below(2)) q += kGaps[rng.below(6)];
    return q;
}

// Kills every dedupe and cap mutant at once (the cap's `<` to `<=` or `>=` to `>`, a non-preferred or
// non-matching row taking a slot, a preferred row displacing a preferred one, the holder read by slot index
// instead of row index, the AND of terms turned into an OR, the substring test negated) and pins the order:
// each kept row at its key's first match.
void filterRowsListsWhatKotlinFilterTracksListed() {
    Lcg rng{20260507u};
    int mismatches = 0;
    for (int iter = 0; iter < 4000; ++iter) {
        const std::vector<Row> rows = randomRows(rng, 0);
        const std::string query = randomQuery(rng);
        const std::size_t max = rng.below(9);
        if (run(rows, query, max) != kotlinFilterTracks(rows, query, max)) ++mismatches;
    }
    check(mismatches == 0, "filterRows lists what Kotlin's filterTracks listed, in the same order");
}

// The same oracle through the C ABI, so a cell-layout mutant (the fields' span starting at the key, the row
// width without the key, the preferred byte read for the wrong row, `out` written short) cannot hide.
void cAbiListsWhatKotlinFilterTracksListed() {
    Lcg rng{341u};
    int mismatches = 0;
    for (int iter = 0; iter < 2000; ++iter) {
        const std::vector<Row> rows = randomRows(rng, 2);
        const std::string query = randomQuery(rng);
        const std::size_t max = 1 + rng.below(8);
        std::vector<std::string> owned;  // NUL-terminated copies of the fields, never reallocated
        owned.reserve(rows.size() * 2);
        std::vector<const char*> cells;
        std::vector<unsigned char> preferred;
        for (const Row& r : rows) {
            cells.push_back(r.key);
            for (std::string_view f : r.fields) {
                owned.emplace_back(f);
                cells.push_back(owned.back().c_str());
            }
            preferred.push_back(r.preferred ? 1 : 0);
        }
        std::vector<int> out(max, -1);
        const int n = geode_library_filter(cells.data(), static_cast<int>(rows.size()), 2, preferred.data(),
                                           query.c_str(), out.data(), static_cast<int>(max));
        out.resize(n < 0 ? 0 : static_cast<std::size_t>(n));
        if (out != kotlinFilterTracks(rows, query, max)) ++mismatches;
    }
    check(mismatches == 0, "geode_library_filter lists what Kotlin's filterTracks listed");
    const char* cells[] = {"zzkey", "Song", "Artist"};
    int out[1] = {-1};
    // Kills: the fields' span starting at the key cell.
    check(geode_library_filter(cells, 1, 2, nullptr, "zzkey", out, 1) == 0, "the key is not a searched field");
    // Kills: the span one field short.
    check(geode_library_filter(cells, 1, 2, nullptr, "artist", out, 1) == 1 && out[0] == 0,
          "the last field is searched");
}

// --- Known parity gaps: these SHOULD FAIL on #507 as written ------------------------------------------------
// Kotlin's contains(ignoreCase = true) folds every BMP letter that has a simple case mapping; fold() covers
// only its listed ranges, so Latin Extended-B letters outside them (the Azerbaijani schwa, the open o of Akan
// and Ewe) stop matching their other case. Kotlin's Char.isWhitespace leaves out U+0085 (NEL: a control
// outside U+0009..U+000D and U+001C..U+001F), so trim() keeps it and the term is not blank.
void kotlinParityGaps() {
    check(finds(U"Əli", U"əli"), "PARITY GAP: capital schwa matches small schwa, as in Kotlin");
    check(finds(U"Ɔ", U"ɔ"), "PARITY GAP: capital open o matches small open o, as in Kotlin");
    check(termsOf(U"\x85" U"ab") == std::vector<std::string>({utf8(U"\x85" U"ab")}),
          "PARITY GAP: U+0085 is not Kotlin whitespace, so trim() keeps it");
}

}  // namespace

int main() {
    termsAreTrimmedAndSplit();
    everyTermInSomeField();
    caseIsIgnoredLikeKotlin();
    dedupeKeepsThePreferredRow();
    capComesAfterDedupe();
    cAbiMarshalsRows();
    queryEdgesTrimLikeKotlinAndSplitLikeJavaRegex();
    caseFoldingMatchesKotlinAtEveryBoundary();
    distinctLettersStayDistinct();
    aRowIsMatchedOnlyOnItsOwnFields();
    dedupeAndCapHandWorked();
    filterRowsListsWhatKotlinFilterTracksListed();
    cAbiListsWhatKotlinFilterTracksListed();
    kotlinParityGaps();
    if (failures == 0) std::printf("library_filter_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
