#include "library/Search.hpp"

#include <algorithm>
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

// Simple case folding, one letter to one letter, so a folded substring search answers what Kotlin's
// `contains(ignoreCase = true)` answers: ASCII, Latin-1, Latin Extended-A and the paired runs of Extended-B
// and Extended Additional (Pinyin, Vietnamese), Greek, Cyrillic, Armenian, full-width Latin, and the few
// letters whose upper or lower case is a plain letter (Turkish i, long s, Kelvin, Angstrom, Ohm, micro).
// Anything else compares exactly.
char32_t fold(char32_t c) {
    if (c < 0x80) return (c >= U'A' && c <= U'Z') ? c + 32 : c;
    switch (c) {
        case 0x130:  // capital I with dot
        case 0x131:  // dotless i: Kotlin matches it with I and i through their upper case
            return U'i';
        case 0x17F:  // long s
            return U's';
        case 0x178:  // capital Y with diaeresis
            return 0xFF;
        case 0xB5:  // micro sign, to Greek mu
            return 0x3BC;
        case 0x1E9E:  // capital sharp s
            return 0xDF;
        case 0x212A:  // Kelvin sign
            return U'k';
        case 0x212B:  // Angstrom sign
            return 0xE5;
        case 0x2126:  // Ohm sign
            return 0x3C9;
        case 0x386:  // Greek capital alpha with tonos
            return 0x3AC;
        case 0x38C:  // Greek capital omicron with tonos
            return 0x3CC;
        case 0x3C2:  // final sigma
            return 0x3C3;
        case 0x4C0:  // Cyrillic palochka
            return 0x4CF;
        case 0x1A0:  // Vietnamese O with horn
        case 0x1AF:  // Vietnamese U with horn
            return c + 1;
        default:
            break;
    }
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c >= 0x100 && c <= 0x17F && c != 0x138 && c != 0x149) {
        // Latin Extended-A pairs upper/lower on even/odd, except the 0x139..0x148 and 0x179..0x17E runs,
        // which pair odd/even.
        const bool oddUpper = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
        if (oddUpper) return (c % 2 == 1) ? c + 1 : c;
        return (c % 2 == 0) ? c + 1 : c;
    }
    if (c >= 0x1CD && c <= 0x1DC) return (c % 2 == 1) ? c + 1 : c;  // Pinyin's caron and diaeresis vowels
    if ((c >= 0x1DE && c <= 0x1EF) || (c >= 0x1F8 && c <= 0x21F) || (c >= 0x222 && c <= 0x233)) {
        return (c % 2 == 0) ? c + 1 : c;
    }
    if (c >= 0x388 && c <= 0x38A) return c + 37;  // Greek capitals with tonos: epsilon, eta, iota
    if (c >= 0x38E && c <= 0x38F) return c + 63;  // upsilon, omega
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    if ((c >= 0x460 && c <= 0x481) || (c >= 0x48A && c <= 0x4BF) || (c >= 0x4D0 && c <= 0x52F)) {
        return (c % 2 == 0) ? c + 1 : c;
    }
    if (c >= 0x4C1 && c <= 0x4CE) return (c % 2 == 1) ? c + 1 : c;
    if (c >= 0x531 && c <= 0x556) return c + 48;                      // Armenian
    if ((c >= 0x1E00 && c <= 0x1E95) || (c >= 0x1EA0 && c <= 0x1EFF)) {  // Latin Extended Additional
        return (c % 2 == 0) ? c + 1 : c;
    }
    if (c >= 0x2160 && c <= 0x216F) return c + 16;  // Roman numerals
    if (c >= 0x24B6 && c <= 0x24CF) return c + 26;  // circled letters
    if (c >= 0xFF21 && c <= 0xFF3A) return c + 32;  // full-width Latin
    return c;
}

std::u32string folded(std::string_view s) {
    std::u32string out = decode(s);
    for (char32_t& c : out) c = fold(c);
    return out;
}

bool isSeparator(char32_t c) {
    return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == U'\f' || c == U'\v' || c == U'-' ||
           c == U'_' || c == U'.' || c == U'/';
}

std::vector<std::u32string> words(const std::u32string& text) {
    std::vector<std::u32string> out;
    std::u32string cur;
    for (char32_t c : text) {
        if (isSeparator(c)) {
            if (!cur.empty()) out.push_back(std::move(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

int typoBudget(size_t length) {
    if (length <= 3) return 0;
    if (length <= 5) return 1;
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

bool termIn(const std::u32string& term, const std::vector<std::u32string>& fields) {
    if (term.empty()) return true;
    for (const auto& f : fields) {
        if (f.find(term) != std::u32string::npos) return true;
    }
    const int budget = typoBudget(term.size());
    if (budget == 0) return false;
    for (const auto& f : fields) {
        for (const auto& w : words(f)) {
            if (withinEditDistance(term, w, budget)) return true;
        }
    }
    return false;
}

// Every word of `value` is somewhere in `fields`. A blank value has no words and matches nothing,
// so a filter is only applied when the caller has something to filter by.
bool allWordsIn(const std::vector<std::u32string>& terms, const std::vector<std::u32string>& fields) {
    if (terms.empty()) return false;
    return std::all_of(terms.begin(), terms.end(), [&](const auto& t) { return termIn(t, fields); });
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

struct FoldedRow {
    std::u32string title;
    std::u32string artist;
    std::u32string album;
};

std::vector<int> byFocus(const std::vector<FoldedRow>& rows, const SearchRequest& r) {
    const Focus focus = focusOf(r.focus);
    if (focus == Focus::None) return {};
    const auto artist = words(folded(r.artist));
    const auto album = words(folded(r.album));
    const auto title = words(folded(r.title));
    const auto* key = focus == Focus::Artist ? &artist : focus == Focus::Album ? &album : &title;
    if (key->empty()) return {};
    std::vector<int> out;
    for (size_t i = 0; i < rows.size(); ++i) {
        const FoldedRow& row = rows[i];
        bool ok = focus == Focus::Artist  ? allWordsIn(artist, {row.artist})
                  : focus == Focus::Album ? allWordsIn(album, {row.album})
                                          : allWordsIn(title, {row.title});
        // An album or song named with its artist narrows to that artist's.
        if (ok && focus != Focus::Artist && !artist.empty()) ok = allWordsIn(artist, {row.artist});
        if (ok) out.push_back(static_cast<int>(i));
    }
    return out;
}

}  // namespace

bool termMatches(std::string_view term, const std::vector<std::string_view>& fields) {
    std::vector<std::u32string> f;
    f.reserve(fields.size());
    for (auto field : fields) f.push_back(folded(field));
    return termIn(folded(term), f);
}

std::vector<int> searchTracks(const std::vector<SearchRow>& rows, const SearchRequest& request) {
    std::vector<FoldedRow> lib;
    lib.reserve(rows.size());
    for (const auto& row : rows) lib.push_back({folded(row.title), folded(row.artist), folded(row.album)});

    std::vector<int> hits = byFocus(lib, request);
    if (!hits.empty()) return hits;

    const auto terms = words(folded(request.query));
    if (terms.empty()) {
        // "Play music": nothing to match. The structured extras alone (no query) still count as a
        // request, and one that found nothing above plays nothing rather than everything.
        const bool asked = !words(folded(request.artist)).empty() || !words(folded(request.album)).empty() ||
                           !words(folded(request.title)).empty();
        if (asked) return {};
        hits.resize(rows.size());
        for (size_t i = 0; i < rows.size(); ++i) hits[i] = static_cast<int>(i);
        return hits;
    }
    for (size_t i = 0; i < lib.size(); ++i) {
        if (allWordsIn(terms, {lib[i].title, lib[i].artist, lib[i].album})) hits.push_back(static_cast<int>(i));
    }
    return hits;
}

// --- Library text filter (#341) ----------------------------------------------------------------------------
namespace {

// Kotlin's `Char.isWhitespace`: what `trim()` strips and `isNotBlank()` looks through.
bool isKotlinWhitespace(char32_t c) {
    return (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F ||
           c == 0x3000;
}

// Java's `\s` without UNICODE_CHARACTER_CLASS: the split the Kotlin SearchMatcher.terms used.
bool isRegexSpace(char32_t c) { return c == U' ' || (c >= 0x09 && c <= 0x0D); }

// The query as SearchMatcher.terms read it (trimmed, split on `\s+`, blank pieces dropped), then folded.
std::vector<std::u32string> queryTerms(std::string_view query) {
    const std::u32string text = decode(query);
    size_t from = 0;
    size_t until = text.size();
    while (from < until && isKotlinWhitespace(text[from])) ++from;
    while (until > from && isKotlinWhitespace(text[until - 1])) --until;
    std::vector<std::u32string> out;
    std::u32string cur;
    const auto flush = [&] {
        if (!std::all_of(cur.begin(), cur.end(), isKotlinWhitespace)) out.push_back(cur);
        cur.clear();
    };
    for (size_t i = from; i < until; ++i) {
        if (isRegexSpace(text[i])) {
            flush();
        } else {
            cur.push_back(fold(text[i]));
        }
    }
    flush();
    return out;
}

// `folded`, into a buffer the caller reuses row after row, so a 15k-track search does not allocate per field.
void foldInto(std::string_view s, std::u32string& out) {
    out.clear();
    // Plain ASCII, the bulk of most libraries' tags, skips the decoder.
    if (std::all_of(s.begin(), s.end(), [](char ch) { return static_cast<unsigned char>(ch) < 0x80; })) {
        for (char ch : s) out.push_back(fold(static_cast<char32_t>(static_cast<unsigned char>(ch))));
        return;
    }
    out = decode(s);
    for (char32_t& c : out) c = fold(c);
}

std::string encode(const std::u32string& s) {
    std::string out;
    for (char32_t c : s) {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

}  // namespace

std::vector<std::string> filterTerms(std::string_view query) {
    std::vector<std::string> out;
    for (const auto& term : queryTerms(query)) out.push_back(encode(term));
    return out;
}

std::vector<int> filterRows(std::span<const FilterRow> rows, std::string_view query, std::size_t max) {
    const std::vector<std::u32string> terms = queryTerms(query);
    if (terms.empty() || max == 0) return {};

    // One slot per kept row, in first-match order; a key's slot can be taken over by a preferred row.
    std::vector<int> kept;
    std::unordered_map<std::string_view, size_t> slotOf;
    std::vector<std::u32string> fields;
    const auto matches = [&](const FilterRow& row) {
        if (fields.size() < row.fields.size()) fields.resize(row.fields.size());
        for (size_t f = 0; f < row.fields.size(); ++f) foldInto(row.fields[f], fields[f]);
        return std::all_of(terms.begin(), terms.end(), [&](const std::u32string& term) {
            for (size_t f = 0; f < row.fields.size(); ++f) {
                if (fields[f].find(term) != std::u32string::npos) return true;
            }
            return false;
        });
    };
    for (size_t i = 0; i < rows.size(); ++i) {
        const FilterRow& row = rows[i];
        if (!row.key) {
            // Rows past `max` would be cut anyway, so a full list skips the matching.
            if (kept.size() < max && matches(row)) kept.push_back(static_cast<int>(i));
            continue;
        }
        const auto found = slotOf.find(*row.key);
        if (found == slotOf.end()) {
            if (kept.size() >= max || !matches(row)) continue;
            slotOf.emplace(*row.key, kept.size());
            kept.push_back(static_cast<int>(i));
        } else {
            // Only a preferred row can take a key's slot over, and only from one that is not preferred.
            const auto holder = static_cast<size_t>(kept[found->second]);
            if (!row.preferred || rows[holder].preferred || !matches(row)) continue;
            kept[found->second] = static_cast<int>(i);
        }
    }
    return kept;
}

}  // namespace geode::library
