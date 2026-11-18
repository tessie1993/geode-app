#pragma once
// Private to core/library: the java.lang.String / Kotlin text rules the playlist, smart-playlist, history and
// session rules were written against, over UTF-8. Case follows Unicode's simple mappings (JvmCase.inc, written
// by tools/jvm_case.py from Python's Unicode tables).
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace geode::library::jvm {

struct CodePoint {
    uint32_t value = 0;
    size_t length = 1;
    bool valid = true;
};

inline CodePoint decode(const std::string& s, size_t i) {
    const auto b = [&](size_t k) { return static_cast<uint8_t>(s[k]); };
    const uint8_t c = b(i);
    const auto cont = [&](size_t k) { return k < s.size() && (b(k) & 0xC0) == 0x80; };
    if (c < 0x80) return {c, 1, true};
    if (c >= 0xC2 && c <= 0xDF && cont(i + 1)) return {static_cast<uint32_t>(((c & 0x1F) << 6) | (b(i + 1) & 0x3F)), 2, true};
    if (c >= 0xE0 && c <= 0xEF && cont(i + 1) && cont(i + 2)) {
        const uint32_t v = ((c & 0x0Fu) << 12) | ((b(i + 1) & 0x3Fu) << 6) | (b(i + 2) & 0x3Fu);
        if (v >= 0x800 && !(v >= 0xD800 && v <= 0xDFFF)) return {v, 3, true};
    }
    if (c >= 0xF0 && c <= 0xF4 && cont(i + 1) && cont(i + 2) && cont(i + 3)) {
        const uint32_t v = ((c & 0x07u) << 18) | ((b(i + 1) & 0x3Fu) << 12) | ((b(i + 2) & 0x3Fu) << 6) | (b(i + 3) & 0x3Fu);
        if (v >= 0x10000 && v <= 0x10FFFF) return {v, 4, true};
    }
    return {0x110000u + c, 1, false};  // a stray byte stays its own character
}

inline std::vector<uint32_t> codePoints(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const CodePoint cp = decode(s, i);
        out.push_back(cp.value);
        i += cp.length;
    }
    return out;
}

inline void append(std::string& out, uint32_t cp) {
    if (cp >= 0x110000u) {
        out += static_cast<char>(cp - 0x110000u);
    } else if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

struct CaseRun {
    uint32_t first;
    uint32_t last;
    int32_t delta;
    uint32_t stride;
};

#include "library/JvmCase.inc"

// The mapping of c in a run table: the run starting at or before c, when c is one of its letters.
template <size_t N>
uint32_t mapped(const CaseRun (&runs)[N], uint32_t c) {
    const CaseRun* end = runs + N;
    const CaseRun* after = std::upper_bound(runs, end, c, [](uint32_t v, const CaseRun& r) { return v < r.first; });
    if (after == runs) return c;
    const CaseRun& r = *(after - 1);
    if (c > r.last || (c - r.first) % r.stride != 0) return c;
    return static_cast<uint32_t>(static_cast<int64_t>(c) + r.delta);
}

// Character.toLowerCase(int).
inline uint32_t lowerOf(uint32_t c) { return c < 0x80 ? ((c >= 'A' && c <= 'Z') ? c + 32 : c) : mapped(kLowerRuns, c); }

// Char.equals(ignoreCase): equal when the upper cases are, or those upper cases' lower cases are; Kotlin compares
// one UTF-16 unit at a time, so a character past the BMP (a surrogate pair) only ever equals itself.
inline uint32_t foldOf(uint32_t c) {
    if (c > 0xFFFF) return c;
    return lowerOf(mapped(kUpperRuns, c));
}

// String.lowercase() (Locale.ROOT): lowerOf, except the capital I with a dot becomes i plus a combining dot.
inline std::string lowered(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const CodePoint cp = decode(s, i);
        if (cp.value == 0x130) out += "i\xCC\x87";
        else append(out, lowerOf(cp.value));
        i += cp.length;
    }
    return out;
}

inline bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    const std::vector<uint32_t> x = codePoints(a);
    const std::vector<uint32_t> y = codePoints(b);
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i) {
        if (x[i] != y[i] && foldOf(x[i]) != foldOf(y[i])) return false;
    }
    return true;
}

// String.contains(other, ignoreCase = true): the empty string is in every string.
inline bool containsIgnoreCase(const std::string& hay, const std::string& needle) {
    const std::vector<uint32_t> h = codePoints(hay);
    const std::vector<uint32_t> n = codePoints(needle);
    if (n.empty()) return true;
    if (n.size() > h.size()) return false;
    for (size_t i = 0; i + n.size() <= h.size(); ++i) {
        bool all = true;
        for (size_t k = 0; k < n.size() && all; ++k) {
            all = h[i + k] == n[k] || foldOf(h[i + k]) == foldOf(n[k]);
        }
        if (all) return true;
    }
    return false;
}

// java.lang.String order: UTF-16 code units, so a supplementary character sorts below U+E000..U+FFFF.
inline bool utf16Less(const std::string& a, const std::string& b) {
    const auto unitsOf = [](const std::string& s) {
        std::vector<uint32_t> units;
        for (const uint32_t cp : codePoints(s)) {
            if (cp >= 0x10000 && cp < 0x110000u) {
                units.push_back(0xD800 + ((cp - 0x10000) >> 10));
                units.push_back(0xDC00 + ((cp - 0x10000) & 0x3FF));
            } else {
                units.push_back(cp);
            }
        }
        return units;
    };
    const std::vector<uint32_t> x = unitsOf(a);
    const std::vector<uint32_t> y = unitsOf(b);
    return std::lexicographical_compare(x.begin(), x.end(), y.begin(), y.end());
}

// Kotlin Char.isWhitespace: Character.isWhitespace or Character.isSpaceChar.
inline bool isWhitespace(uint32_t c) {
    return (c >= 9 && c <= 13) || (c >= 0x1C && c <= 0x20) || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

// String.trimStart() and trim() by isWhitespace.
inline std::string trimStart(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        const CodePoint cp = decode(s, i);
        if (!isWhitespace(cp.value)) break;
        i += cp.length;
    }
    return s.substr(i);
}

inline std::string trim(const std::string& s) {
    std::string front = trimStart(s);
    size_t end = 0;
    for (size_t i = 0; i < front.size();) {
        const CodePoint cp = decode(front, i);
        i += cp.length;
        if (!isWhitespace(cp.value)) end = i;
    }
    front.resize(end);
    return front;
}

inline bool isBlank(const std::string& s) { return trimStart(s).empty(); }

// sortedBy { it.name.lowercase() }: stable, ties keep their order.
template <class T, class NameOf>
void stableSortByLowercase(std::vector<T>& items, NameOf nameOf) {
    std::vector<std::pair<std::string, size_t>> keyed;
    keyed.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i) keyed.emplace_back(lowered(nameOf(items[i])), i);
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return utf16Less(a.first, b.first); });
    std::vector<T> out;
    out.reserve(items.size());
    for (const auto& k : keyed) out.push_back(std::move(items[k.second]));
    items = std::move(out);
}

}  // namespace geode::library::jvm
