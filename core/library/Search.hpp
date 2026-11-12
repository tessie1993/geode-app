#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace geode::library {

// The tags a library search reads for one track. Views into the caller's strings, UTF-8.
struct SearchRow {
    std::string_view title;
    std::string_view artist;
    std::string_view album;
};

// A "play X" request as the media session hears it: the spoken query, plus the structured extras an
// assistant may add (MediaStore.EXTRA_MEDIA_FOCUS and EXTRA_MEDIA_ARTIST / _ALBUM / _TITLE). Any of
// them may be empty.
struct SearchRequest {
    std::string_view query;
    std::string_view focus;
    std::string_view artist;
    std::string_view album;
    std::string_view title;
};

// How the whole library is listed when nothing is searched.
enum class Sort { Title = 0, Artist = 1, Album = 2, Duration = 3, Added = 4 };

// What one query finds, best first, as row numbers of the library the index was built from. An album
// or artist is named by one of its rows (the first in the library).
struct SearchHits {
    std::vector<int> tracks;
    std::vector<int> albums;
    std::vector<int> artists;
};

// The app's one library match (the Library screen, Android Auto's search and voice "play X").
//
// Text is folded once: lower case, accents gone, apostrophes dropped, other punctuation a word break
// ("AC/DC" is also the word "acdc", "R.E.M." also "rem"). Every word of the query must land in the
// title, artist or album; a lone "the" may be missing. A word lands as a whole word, as the start of
// one, or (three letters and up) inside one. Best first: words that start a word before words found
// inside one; then by the field the match needed (title, then artist, then album); then a field that
// starts with the query; then whole words; then title order. A misspelling is only tried when
// nothing matches as typed: one edit from four letters, two from eight, first letter kept, against a
// word or the start of a longer one.
class SearchIndex {
public:
    // `durationsMs` and `addedSec` may be shorter than `rows`; a missing value is 0.
    SearchIndex(const std::vector<SearchRow>& rows, const std::vector<long long>& durationsMs,
                const std::vector<long long>& addedSec);

    int size() const { return static_cast<int>(entries_.size()); }

    // Every row in `sort` order. Text orders compare folded, so "Élan" sits with the Es.
    std::vector<int> order(Sort sort) const;

    // A blank query finds nothing (the caller lists the library with `order`). Artists are those
    // whose name holds every word, albums those whose name and artist do with a word in the name,
    // at most `kMaxArtistHits` and `kMaxAlbumHits`.
    SearchHits search(std::string_view query) const;

    static constexpr int kMaxArtistHits = 5;
    static constexpr int kMaxAlbumHits = 8;

    struct Field {
        std::u32string key;                 // folded words joined by one space, for order and "starts with"
        std::vector<std::u32string> words;  // the words, plus a punctuated word run together
    };
    struct Entry {
        Field title;
        Field artist;
        Field album;
    };

private:
    std::vector<Entry> entries_;
    std::vector<long long> durationsMs_;
    std::vector<long long> addedSec_;
    std::vector<int> titleRank_;    // each row's place in title order: the tie-break
    std::vector<int> albumOf_;      // each row's album group
    std::vector<int> artistOf_;     // each row's artist group
    std::vector<int> albumFirst_;   // each album group's first row
    std::vector<int> artistFirst_;  // each artist group's first row
};

// The rows of `rows` a voice request plays, best first (ties in library order).
//
// The focus extras are tried first, because they are the assistant's own parse of the sentence
// ("Radiohead" as an artist, not a word somewhere in a title): an artist focus keeps the tracks by that
// artist, an album focus that album (and artist when given), a song focus that title. When the focus
// is missing, unknown, or finds nothing, the free query is matched instead, as `SearchIndex::search`
// matches tracks. A request with nothing in it at all ("play music") plays the whole library; a query
// that matches nothing returns nothing, so the caller can refuse it.
std::vector<int> searchTracks(const std::vector<SearchRow>& rows, const SearchRequest& request);

// True when `term` lands in any of `fields` as typed or, failing that, as a misspelling.
bool termMatches(std::string_view term, const std::vector<std::string_view>& fields);

// One track of the global search sheet: its tags, one more field searched after the album (a device
// track's folder, a library track's genre), the key every copy of one song shares (its URI), and
// whether this copy is the one to show when copies meet (the device's over the app library's).
struct SheetTrack {
    SearchRow tags;
    std::string_view extra;
    std::string_view key;
    bool preferred;
};

// What the global search sheet shows for one query: rows of the tracks, playlists and presets it was
// handed, best first.
struct SheetHits {
    std::vector<int> tracks;
    std::vector<int> playlists;
    std::vector<int> presets;
};

inline constexpr int kMaxSheetTracks = 30;

// The global search sheet (tracks, playlists, presets), matched and ranked as `SearchIndex::search`
// ranks tracks, with a track's extra field as a fourth after the album. The copies of one song (one
// key; an empty key is never a copy) are one hit: ranked by its best copy, placed among equals by its
// first, shown as its preferred copy. At most `kMaxSheetTracks` tracks. Playlists and presets match by
// name, ties in the caller's order. A misspelling is only tried when nothing matches as typed; a blank
// query finds nothing.
SheetHits searchSheet(const std::vector<SheetTrack>& tracks, const std::vector<std::string_view>& playlists,
                      const std::vector<std::string_view>& presets, std::string_view query);

}  // namespace geode::library
