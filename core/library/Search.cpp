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

// --- Accents (#395's `SearchMatcher.fold`: NFD, drop the non-spacing marks, spell out what does not split) ---
//
// No ICU on the NDK side, so the decompositions a music library meets are tables over the lower-case letters
// `fold` gives: Latin-1, Latin Extended-A and -B (Pinyin, Romanian, Livonian), Latin Extended Additional
// (Vietnamese), Greek tonos and dialytika, Cyrillic breve and diaeresis, and the kana voicing marks. Every
// entry is the canonical decomposition's base letter (UnicodeData.txt field 5); '.' or 0 = the letter has
// none and stays. Letters NFD leaves whole (ħ, ŧ, ĳ, ŀ, ŋ) stay too, as they did in Kotlin.

// Latin-1 lower case, U+00E0..U+00FF.
constexpr std::string_view kLatin1Base = "aaaaaa.ceeeeiiii.nooooo..uuuuy.y";

// Latin Extended-A, U+0100..U+017F, both cases.
constexpr std::string_view kExtABase =
    "aaaaaaccccccccdd"
    "ddeeeeeeeeeegggg"
    "gggghh..iiiiiiii"
    "ii..jjkk.llllll."
    ".llnnnnnn...oooo"
    "oo..rrrrrrssssss"
    "sstttt..uuuuuuuu"
    "uuuuwwyyyzzzzzzs";

// Latin Extended-B, U+01DE..U+01EF by pair: ǟ ǡ ǣ(æ) ǥ ǧ ǩ ǫ ǭ ǯ(ʒ).
constexpr char32_t kExtBPairs1DE[] = {U'a', U'a', 0xE6, 0, U'g', U'k', U'o', U'o', 0x292};

// Latin Extended-B, U+01F8..U+0233 by pair: ǹ ǻ ǽ(æ) ǿ(ø), the double-grave and inverted-breve vowels, ș ț,
// ȝ (none), ȟ, ƞ ȣ ȥ (none), the dotted and macron vowels, ȳ.
constexpr char32_t kExtBPairs1F8[] = {U'n', U'a', 0xE6, 0xF8, U'a', U'a', U'e', U'e', U'i', U'i',
                                      U'o', U'o', U'r', U'r', U'u', U'u', U's', U't', 0,    U'h',
                                      0,    0,    0,    U'a', U'e', U'o', U'o', U'o', U'o', U'y'};

// Latin Extended Additional, U+1E00..U+1E95 by pair.
constexpr std::string_view kExtAddBase =
    "abbbcddddd"
    "eeeeefghhh"
    "hhiikkklll"
    "lmmmnnnnoo"
    "oopprrrrss"
    "sssttttuuu"
    "uuvvwwwwwx"
    "xyzzz";

// Vietnamese, U+1EA0..U+1EFF by pair: ạ ả ấ ầ ẩ ẫ ậ ắ ằ ẳ ẵ ặ, ẹ…ệ, ỉ ị, ọ…ợ, ụ…ự, ỳ ỵ ỷ ỹ, then ỻ ỽ ỿ (none).
constexpr std::string_view kVietBase = "aaaaaaaaaaaaeeeeeeeeiioooooooooooouuuuuuuyyyy...";

// The non-spacing marks NFD splits off and the fold drops: combining diacritics, Cyrillic titlo, Hebrew
// points, Arabic harakat, the extended and supplementary combining blocks, symbol marks, half marks, and the
// kana voicing marks.
bool isCombiningMark(char32_t c) {
    return (c >= 0x300 && c <= 0x36F) || (c >= 0x483 && c <= 0x487) || (c >= 0x591 && c <= 0x5BD) ||
           c == 0x5BF || c == 0x5C1 || c == 0x5C2 || c == 0x5C4 || c == 0x5C5 || c == 0x5C7 ||
           (c >= 0x610 && c <= 0x61A) || (c >= 0x64B && c <= 0x65F) || c == 0x670 ||
           (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) || (c >= 0x20D0 && c <= 0x20F0) ||
           (c >= 0xFE20 && c <= 0xFE2F) || c == 0x3099 || c == 0x309A;
}

// A hiragana (offset 0) or katakana (offset 0x60) letter without its voicing mark: が → か, ぱ → は.
char32_t unvoicedKana(char32_t c, char32_t offset) {
    const char32_t h = c - offset;
    if (h >= 0x304C && h <= 0x3062 && h % 2 == 0) return c - 1;  // が ぎ … ぢ
    if (h == 0x3065 || h == 0x3067 || h == 0x3069) return c - 1;  // づ で ど
    if (h >= 0x3070 && h <= 0x307D) {                              // ば ぱ … ぼ ぽ
        const char32_t step = (h - 0x306F) % 3;
        return c - step;
    }
    return c;
}

// The letter under the accents of a lower-case letter, or the letter itself.
char32_t baseLetter(char32_t c) {
    const auto table = [c](std::string_view t, char32_t from, char32_t per) -> char32_t {
        const char b = t[(c - from) / per];
        return b == '.' ? c : static_cast<char32_t>(static_cast<unsigned char>(b));
    };
    const auto pairs = [c](const char32_t* t, char32_t from) -> char32_t {
        const char32_t b = t[(c - from) / 2];
        return b == 0 ? c : b;
    };
    if (c < 0xE0) return c;
    if (c <= 0xFF) return table(kLatin1Base, 0xE0, 1);
    if (c <= 0x17F) return table(kExtABase, 0x100, 1);
    if (c == 0x1A0 || c == 0x1A1) return U'o';  // ơ
    if (c == 0x1AF || c == 0x1B0) return U'u';  // ư
    if (c >= 0x1CD && c <= 0x1DC) return U"aiouuuuu"[(c - 0x1CD) / 2];  // Pinyin: ǎ ǐ ǒ ǔ ǖ ǘ ǚ ǜ
    if (c >= 0x1DE && c <= 0x1EF) return pairs(kExtBPairs1DE, 0x1DE);
    if (c == 0x1F0) return U'j';
    if (c == 0x1F4 || c == 0x1F5) return U'g';
    if (c >= 0x1F8 && c <= 0x233) return pairs(kExtBPairs1F8, 0x1F8);
    if (c >= 0x390 && c <= 0x3CE) {
        switch (c) {
            case 0x3AC: return 0x3B1;  // ά
            case 0x3AD: return 0x3B5;  // έ
            case 0x3AE: return 0x3B7;  // ή
            case 0x390:                // ΐ
            case 0x3AF:                // ί
            case 0x3CA:                // ϊ
                return 0x3B9;
            case 0x3B0:  // ΰ
            case 0x3CB:  // ϋ
            case 0x3CD:  // ύ
                return 0x3C5;
            case 0x3CC: return 0x3BF;  // ό
            case 0x3CE: return 0x3C9;  // ώ
            default: return c;
        }
    }
    if (c >= 0x439 && c <= 0x4F9) {
        switch (c) {
            case 0x439:  // й
            case 0x45D:  // ѝ
            case 0x4E3:  // ӣ
            case 0x4E5:  // ӥ
                return 0x438;
            case 0x450:  // ѐ
            case 0x451:  // ё
            case 0x4D7:  // ӗ
                return 0x435;
            case 0x453: return 0x433;  // ѓ
            case 0x457: return 0x456;  // ї
            case 0x45C: return 0x43A;  // ќ
            case 0x45E:                // ў
            case 0x4EF:                // ӯ
            case 0x4F1:                // ӱ
            case 0x4F3:                // ӳ
                return 0x443;
            case 0x477: return 0x475;  // ѷ
            case 0x4C2:                // ӂ
            case 0x4DD:                // ӝ
                return 0x436;
            case 0x4D1:  // ӑ
            case 0x4D3:  // ӓ
                return 0x430;
            case 0x4DB: return 0x4D9;  // ӛ
            case 0x4DF: return 0x437;  // ӟ
            case 0x4E7: return 0x43E;  // ӧ
            case 0x4EB: return 0x4E9;  // ӫ
            case 0x4ED: return 0x44D;  // ӭ
            case 0x4F5: return 0x447;  // ӵ
            case 0x4F9: return 0x44B;  // ӹ
            default: return c;
        }
    }
    if (c >= 0x1E00 && c <= 0x1E95) return table(kExtAddBase, 0x1E00, 2);
    if (c >= 0x1E96 && c <= 0x1E99) return U"htwy"[c - 0x1E96];  // ẖ ẗ ẘ ẙ
    if (c >= 0x1EA0 && c <= 0x1EFF) return table(kVietBase, 0x1EA0, 2);
    if (c >= 0x3040 && c <= 0x309F) {
        if (c == 0x3094) return 0x3046;  // ゔ
        if (c == 0x309E) return 0x309D;  // ゞ
        return unvoicedKana(c, 0);
    }
    if (c >= 0x30A0 && c <= 0x30FF) {
        if (c == 0x30F4) return 0x30A6;                 // ヴ
        if (c >= 0x30F7 && c <= 0x30FA) return c - 8;  // ヷ ヸ ヹ ヺ
        if (c == 0x30FE) return 0x30FD;                 // ヾ
        return unvoicedKana(c, 0x60);
    }
    return c;
}

// One code point of search text, folded onto the end of `out`: lower case, no accents, spelled out.
void appendFolded(char32_t raw, std::u32string& out) {
    const char32_t lower = fold(raw);
    if (isCombiningMark(lower)) return;
    const char32_t c = baseLetter(lower);
    switch (c) {
        case 0xDF:  // ß
            out += U"ss";
            return;
        case 0xE6:  // æ
            out += U"ae";
            return;
        case 0x153:  // œ
            out += U"oe";
            return;
        case 0xF8:  // ø
            out.push_back(U'o');
            return;
        case 0x142:  // ł
            out.push_back(U'l');
            return;
        case 0x111:  // đ
        case 0xF0:   // ð
            out.push_back(U'd');
            return;
        case 0xFE:  // þ
            out += U"th";
            return;
        default:
            out.push_back(c);
    }
}

std::u32string folded(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (char32_t c : decode(s)) appendFolded(c, out);
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
bool withinEditDistance(std::u32string_view a, std::u32string_view b, int max) {
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

// True when some word of `field` (split like `words`) is within `term`'s typo budget of it. Walks the field in
// place, so the Library screen's typo pass does not allocate a word list per row.
bool anyWordWithin(const std::u32string& term, const std::u32string& field) {
    const int budget = typoBudget(term.size());
    if (budget == 0) return false;
    const std::u32string_view text(field);
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && isSeparator(text[i])) ++i;
        size_t end = i;
        while (end < text.size() && !isSeparator(text[end])) ++end;
        if (end > i && withinEditDistance(term, text.substr(i, end - i), budget)) return true;
        i = end;
    }
    return false;
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
            appendFolded(text[i], cur);
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
    for (char32_t c : decode(s)) appendFolded(c, out);
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

std::string foldText(std::string_view text) {
    std::u32string out;
    foldInto(text, out);
    return encode(out);
}

namespace {

// One query's terms against row after row, with a field buffer reused between rows, so a 15k-track search does
// not allocate per field.
class Matcher {
public:
    Matcher(std::string_view query, FilterOptions options) : terms_(queryTerms(query)), options_(options) {}

    bool blank() const { return terms_.empty(); }

    // The row's tier (see filterRows), or -1 when some term is in no field. A blank query is tier 0.
    int tier(std::span<const std::string_view> row) {
        if (terms_.empty()) return 0;
        const size_t count = row.size();
        if (fields_.size() < count) fields_.resize(count);
        for (size_t f = 0; f < count; ++f) foldInto(row[f], fields_[f]);
        const int exact = tierBy(count, [](const std::u32string& term, const std::u32string& field) {
            return field.find(term) != std::u32string::npos;
        });
        if (exact >= 0 || !options_.typos) return exact;
        // A typo hit is a guess, so it ranks below everything the user actually spelled.
        const int typo = tierBy(count, [](const std::u32string& term, const std::u32string& field) {
            return field.find(term) != std::u32string::npos || anyWordWithin(term, field);
        });
        return typo < 0 ? -1 : typo + static_cast<int>(count);
    }

private:
    // The furthest field the terms needed, each term at the first field `hit` says holds it; -1 = no match.
    template <typename Hit>
    int tierBy(size_t count, Hit hit) const {
        int needed = 0;
        for (const std::u32string& term : terms_) {
            int at = -1;
            for (size_t f = 0; f < count; ++f) {
                if (hit(term, fields_[f])) {
                    at = static_cast<int>(f);
                    break;
                }
            }
            if (at < 0) return -1;
            needed = std::max(needed, at);
        }
        return needed;
    }

    std::vector<std::u32string> terms_;
    FilterOptions options_;
    std::vector<std::u32string> fields_;
};

}  // namespace

int filterTier(std::span<const std::string_view> fields, std::string_view query, FilterOptions options) {
    return Matcher(query, options).tier(fields);
}

std::vector<int> filterRows(std::span<const FilterRow> rows, std::string_view query, std::size_t max,
                            FilterOptions options) {
    Matcher matcher(query, options);
    if (max == 0 || (matcher.blank() && !options.blankKeepsAll)) return {};

    // One slot per kept row, in first-match order; a key's slot can be taken over by a preferred row.
    struct Kept {
        int row;
        int tier;
    };
    std::vector<Kept> kept;
    std::unordered_map<std::string_view, size_t> slotOf;
    const auto tierOf = [&](const FilterRow& row) { return matcher.tier(row.fields); };
    for (size_t i = 0; i < rows.size(); ++i) {
        const FilterRow& row = rows[i];
        if (!row.key) {
            const int tier = tierOf(row);
            if (tier >= 0) kept.push_back({static_cast<int>(i), tier});
            continue;
        }
        const auto found = slotOf.find(*row.key);
        if (found == slotOf.end()) {
            const int tier = tierOf(row);
            if (tier < 0) continue;
            slotOf.emplace(*row.key, kept.size());
            kept.push_back({static_cast<int>(i), tier});
        } else {
            // Only a preferred row can take a key's slot over, and only from one that is not preferred; the
            // slot keeps the better tier of the two.
            Kept& holder = kept[found->second];
            if (!row.preferred || rows[static_cast<size_t>(holder.row)].preferred) continue;
            const int tier = tierOf(row);
            if (tier < 0) continue;
            holder.row = static_cast<int>(i);
            holder.tier = std::min(holder.tier, tier);
        }
    }
    // Rank, then cap: within a tier the caller's order (its sort, recent first) holds.
    std::stable_sort(kept.begin(), kept.end(), [](const Kept& a, const Kept& b) { return a.tier < b.tier; });
    const size_t n = std::min(max, kept.size());
    std::vector<int> out(n);
    for (size_t k = 0; k < n; ++k) out[k] = kept[k].row;
    return out;
}

}  // namespace geode::library
