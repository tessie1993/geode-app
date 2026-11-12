// Host-side checks for the library search (core/library/Search): the Library screen's index
// (geode_library_index_*), Android Auto's search and voice "play X" (geode_library_search). Built and run by CI
// ("Native host tests") with the system compiler; no device.
//
// What is pinned: one match for every caller. Every word of a query must land in title, artist or album, in
// any order; accents, case, apostrophes and punctuation never matter; a word start ranks above the inside of
// a word, a title match above an artist's above an album's; a misspelling is only tried when nothing matches
// as typed; a query finds its artists and albums beside its tracks. For voice (#382): MediaStore's focus
// extras narrow by field first, a blank request plays the whole library, and a query that matches nothing
// finds nothing, so the session refuses it instead of handing ExoPlayer an item without a URI.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "library/Search.hpp"

using geode::library::SearchHits;
using geode::library::SearchIndex;
using geode::library::SearchRequest;
using geode::library::SearchRow;
using geode::library::searchSheet;
using geode::library::searchTracks;
using geode::library::SheetHits;
using geode::library::SheetTrack;
using geode::library::Sort;
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
    check(searchTracks(kLibrary, ask("ca plane")) == std::vector<int>({5}), "accents fold for voice too");
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
    check(searchTracks(kLibrary, unknown) == std::vector<int>({4}),
          "a focus that finds nothing falls back to the query");
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

// The Library screen's index. Rows are in no order; title order is 0 5 7 2 9 8 4 10 1 3 6.
const std::vector<SearchRow> kShelf = {
    {"Another Brick in the Wall", "Pink Floyd", "The Wall"},       // 0
    {"Walk", "Pantera", "Vulgar Display of Power"},                // 1
    {"Call Me", "Blondie", "Best of Blondie"},                     // 2
    {"Wall of Sound", "Naturally 7", "Wall of Sound"},             // 3
    {"Heart of Glass", "Blondie", "Parallel Lines"},               // 4
    {"Art for Art's Sake", "10cc", "How Dare You!"},               // 5
    {"Wonderwall", "Oasis", "(What's the Story) Morning Glory?"},  // 6
    {"Back in Black", "AC/DC", "Back in Black"},                   // 7
    {"Halo", "Beyoncé", "I Am… Sasha Fierce"},                     // 8
    {"Flood", "Tool", "Undertow"},                                 // 9
    {"Money", "Pink Floyd", "The Dark Side of the Moon"},          // 10
};

SearchIndex shelf() { return SearchIndex(kShelf, {}, {}); }

std::vector<int> tracksFor(const char* query) { return shelf().search(query).tracks; }

void everyWordMustLandInAnyOrder() {
    check(tracksFor("pink floyd wall") == std::vector<int>({0}), "words spread over artist, album and title");
    check(tracksFor("wall floyd pink") == std::vector<int>({0}), "word order does not matter");
    check(tracksFor("pink floyd") == std::vector<int>({0, 10}), "an artist's tracks, in title order");
    check(tracksFor("pink zeppelin").empty(), "a word that lands nowhere finds nothing");
    check(tracksFor("the pantera") == std::vector<int>({1}), "a \"the\" the tags lack is forgiven");
    check(tracksFor("the").size() == 3, "but \"the\" alone is a word like any other");
}

void wordStartsRankAboveInsideAWord() {
    // "Wall of Sound" starts with it; "Another Brick in the Wall" holds the word; "Wonderwall" only inside.
    check(tracksFor("wall") == std::vector<int>({3, 0, 6}), "starts with, then a whole word, then inside a word");
    check(tracksFor("art") == std::vector<int>({5, 4}), "\"art\" is Art for Art's Sake before Heart of Glass");
    check(tracksFor("wal") == std::vector<int>({1, 3, 0, 6}), "a half-typed word finds what it starts");
    check(tracksFor("blondie") == std::vector<int>({2, 4}), "an artist match keeps title order");
}

void titleRanksAboveArtistAboveAlbum() {
    const std::vector<SearchRow> rows = {
        {"Aaa", "Someone", "Love Songs"}, {"Bbb", "Love Inc.", "Something"}, {"Zzz Love", "Someone", "Something"}};
    check(SearchIndex(rows, {}, {}).search("love").tracks == std::vector<int>({2, 1, 0}),
          "title, then artist, then album, whatever the title order");
}

void shortQueriesOnlyStartWords() {
    check(tracksFor("a") == std::vector<int>({0, 5, 7, 8}), "one letter starts a word: Another, Art, AC/DC, I Am");
    check(tracksFor("al").empty(), "two letters never match inside a word (Wall, Halo)");
    check(tracksFor("10") == std::vector<int>({5}), "numbers are words: 10cc");
}

void accentsCaseAndPunctuationNeverMatter() {
    check(tracksFor("beyonce") == std::vector<int>({8}), "a plain query finds an accented tag");
    check(tracksFor("BEYONCÉ") == std::vector<int>({8}), "and an accented, shouted one does too");
    check(tracksFor("acdc") == std::vector<int>({7}), "AC/DC without the slash");
    check(tracksFor("ac/dc") == std::vector<int>({7}), "and with it");
    check(tracksFor("ac dc") == std::vector<int>({7}), "and as two words");
    check(tracksFor("arts sake") == std::vector<int>({5}), "an apostrophe is not a letter");
    check(tracksFor("art's sake") == std::vector<int>({5}), "typed or not");
    check(tracksFor("whats the story") == std::vector<int>({6}), "brackets and question marks are word breaks");
    check(termMatches("rem", {"R.E.M."}) && termMatches("r.e.m.", {"R.E.M."}), "R.E.M. with or without dots");
    check(termMatches("strasse", {"Straße"}) && termMatches("bjork", {"Björk"}), "letters as a plain keyboard types them");
    check(termMatches("sigur ros", {"Sigur Rós"}), "word by word");
    check(termMatches("zemfira", {"Zemfira"}) && termMatches("ЗЕМФИРА", {"Земфира"}), "Cyrillic case folds");
}

void misspellingsOnlyWhenNothingMatches() {
    // The old rule let "wall" also find Walk and Call Me, and "floyd" find Flood.
    check(tracksFor("wall") == std::vector<int>({3, 0, 6}), "a word that matches as typed brings no near misses");
    check(tracksFor("floyd") == std::vector<int>({0, 10}), "\"floyd\" is not Flood");
    check(tracksFor("blondy") == std::vector<int>({2, 4}), "one edit in a half-typed word, when nothing else matches");
    check(tracksFor("blondia") == std::vector<int>({2, 4}), "or in a whole one");
    check(tracksFor("pink floid") == std::vector<int>({0, 10}), "a misspelt word beside a good one");
    check(tracksFor("wonderwal") == std::vector<int>({6}), "a word start is not a misspelling");
    check(tracksFor("hart").empty() == false && tracksFor("cal").empty() == false, "short words still start words");
    check(tracksFor("tol").empty(), "three letters have no spare edits");
    check(tracksFor("londie").empty() == false, "inside a word still counts");
    check(tracksFor("xlondie").empty(), "a misspelling keeps its first letter");
}

void aQueryFindsItsArtistsAndAlbums() {
    const SearchIndex index = shelf();
    SearchHits hits = index.search("pink floyd");
    check(hits.artists == std::vector<int>({0}), "the artist, as its first row");
    check(hits.albums.empty(), "an artist's name alone is not every album they made");
    hits = index.search("wall");
    check(hits.albums == std::vector<int>({3, 0}), "albums by name: one that starts with the word first");
    check(hits.artists.empty(), "no artist is called that");
    hits = index.search("pink floyd wall");
    check(hits.albums == std::vector<int>({0}), "an album named with its artist");
    hits = index.search("blondie");
    check(hits.artists == std::vector<int>({2}) && hits.albums == std::vector<int>({2}),
          "one row per artist, however many tracks; Best of Blondie is an album by name");
    check(index.search("").tracks.empty() && index.search("  ").artists.empty(), "a blank query finds nothing");
    check(index.search("?!").tracks.empty(), "nor does punctuation alone");

    std::vector<SearchRow> many;
    std::vector<std::string> names;
    for (int i = 0; i < 20; ++i) names.push_back("Love Band " + std::to_string(i));
    for (const auto& name : names) many.push_back({"Song", name, name});
    hits = SearchIndex(many, {}, {}).search("love");
    check(static_cast<int>(hits.artists.size()) == SearchIndex::kMaxArtistHits, "artists are capped");
    check(static_cast<int>(hits.albums.size()) == SearchIndex::kMaxAlbumHits, "albums are capped");
    check(hits.tracks.size() == 20, "tracks are not");
}

void orderListsTheLibrary() {
    const std::vector<SearchRow> rows = {
        {"Élan", "Zed", "B"}, {"Dawn", "Abe", "C"}, {"fall", "Abe", "A"}, {"Dawn", "Moe", "A"}};
    const SearchIndex index(rows, {300, 100, 200}, {1, 3, 2, 3});
    check(index.order(Sort::Title) == std::vector<int>({1, 3, 0, 2}), "folded: Élan with the Es; ties in library order");
    check(index.order(Sort::Artist) == std::vector<int>({1, 2, 3, 0}), "by artist, then title");
    check(index.order(Sort::Album) == std::vector<int>({3, 2, 0, 1}), "by album, then title");
    check(index.order(Sort::Duration) == std::vector<int>({3, 1, 2, 0}), "shortest first; a missing length is 0");
    check(index.order(Sort::Added) == std::vector<int>({1, 3, 2, 0}), "newest first");
}

void voiceAndTheLibraryAgree() {
    for (const char* query : {"wall", "pink floyd wall", "beyonce", "acdc", "blondy", "art", "the pantera", "al"}) {
        std::vector<int> voice = searchTracks(kShelf, ask(query));
        std::vector<int> library = tracksFor(query);
        std::sort(voice.begin(), voice.end());
        std::sort(library.begin(), library.end());
        check(voice == library, query);
    }
    check(searchTracks(kShelf, ask("wall")).front() == 3, "voice plays the best match first");
}

void indexCAbi() {
    const char* rows[] = {"Teardrop", "Massive Attack", "Mezzanine", "Airbag", "Radiohead", "OK Computer"};
    const long long lengths[] = {200, 100};
    geode_library_index* index = geode_library_index_create(rows, lengths, nullptr, 2);
    int out[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    check(geode_library_index_order(index, 0, out, 8) == 2 && out[0] == 1 && out[1] == 0, "title order");
    check(geode_library_index_order(index, 3, out, 1) == 2 && out[0] == 1, "duration order, a short buffer");
    check(geode_library_index_order(index, 99, out, 8) == 2 && out[0] == 1, "an unknown order is title order");
    check(geode_library_index_search(index, "radiohead", out, 8) == 5, "three counts, a track and an artist");
    check(out[0] == 1 && out[1] == 0 && out[2] == 1 && out[3] == 1 && out[4] == 1, "packed: counts, then rows");
    check(geode_library_index_search(index, "", out, 8) == 3 && out[0] == 0, "a blank query is three zero counts");
    check(geode_library_index_search(index, nullptr, nullptr, 0) == 3, "no buffer, no crash");
    geode_library_index_destroy(index);
    geode_library_index_destroy(nullptr);
    check(geode_library_index_create(nullptr, nullptr, nullptr, 3) == nullptr, "rows are required");
    check(geode_library_index_order(nullptr, 0, out, 8) == 0 && geode_library_index_search(nullptr, "x", out, 8) == 0,
          "no index, no rows");
    geode_library_index* empty = geode_library_index_create(nullptr, nullptr, nullptr, 0);
    check(empty && geode_library_index_order(empty, 0, out, 8) == 0, "an empty library is an index too");
    geode_library_index_destroy(empty);
}

// The global search sheet (searchSheet, geode_search_sheet): tracks from the device and from the app's
// library, playlists and presets, for one query.
SheetTrack sheetTrack(const char* title, const char* artist, const char* album, const char* extra, const char* key,
                      bool preferred = false) {
    return SheetTrack{SearchRow{title, artist, album}, extra, key, preferred};
}

const std::vector<std::string_view> kNoNames;

void sheetRanksTitleArtistAlbumThenTheExtraField() {
    const std::vector<SheetTrack> tracks = {
        sheetTrack("Nothing", "Nirvana", "Teen Spirit Live", "", "u0"),
        sheetTrack("Smells Like Teen Spirit", "Nirvana", "Nevermind", "", "u1"),
        sheetTrack("Teen", "Spirit Band", "A", "", "u2"),
        sheetTrack("Song", "Band", "Album", "/music/teen spirit", "u3"),
    };
    const SheetHits hits = searchSheet(tracks, kNoNames, kNoNames, "teen spirit");
    check(hits.tracks == std::vector<int>({1, 2, 0, 3}),
          "a title holding every word, then one that needs the artist, the album, the folder or genre");
    check(searchSheet(tracks, kNoNames, kNoNames, "   ").tracks.empty(), "a blank query finds nothing");
}

void sheetShowsOneHitPerSong() {
    const std::vector<SheetTrack> tracks = {
        sheetTrack("Halo", "Beyoncé", "I Am", "Pop", "content://1"),
        sheetTrack("Halo", "Beyonce", "I Am", "/music", "content://1", true),
        sheetTrack("Halo Remix", "DJ", "B", "", "content://2"),
    };
    check(searchSheet(tracks, kNoNames, kNoNames, "halo").tracks == std::vector<int>({1, 2}),
          "two copies of a song are one hit, shown as the device's copy, placed by the first");

    const std::vector<SheetTrack> later = {
        sheetTrack("X", "Halo Band", "Y", "", "k"),
        sheetTrack("Other", "Z", "W", "", "j"),
        sheetTrack("Halo", "A", "B", "", "m"),
        sheetTrack("Halo", "C", "D", "", "k", true),
    };
    check(searchSheet(later, kNoNames, kNoNames, "halo").tracks == std::vector<int>({3, 2}),
          "a song ranks by its best copy");

    const std::vector<SheetTrack> keyless = {sheetTrack("Halo", "A", "B", "", ""), sheetTrack("Halo", "C", "D", "", "")};
    check(searchSheet(keyless, kNoNames, kNoNames, "halo").tracks == std::vector<int>({0, 1}),
          "tracks without a key are never copies");
}

void sheetKeepsThirtyTracks() {
    std::vector<std::string> titles;
    std::vector<std::string> keys;
    for (int i = 0; i < 40; ++i) {
        titles.push_back("Love " + std::to_string(i));
        keys.push_back("content://" + std::to_string(i));
    }
    std::vector<SheetTrack> tracks;
    for (size_t i = 0; i < titles.size(); ++i) tracks.push_back(sheetTrack(titles[i].c_str(), "A", "B", "", keys[i].c_str()));
    const SheetHits hits = searchSheet(tracks, kNoNames, kNoNames, "love");
    check(hits.tracks.size() == static_cast<size_t>(geode::library::kMaxSheetTracks) && hits.tracks.front() == 0 &&
              hits.tracks.back() == 29,
          "at most thirty tracks, ties in the caller's order");
}

void sheetFindsPlaylistsAndPresetsByName() {
    const std::vector<std::string_view> playlists = {"Road Trip", "Chill", "Trip Hop Mix"};
    const std::vector<std::string_view> presets = {"Deep Trip", "Ember"};
    const SheetHits hits = searchSheet({}, playlists, presets, "trip");
    check(hits.playlists == std::vector<int>({2, 0}), "a name that starts with the query first");
    check(hits.presets == std::vector<int>({0}), "presets by name");
    check(searchSheet({}, playlists, presets, "trip chill").playlists.empty(), "every word must land in the name");

    const std::vector<std::string_view> mixes = {"Radiohed Classics"};
    const std::vector<SheetTrack> tracks = {sheetTrack("Creep", "Radiohead", "Pablo Honey", "", "a")};
    const SheetHits typed = searchSheet(tracks, mixes, kNoNames, "radiohead");
    check(typed.tracks == std::vector<int>({0}) && typed.playlists.empty(),
          "a misspelling is not tried while something matches as typed");
    check(searchSheet({}, mixes, kNoNames, "radiohead").playlists == std::vector<int>({0}),
          "and is tried when nothing does");
}

void sheetFoldsLettersAsAPlainKeyboardTypesThem() {
    const std::vector<SheetTrack> tracks = {
        sheetTrack("Straße", "Røisin", "Łódź", "", "a"),
        sheetTrack("Æther", "İstanbul", "INXS", "", "b"),
    };
    check(searchSheet(tracks, kNoNames, kNoNames, "strasse roisin lodz").tracks == std::vector<int>({0}),
          "ß, ø and Ł with its accent");
    check(searchSheet(tracks, kNoNames, kNoNames, "AETHER istanbul inxs").tracks == std::vector<int>({1}),
          "Æ, a dotted capital I, and upper case");
}

void sheetCAbi() {
    const char* tracks[] = {
        "Halo", "Beyoncé", "I Am", "Pop", "content://1",     //
        "Halo", "Beyonce", "I Am", "/music", "content://1",  //
        "Run", "Adele", "25", "", "content://3",             //
    };
    const unsigned char preferred[] = {0, 1, 0};
    const char* playlists[] = {"Halo nights"};
    const char* presets[] = {"Ember"};
    int out[16] = {};
    check(geode_search_sheet(tracks, preferred, 3, playlists, 1, presets, 1, "halo", out, 16) == 5,
          "three counts and two rows");
    check(out[0] == 1 && out[1] == 1 && out[2] == 0 && out[3] == 1 && out[4] == 0,
          "one track (the device's copy), one playlist, no preset");
    check(geode_search_sheet(tracks, nullptr, 3, playlists, 1, presets, 1, "halo", out, 16) == 5 && out[3] == 0,
          "no flags: the first copy");
    check(geode_search_sheet(tracks, preferred, 3, playlists, 1, presets, 1, "halo", out, 2) == 5,
          "the total even when the buffer is short");
    int zeros[3] = {7, 7, 7};
    check(geode_search_sheet(nullptr, nullptr, 3, nullptr, 0, nullptr, 0, "halo", zeros, 3) == 3 && zeros[0] == 0 &&
              zeros[1] == 0 && zeros[2] == 0,
          "a missing array with a count finds nothing");
    check(geode_search_sheet(tracks, preferred, 3, playlists, 1, presets, 1, nullptr, zeros, 3) == 3 && zeros[0] == 0,
          "no query finds nothing");
}

}  // namespace

int main() {
    spokenQueryFindsTheArtist();
    blankRequestPlaysTheLibrary();
    noMatchFindsNothing();
    typosWithinBudget();
    focusNarrowsByField();
    cAbiMarshalsRowsAndCounts();
    everyWordMustLandInAnyOrder();
    wordStartsRankAboveInsideAWord();
    titleRanksAboveArtistAboveAlbum();
    shortQueriesOnlyStartWords();
    accentsCaseAndPunctuationNeverMatter();
    misspellingsOnlyWhenNothingMatches();
    aQueryFindsItsArtistsAndAlbums();
    orderListsTheLibrary();
    voiceAndTheLibraryAgree();
    indexCAbi();
    sheetRanksTitleArtistAlbumThenTheExtraField();
    sheetShowsOneHitPerSong();
    sheetKeepsThirtyTracks();
    sheetFindsPlaylistsAndPresetsByName();
    sheetFoldsLettersAsAPlainKeyboardTypesThem();
    sheetCAbi();
    if (failures == 0) std::printf("library_search_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
