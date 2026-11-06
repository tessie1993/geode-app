#pragma once
#include <cstddef>
#include <optional>
#include <span>
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

// The rows of `rows` a voice request plays, as indices in library order.
//
// The focus extras are tried first, because they are the assistant's own parse of the sentence
// ("Radiohead" as an artist, not a word somewhere in a title): an artist focus keeps the tracks by that
// artist, an album focus that album (and artist when given), a song focus that title. When the focus
// is missing, unknown, or finds nothing, the free query is matched instead, every word against title,
// artist or album, like the Library screen's search. A request with nothing in it at all ("play music")
// plays the whole library; a query that matches nothing returns nothing, so the caller can refuse it.
std::vector<int> searchTracks(const std::vector<SearchRow>& rows, const SearchRequest& request);

// True when `term` is in any of `fields`, folded (`foldText`), allowing a typo: none for words of three
// letters or less, one up to five, two beyond (the Library screen's rule, `LibraryBrowse.matches`).
bool termMatches(std::string_view term, const std::vector<std::string_view>& fields);

// --- Library text filter (#341, #395): the search sheet's, the Library screen's and Android Auto's search ----

// Search text as both sides of a match see it: lower case (one letter to one letter for Latin, Greek, Cyrillic,
// Armenian and full-width, never the phone's locale, so "INXS" stays "inxs" on a Turkish phone), accents gone
// (what NFD splits off and #395's Kotlin fold dropped: "Beyoncé" is "beyonce", "Đường" is "duong", "ё" is
// "е", "が" is "か"), and the letters NFD does not split spelled out the way a plain keyboard types them:
// ß → ss, æ → ae, œ → oe, ø → o, ł → l, đ and ð → d, þ → th. UTF-8 in, UTF-8 out. No ICU: tables below.
std::string foldText(std::string_view text);

// One row the search filters: a track, a playlist or a preset. Views into the caller's strings, UTF-8.
struct FilterRow {
    // What makes two rows the same thing (a track's uri). nullopt: the row is never merged with another.
    std::optional<std::string_view> key;
    // Among rows sharing a key, a preferred one (a device track over its library copy) is the one kept.
    bool preferred = false;
    // In rank order, since the field a hit lands in is its tier: tracks pass title, artist, album, then the
    // rest (genre, folder, filename); a playlist or a preset passes its name alone.
    std::span<const std::string_view> fields;
};

struct FilterOptions {
    // After every exact hit, the rows a term reaches only with a typo (none for words of three letters or
    // less, one edit up to five, two beyond, against a field's words): the Library screen's rule.
    bool typos = false;
    // A blank query keeps every row (deduped) instead of none: the Library screen's empty box is the library.
    bool blankKeepsAll = false;
};

// The rows of `rows` whose fields hold every term of `query`, as indices, ranked, at most `max` of them.
//
// The query is trimmed, split on whitespace and folded (`foldText`); every term must be inside some folded
// field. A row's tier is the furthest field its terms needed, each term taking the first field that holds it:
// a title hit beats an artist hit beats an album hit beats the rest, and "nirvana teen spirit" split across
// title and artist ranks as an artist hit. Typo hits (`FilterOptions::typos`) rank after every exact hit, by
// the same rule, at tier `fields.size()` + their field. Rows sharing a key collapse into one, at the place the
// first matching one held, keeping the first preferred match if there is one, else the first match, with the
// better of the two tiers. The rows are then sorted by tier, stably (the caller's order holds inside a tier),
// and only then cut to `max`. A blank query matches nothing unless `blankKeepsAll`.
std::vector<int> filterRows(std::span<const FilterRow> rows, std::string_view query, std::size_t max,
                            FilterOptions options = {});

// One row's tier for `query` as `filterRows` ranks it: 0 title, 1 artist, 2 album, 3+ the rest, then typo hits
// at `fields.size()` + their field; -1 when some term is in no field. 0 for a blank query.
int filterTier(std::span<const std::string_view> fields, std::string_view query, FilterOptions options = {});

// The terms `filterRows` looks for, folded and re-encoded as UTF-8: for tests and callers that show them.
std::vector<std::string> filterTerms(std::string_view query);

}  // namespace geode::library
