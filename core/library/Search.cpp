#include "library/Search.hpp"

#include <algorithm>
#include <string>

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

// Simple case folding for the scripts a music library mostly holds: ASCII, Latin-1, Latin Extended-A,
// Greek and Cyrillic. Anything else compares exactly.
char32_t fold(char32_t c) {
    if (c >= U'A' && c <= U'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c == 0x178) return 0xFF;  // Ÿ
    if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) {
        // Latin Extended-A pairs upper/lower on even/odd, except the 0x139..0x148 and 0x179..0x17E runs,
        // which pair odd/even.
        const bool oddUpper = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
        if (oddUpper) return (c % 2 == 1) ? c + 1 : c;
        return (c % 2 == 0) ? c + 1 : c;
    }
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
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

}  // namespace geode::library
