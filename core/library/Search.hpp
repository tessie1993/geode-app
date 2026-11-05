#pragma once
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

// The rows of `rows` a voice request plays, as indices in library order.
//
// The focus extras are tried first, because they are the assistant's own parse of the sentence
// ("Radiohead" as an artist, not a word somewhere in a title): an artist focus keeps the tracks by that
// artist, an album focus that album (and artist when given), a song focus that title. When the focus
// is missing, unknown, or finds nothing, the free query is matched instead, every word against title,
// artist or album, like the Library screen's search. A request with nothing in it at all ("play music")
// plays the whole library; a query that matches nothing returns nothing, so the caller can refuse it.
std::vector<int> searchTracks(const std::vector<SearchRow>& rows, const SearchRequest& request);

// True when `term` is in any of `fields`, case-folded, allowing a typo: none for words of three
// letters or less, one up to five, two beyond (the Library screen's rule, `LibraryBrowse.matches`).
bool termMatches(std::string_view term, const std::vector<std::string_view>& fields);

}  // namespace geode::library
