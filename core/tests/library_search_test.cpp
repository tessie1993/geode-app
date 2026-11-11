// Host-side checks for voice "play X" over the library (core/library/Search, geode_library_search). Built and
// run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no device.
//
// What is pinned (#382): a spoken query finds the tracks it names, every word against title, artist or album,
// with the Library screen's typo budget; MediaStore's focus extras narrow by field first; a blank request plays
// the whole library; a query that matches nothing finds nothing, so the session refuses it instead of handing
// ExoPlayer an item without a URI.
#include <cstdio>
#include <vector>

#include "api/geode_api.h"
#include "library/Search.hpp"

using geode::library::SearchRequest;
using geode::library::SearchRow;
using geode::library::searchTracks;
using geode::library::termMatches;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// Every field spelled out, so -Wextra's missing-field-initializers stays quiet.
SearchRequest ask(const char* query, const char* focus = "", const char* artist = "", const char* album = "",
                  const char* title = "") {
    return SearchRequest{query, focus, artist, album, title};
}

const std::vector<SearchRow> kLibrary = {
    {"Airbag", "Radiohead", "OK Computer"},           // 0
    {"Paranoid Android", "Radiohead", "OK Computer"},  // 1
    {"Creep", "Radiohead", "Pablo Honey"},             // 2
    {"Teardrop", "Massive Attack", "Mezzanine"},       // 3
    {"Radio Gaga", "Queen", "The Works"},              // 4
    {"Ça plane pour moi", "Plastic Bertrand", "AN1"},  // 5
};

void spokenQueryFindsTheArtist() {
    const auto hits = searchTracks(kLibrary, ask("Radiohead"));
    check(hits == std::vector<int>({0, 1, 2}), "\"Radiohead\" plays Radiohead's tracks, in library order");
    check(searchTracks(kLibrary, ask("radiohead creep")) == std::vector<int>({2}),
          "every word must match, so more words narrow");
    check(searchTracks(kLibrary, ask("OK Computer")) == std::vector<int>({0, 1}), "an album by its name");
}

void blankRequestPlaysTheLibrary() {
    check(searchTracks(kLibrary, ask("")).size() == kLibrary.size(), "\"play music\" plays everything");
    check(searchTracks(kLibrary, ask("   ")).size() == kLibrary.size(), "whitespace is blank too");
}

void noMatchFindsNothing() {
    check(searchTracks(kLibrary, ask("Beyonce")).empty(), "an unknown artist finds nothing");
    check(searchTracks({}, ask("Radiohead")).empty(), "an empty library finds nothing");
    check(searchTracks({}, ask("")).empty(), "an empty library plays nothing even for a blank request");
}

void typosWithinBudget() {
    check(searchTracks(kLibrary, ask("Radiohed")) == std::vector<int>({0, 1, 2}),
          "one typo in a long word still matches");
    check(!termMatches("cep", {"Creep"}), "a three-letter word has no spare edits");
    check(termMatches("MASSIVE", {"Massive Attack"}), "case is folded");
    check(termMatches("ÇA", {"Ça plane pour moi"}), "Latin-1 case is folded");
}

void focusNarrowsByField() {
    SearchRequest artist = ask("radio", "vnd.android.cursor.item/artist", "Radiohead");
    check(searchTracks(kLibrary, artist) == std::vector<int>({0, 1, 2}),
          "an artist focus keeps that artist, not Radio Gaga");
    SearchRequest album = ask("ok computer", "vnd.android.cursor.item/album", "Radiohead", "OK Computer");
    check(searchTracks(kLibrary, album) == std::vector<int>({0, 1}), "an album focus keeps that album");
    SearchRequest song = ask("teardrop", "vnd.android.cursor.item/audio", "", "", "Teardrop");
    check(searchTracks(kLibrary, song) == std::vector<int>({3}), "a song focus keeps that title");
    SearchRequest unknown = ask("Queen", "vnd.android.cursor.item/artist", "Nobody At All");
    check(searchTracks(kLibrary, unknown) == std::vector<int>({4}), "a focus that finds nothing falls back to the query");
    SearchRequest extrasOnly = ask("", "vnd.android.cursor.item/artist", "Nobody At All");
    check(searchTracks(kLibrary, extrasOnly).empty(), "extras that match nothing never play the whole library");
}

void cAbiMarshalsRowsAndCounts() {
    const char* rows[] = {"Airbag", "Radiohead", "OK Computer", "Teardrop", "Massive Attack", "Mezzanine"};
    int out[1] = {-1};
    check(geode_library_search(rows, 2, "", nullptr, nullptr, nullptr, nullptr, out, 1) == 2,
          "the count is every match even when the buffer is short");
    check(out[0] == 0, "and the buffer holds the first");
    check(geode_library_search(rows, 2, "mezzanine", nullptr, nullptr, nullptr, nullptr, out, 1) == 1 && out[0] == 1,
          "a query reaches the album of the second row");
    check(geode_library_search(nullptr, 0, "x", nullptr, nullptr, nullptr, nullptr, nullptr, 0) == 0,
          "no library, no match, no crash");
}

}  // namespace

int main() {
    spokenQueryFindsTheArtist();
    blankRequestPlaysTheLibrary();
    noMatchFindsNothing();
    typosWithinBudget();
    focusNarrowsByField();
    cAbiMarshalsRowsAndCounts();
    if (failures == 0) std::printf("library_search_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
