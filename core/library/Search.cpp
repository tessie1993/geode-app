#include "library/Search.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <numeric>
#include <string>
#include <unordered_map>

namespace geode::library {
namespace {

// Decodes UTF-8 into code points; a malformed byte stands for itself so nothing is ever lost. JNI's
// modified UTF-8 (surrogate pairs as two 3-byte runs) decodes to two code points on both sides of a
// comparison, so it still compares equal to itself.
std::u32string decode(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const auto b = static_cast<unsigned char>(s[i]);
        size_t extra = 0;
        char32_t cp = b;
        if (b >= 0xF0 && b < 0xF8) {
            extra = 3;
            cp = b & 0x07;
        } else if (b >= 0xE0 && b < 0xF0) {
            extra = 2;
            cp = b & 0x0F;
        } else if (b >= 0xC0 && b < 0xE0) {
            extra = 1;
            cp = b & 0x1F;
        }
        bool ok = i + extra < s.size();
        for (size_t k = 1; ok && k <= extra; ++k) {
            const auto c = static_cast<unsigned char>(s[i + k]);
            ok = (c & 0xC0) == 0x80;
            cp = (cp << 6) | (c & 0x3F);
        }
        if (!ok) {
            // A stray continuation byte or a sequence cut short stands for itself.
            cp = b;
            extra = 0;
        }
        out.push_back(cp);
        i += 1 + extra;
    }
    return out;
}

using Field = SearchIndex::Field;
using Entry = SearchIndex::Entry;

struct FoldRow {
    char32_t from;
    char32_t to[2];
};

// Accented and cased letters beyond ASCII, each as the one or two letters it is searched as.
constexpr FoldRow kFold[] = {
#include "library/SearchFold.inc"
};

enum class Glyph { Letter, Drop, Space, Punct };

// Folds one code point into `out`, or says what kind of break it is. Apostrophes and accents vanish
// ("don't" is "dont"); other punctuation ends a word but not the run ("AC/DC").
Glyph foldInto(char32_t c, std::u32string& out) {
    if (c < 0x80) {
        if (c >= U'A' && c <= U'Z') {
            out.push_back(c + 32);
            return Glyph::Letter;
        }
        if ((c >= U'a' && c <= U'z') || (c >= U'0' && c <= U'9')) {
            out.push_back(c);
            return Glyph::Letter;
        }
        if (c == U'\'' || c == U'`') return Glyph::Drop;
        if (c == U' ' || (c >= 0x09 && c <= 0x0D)) return Glyph::Space;
        return Glyph::Punct;
    }
    if ((c >= 0x300 && c <= 0x36F) || c == 0x2018 || c == 0x2019 || c == 0x2BC || c == 0xB4) return Glyph::Drop;
    if (c == 0xA0 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x3000) return Glyph::Space;
    const bool latinPunct = c >= 0xA1 && c <= 0xBF && c != 0xAA && c != 0xB5 && c != 0xBA;
    if (latinPunct || c == 0xD7 || c == 0xF7 || (c >= 0x2010 && c <= 0x205E) || (c >= 0x3001 && c <= 0x303F)) {
        return Glyph::Punct;
    }
    const auto* row = std::lower_bound(std::begin(kFold), std::end(kFold), c,
                                       [](const FoldRow& r, char32_t v) { return r.from < v; });
    if (row != std::end(kFold) && row->from == c) {
        out.push_back(row->to[0]);
        if (row->to[1] != 0) out.push_back(row->to[1]);
    } else {
        out.push_back(c);
    }
    return Glyph::Letter;
}

Field foldField(std::string_view text) {
    Field field;
    std::u32string piece;
    std::vector<std::u32string> run;
    const auto endPiece = [&] {
        if (!piece.empty()) run.push_back(std::move(piece));
        piece.clear();
    };
    const auto endRun = [&] {
        endPiece();
        std::u32string joined;
        for (auto& word : run) {
            if (!field.key.empty()) field.key.push_back(U' ');
            field.key += word;
            joined += word;
            field.words.push_back(std::move(word));
        }
        // A punctuated run is also one word: "acdc" finds "AC/DC", "rem" finds "R.E.M.".
        if (run.size() > 1) field.words.push_back(std::move(joined));
        run.clear();
    };
    for (char32_t c : decode(text)) {
        switch (foldInto(c, piece)) {
            case Glyph::Letter:
            case Glyph::Drop: break;
            case Glyph::Punct: endPiece(); break;
            case Glyph::Space: endRun(); break;
        }
    }
    endRun();
    return field;
}

// A query: its folded text and the words that must each land.
struct Query {
    std::u32string key;
    std::vector<std::u32string> terms;
};

Query queryOf(std::string_view text) {
    Query query;
    query.key = foldField(text).key;
    std::u32string cur;
    for (char32_t c : query.key) {
        if (c == U' ') {
            query.terms.push_back(std::move(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) query.terms.push_back(std::move(cur));
    return query;
}

// One edit from four letters, two from eight: a short word one edit away is a different word.
int typoBudget(size_t length) {
    if (length < 4) return 0;
    if (length < 8) return 1;
    return 2;
}

// Levenshtein distance as a yes/no against `max`, two rows, bailing out once every path is over budget.
bool withinEditDistance(const std::u32string& a, const std::u32string& b, int max) {
    const auto la = static_cast<long>(a.size());
    const auto lb = static_cast<long>(b.size());
    if (la - lb > max || lb - la > max) return false;
    std::vector<int> prev(b.size() + 1);
    std::vector<int> cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        int rowMin = cur[0];
        for (size_t j = 1; j <= b.size(); ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
            rowMin = std::min(rowMin, cur[j]);
        }
        if (rowMin > max) return false;
        std::swap(prev, cur);
    }
    return prev[b.size()] <= max;
}

// How a word of the query lands in a field, worst to best.
enum Landing : int { kNone = 0, kTypo = 1, kInner = 2, kPrefix = 3, kWhole = 4 };

// Inside a word only from three letters ("art" in "heart", never "a" in everything), except in
// scripts written without spaces, where two characters are already a word.
bool innerAllowed(const std::u32string& term) {
    if (term.size() >= 3) return true;
    return std::any_of(term.begin(), term.end(), [](char32_t c) { return c >= 0x2E80; });
}

Landing landing(const std::u32string& term, const Field& field, bool typos) {
    Landing best = kNone;
    const bool inner = innerAllowed(term);
    for (const auto& word : field.words) {
        if (word.size() < term.size()) continue;
        if (word.starts_with(term)) {
            if (word.size() == term.size()) return kWhole;
            best = kPrefix;
        } else if (best < kInner && inner && word.find(term) != std::u32string::npos) {
            best = kInner;
        }
    }
    if (best != kNone || !typos) return best;
    const int budget = typoBudget(term.size());
    if (budget == 0) return kNone;
    for (const auto& word : field.words) {
        if (word.front() != term.front()) continue;
        if (withinEditDistance(term, word, budget)) return kTypo;
        // A misspelt start of a longer word: "blondy" is Blondie half-typed.
        if (word.size() > term.size() && withinEditDistance(term, word.substr(0, term.size()), budget)) return kTypo;
    }
    return kNone;
}

// A word start beats the inside of a word, which beats a misspelling; whole and start count alike here,
// so the earlier field keeps a word it starts.
int landingClass(Landing l) { return l >= kPrefix ? 3 : static_cast<int>(l); }

struct Fields {
    const Field* at[4];
    int count;
};

// The rank of `fields` (in rank order) for the query, lower is better, or -1 when a word lands nowhere.
// `tierOut` is the last field the match needed.
int rankOf(const Query& query, const Fields& fields, bool typos, int* tierOut = nullptr) {
    bool typo = false;
    bool inner = false;
    bool allWhole = true;
    int tier = 0;
    int landed = 0;
    for (const auto& term : query.terms) {
        Landing best = kNone;
        int at = -1;
        for (int i = 0; i < fields.count; ++i) {
            const Landing l = landing(term, *fields.at[i], typos);
            if (landingClass(l) > landingClass(best)) {
                best = l;
                at = i;
            }
        }
        if (at < 0) {
            // "the beatles" still finds "Beatles".
            if (term == U"the" && query.terms.size() > 1) continue;
            return -1;
        }
        ++landed;
        typo = typo || best == kTypo;
        inner = inner || best == kInner;
        allWhole = allWhole && best == kWhole;
        tier = std::max(tier, at);
    }
    if (landed == 0) return -1;
    if (tierOut) *tierOut = tier;
    const bool leading = fields.at[tier]->key.starts_with(query.key);
    return (typo ? 32 : 0) | (inner ? 16 : 0) | (tier << 2) | (leading ? 0 : 2) | (allWhole ? 0 : 1);
}

Fields trackFields(const Entry& e) { return Fields{{&e.title, &e.artist, &e.album}, 3}; }

struct Scored {
    int rank;
    int tie;
    int row;
};

std::vector<int> sortedRows(std::vector<Scored>& scored, size_t limit) {
    std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        return a.rank != b.rank ? a.rank < b.rank : a.tie < b.tie;
    });
    std::vector<int> out;
    out.reserve(std::min(limit, scored.size()));
    for (size_t i = 0; i < scored.size() && i < limit; ++i) out.push_back(scored[i].row);
    return out;
}

// Every row the query finds, best first; `tie` is each row's place among equals (null: library order).
std::vector<int> rankTracks(const std::vector<Entry>& entries, const Query& query, bool typos,
                            const std::vector<int>* tie) {
    std::vector<Scored> scored;
    for (size_t i = 0; i < entries.size(); ++i) {
        const int rank = rankOf(query, trackFields(entries[i]), typos);
        if (rank >= 0) scored.push_back({rank, tie ? (*tie)[i] : static_cast<int>(i), static_cast<int>(i)});
    }
    return sortedRows(scored, scored.size());
}

std::vector<Entry> foldRows(const std::vector<SearchRow>& rows) {
    std::vector<Entry> entries;
    entries.reserve(rows.size());
    for (const auto& row : rows) {
        entries.push_back({foldField(row.title), foldField(row.artist), foldField(row.album)});
    }
    return entries;
}

enum class Focus { None, Artist, Album, Title };

// MediaStore's ENTRY_CONTENT_TYPE strings for EXTRA_MEDIA_FOCUS. Genre, playlist and the
// unstructured "vnd.android.cursor.item/*" have no structured field here, so they use the query.
Focus focusOf(std::string_view focus) {
    if (focus == "vnd.android.cursor.item/artist") return Focus::Artist;
    if (focus == "vnd.android.cursor.item/album") return Focus::Album;
    if (focus == "vnd.android.cursor.item/audio") return Focus::Title;
    return Focus::None;
}

bool lands(const Query& query, const Field& field, bool typos) {
    return rankOf(query, Fields{{&field}, 1}, typos) >= 0;
}

std::vector<int> byFocus(const std::vector<Entry>& rows, const SearchRequest& r, bool typos) {
    const Focus focus = focusOf(r.focus);
    if (focus == Focus::None) return {};
    const Query artist = queryOf(r.artist);
    const Query album = queryOf(r.album);
    const Query title = queryOf(r.title);
    const Query& key = focus == Focus::Artist ? artist : focus == Focus::Album ? album : title;
    if (key.terms.empty()) return {};
    std::vector<int> out;
    for (size_t i = 0; i < rows.size(); ++i) {
        const Entry& row = rows[i];
        bool ok = focus == Focus::Artist  ? lands(artist, row.artist, typos)
                  : focus == Focus::Album ? lands(album, row.album, typos)
                                          : lands(title, row.title, typos);
        // An album or song named with its artist narrows to that artist's.
        if (ok && focus != Focus::Artist && !artist.terms.empty()) ok = lands(artist, row.artist, typos);
        if (ok) out.push_back(static_cast<int>(i));
    }
    return out;
}

}  // namespace

SearchIndex::SearchIndex(const std::vector<SearchRow>& rows, const std::vector<long long>& durationsMs,
                         const std::vector<long long>& addedSec)
    : entries_(foldRows(rows)), durationsMs_(durationsMs), addedSec_(addedSec) {
    durationsMs_.resize(rows.size(), 0);
    addedSec_.resize(rows.size(), 0);
    const std::vector<int> byTitle = order(Sort::Title);
    titleRank_.resize(rows.size());
    for (size_t place = 0; place < byTitle.size(); ++place) {
        titleRank_[static_cast<size_t>(byTitle[place])] = static_cast<int>(place);
    }
    // Groups by the tag as written, the way the Albums and Artists tabs group.
    std::unordered_map<std::string_view, int> albums;
    std::unordered_map<std::string_view, int> artists;
    albumOf_.reserve(rows.size());
    artistOf_.reserve(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto album = albums.try_emplace(rows[i].album, static_cast<int>(albumFirst_.size()));
        if (album.second) albumFirst_.push_back(static_cast<int>(i));
        albumOf_.push_back(album.first->second);
        const auto artist = artists.try_emplace(rows[i].artist, static_cast<int>(artistFirst_.size()));
        if (artist.second) artistFirst_.push_back(static_cast<int>(i));
        artistOf_.push_back(artist.first->second);
    }
}

std::vector<int> SearchIndex::order(Sort sort) const {
    std::vector<int> rows(entries_.size());
    std::iota(rows.begin(), rows.end(), 0);
    const auto text = [this](const Field Entry::* first) {
        return [this, first](int a, int b) {
            const Entry& x = entries_[static_cast<size_t>(a)];
            const Entry& y = entries_[static_cast<size_t>(b)];
            const int byFirst = (x.*first).key.compare((y.*first).key);
            return byFirst != 0 ? byFirst < 0 : x.title.key < y.title.key;
        };
    };
    switch (sort) {
        case Sort::Title: std::stable_sort(rows.begin(), rows.end(), text(&Entry::title)); break;
        case Sort::Artist: std::stable_sort(rows.begin(), rows.end(), text(&Entry::artist)); break;
        case Sort::Album: std::stable_sort(rows.begin(), rows.end(), text(&Entry::album)); break;
        case Sort::Duration:
            std::stable_sort(rows.begin(), rows.end(), [this](int a, int b) {
                return durationsMs_[static_cast<size_t>(a)] < durationsMs_[static_cast<size_t>(b)];
            });
            break;
        case Sort::Added:
            std::stable_sort(rows.begin(), rows.end(), [this](int a, int b) {
                return addedSec_[static_cast<size_t>(a)] > addedSec_[static_cast<size_t>(b)];
            });
            break;
    }
    return rows;
}

SearchHits SearchIndex::search(std::string_view text) const {
    const Query query = queryOf(text);
    SearchHits hits;
    if (query.terms.empty()) return hits;
    for (const bool typos : {false, true}) {
        hits.tracks = rankTracks(entries_, query, typos, &titleRank_);

        std::vector<Scored> artists;
        for (size_t g = 0; g < artistFirst_.size(); ++g) {
            const int row = artistFirst_[g];
            const int rank = rankOf(query, Fields{{&entries_[static_cast<size_t>(row)].artist}, 1}, typos);
            if (rank >= 0) artists.push_back({rank, titleRank_[static_cast<size_t>(row)], row});
        }
        hits.artists = sortedRows(artists, static_cast<size_t>(kMaxArtistHits));

        // An album by its name, or its name and artist ("pink floyd wall"); the artist alone is the
        // Artists row's, not every album they made.
        std::vector<int> best(albumFirst_.size(), -1);
        for (size_t i = 0; i < entries_.size(); ++i) {
            const Entry& e = entries_[i];
            int tier = 0;
            const int rank = rankOf(query, Fields{{&e.album, &e.artist}, 2}, typos, &tier);
            if (rank < 0) continue;
            const bool named = tier == 0 || std::any_of(query.terms.begin(), query.terms.end(), [&](const auto& term) {
                return landing(term, e.album, typos) != kNone;
            });
            int& kept = best[static_cast<size_t>(albumOf_[i])];
            if (named && (kept < 0 || rank < kept)) kept = rank;
        }
        std::vector<Scored> albums;
        for (size_t g = 0; g < best.size(); ++g) {
            const int row = albumFirst_[g];
            if (best[g] >= 0) albums.push_back({best[g], titleRank_[static_cast<size_t>(row)], row});
        }
        hits.albums = sortedRows(albums, static_cast<size_t>(kMaxAlbumHits));

        if (!hits.tracks.empty() || !hits.artists.empty() || !hits.albums.empty()) break;
    }
    return hits;
}

bool termMatches(std::string_view term, const std::vector<std::string_view>& fields) {
    const Query query = queryOf(term);
    if (query.terms.empty()) return true;
    std::vector<Field> folded;
    folded.reserve(fields.size());
    for (auto field : fields) folded.push_back(foldField(field));
    for (const bool typos : {false, true}) {
        const bool all = std::all_of(query.terms.begin(), query.terms.end(), [&](const auto& t) {
            return std::any_of(folded.begin(), folded.end(),
                               [&](const Field& f) { return landing(t, f, typos) != kNone; });
        });
        if (all) return true;
    }
    return false;
}

std::vector<int> searchTracks(const std::vector<SearchRow>& rows, const SearchRequest& request) {
    const std::vector<Entry> lib = foldRows(rows);

    for (const bool typos : {false, true}) {
        std::vector<int> hits = byFocus(lib, request, typos);
        if (!hits.empty()) return hits;
    }

    const Query query = queryOf(request.query);
    if (query.terms.empty()) {
        // "Play music": nothing to match. The structured extras alone (no query) still count as a
        // request, and one that found nothing above plays nothing rather than everything.
        const bool asked = !queryOf(request.artist).terms.empty() || !queryOf(request.album).terms.empty() ||
                           !queryOf(request.title).terms.empty();
        if (asked) return {};
        std::vector<int> all(rows.size());
        std::iota(all.begin(), all.end(), 0);
        return all;
    }
    for (const bool typos : {false, true}) {
        std::vector<int> hits = rankTracks(lib, query, typos, nullptr);
        if (!hits.empty()) return hits;
    }
    return {};
}

namespace {

std::vector<Field> foldNames(const std::vector<std::string_view>& names) {
    std::vector<Field> out;
    out.reserve(names.size());
    for (const auto name : names) out.push_back(foldField(name));
    return out;
}

// Every name the query finds, best first, ties in the caller's order.
std::vector<int> rankNames(const std::vector<Field>& names, const Query& query, bool typos) {
    std::vector<Scored> scored;
    for (size_t i = 0; i < names.size(); ++i) {
        const int rank = rankOf(query, Fields{{&names[i]}, 1}, typos);
        if (rank >= 0) scored.push_back({rank, static_cast<int>(i), static_cast<int>(i)});
    }
    return sortedRows(scored, scored.size());
}

}  // namespace

SheetHits searchSheet(const std::vector<SheetTrack>& tracks, const std::vector<std::string_view>& playlists,
                      const std::vector<std::string_view>& presets, std::string_view text) {
    const Query query = queryOf(text);
    SheetHits hits;
    if (query.terms.empty()) return hits;
    std::vector<std::array<Field, 4>> folded;
    folded.reserve(tracks.size());
    for (const SheetTrack& t : tracks) {
        folded.push_back(std::array<Field, 4>{foldField(t.tags.title), foldField(t.tags.artist),
                                              foldField(t.tags.album), foldField(t.extra)});
    }
    const std::vector<Field> playlistNames = foldNames(playlists);
    const std::vector<Field> presetNames = foldNames(presets);
    for (const bool typos : {false, true}) {
        // One entry per song, at the place of its first copy; `tie` is that place.
        std::vector<Scored> found;
        std::unordered_map<std::string_view, size_t> byKey;
        for (size_t i = 0; i < tracks.size(); ++i) {
            const std::array<Field, 4>& f = folded[i];
            const int rank = rankOf(query, Fields{{&f[0], &f[1], &f[2], &f[3]}, 4}, typos);
            if (rank < 0) continue;
            const int row = static_cast<int>(i);
            if (!tracks[i].key.empty()) {
                const auto [slot, fresh] = byKey.try_emplace(tracks[i].key, found.size());
                if (!fresh) {
                    Scored& kept = found[slot->second];
                    kept.rank = std::min(kept.rank, rank);
                    if (tracks[i].preferred && !tracks[static_cast<size_t>(kept.row)].preferred) kept.row = row;
                    continue;
                }
            }
            found.push_back({rank, static_cast<int>(found.size()), row});
        }
        hits.tracks = sortedRows(found, static_cast<size_t>(kMaxSheetTracks));
        hits.playlists = rankNames(playlistNames, query, typos);
        hits.presets = rankNames(presetNames, query, typos);
        if (!hits.tracks.empty() || !hits.playlists.empty() || !hits.presets.empty()) break;
    }
    return hits;
}

}  // namespace geode::library
