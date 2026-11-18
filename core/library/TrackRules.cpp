#include "library/TrackRules.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace geode::library {

namespace {

// ── UTF-8 / UTF-16 ──────────────────────────────────────────────────────────────────────────────────────────

constexpr char32_t kReplacement = 0xFFFD;

// Decodes UTF-8; a malformed byte reads as U+FFFD (what a JVM decoder hands the app for it).
std::u32string decode(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        int extra = 0;
        char32_t cp = 0;
        if (b < 0x80) {
            cp = b;
        } else if (b >= 0xC2 && b < 0xE0) {
            extra = 1;
            cp = b & 0x1F;
        } else if (b >= 0xE0 && b < 0xF0) {
            extra = 2;
            cp = b & 0x0F;
        } else if (b >= 0xF0 && b < 0xF5) {
            extra = 3;
            cp = b & 0x07;
        } else {
            out.push_back(kReplacement);
            ++i;
            continue;
        }
        if (i + static_cast<size_t>(extra) >= s.size()) {
            out.push_back(kReplacement);
            ++i;
            continue;
        }
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            const unsigned char c = static_cast<unsigned char>(s[i + k]);
            if ((c & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (c & 0x3F);
        }
        const bool overlong = (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000);
        if (!ok || overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000)) {
            out.push_back(kReplacement);
            ++i;
            continue;
        }
        out.push_back(cp);
        i += static_cast<size_t>(extra) + 1;
    }
    return out;
}

void encode(char32_t cp, std::string& out) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string encodeAll(const std::u32string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char32_t c : s) encode(c, out);
    return out;
}

// ── Kotlin's character classes ──────────────────────────────────────────────────────────────────────────────

// Char.isWhitespace(): Character.isWhitespace or Character.isSpaceChar. (U+0085 is neither.)
bool isKotlinSpace(char32_t c) {
    if ((c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20)) return true;
    switch (c) {
        case 0x00A0:
        case 0x1680:
        case 0x2028:
        case 0x2029:
        case 0x202F:
        case 0x205F:
        case 0x3000:
            return true;
        default:
            return c >= 0x2000 && c <= 0x200A;
    }
}

// String.trim(): drops Kotlin white space at both ends.
std::string trimText(std::string_view s) {
    std::u32string cps = decode(s);
    size_t a = 0;
    size_t b = cps.size();
    while (a < b && isKotlinSpace(cps[a])) ++a;
    while (b > a && isKotlinSpace(cps[b - 1])) --b;
    return encodeAll(cps.substr(a, b - a));
}

// The zero of each decimal-digit run (general category Nd) in the Basic Multilingual Plane, as Character.digit
// reads them; the runs are ten long. A digit run outside this list is not read as a digit here.
constexpr char32_t kDigitZeros[] = {0x0030, 0x0660, 0x06F0, 0x07C0, 0x0966, 0x09E6, 0x0A66, 0x0AE6, 0x0B66,
                                    0x0BE6, 0x0C66, 0x0CE6, 0x0D66, 0x0DE6, 0x0E50, 0x0ED0, 0x0F20, 0x1040,
                                    0x1090, 0x17E0, 0x1810, 0x1946, 0x19D0, 0x1A80, 0x1A90, 0x1B50, 0x1BB0,
                                    0x1C40, 0x1C50, 0xA620, 0xA8D0, 0xA900, 0xA9D0, 0xA9F0, 0xAA50, 0xABF0,
                                    0xFF10};

int digitValue(char32_t c) {
    for (const char32_t zero : kDigitZeros) {
        if (c >= zero && c < zero + 10) return static_cast<int>(c - zero);
    }
    return -1;
}

// String.toIntOrNull(): an optional sign, then digits (Unicode decimal digits included), no overflow.
std::optional<int> toIntOrNull(const std::u32string& s) {
    if (s.empty()) return std::nullopt;
    size_t i = 0;
    bool negative = false;
    if (s[0] < U'0') {
        if (s.size() == 1) return std::nullopt;
        if (s[0] == U'-') {
            negative = true;
        } else if (s[0] != U'+') {
            return std::nullopt;
        }
        i = 1;
    }
    long long acc = 0;
    for (; i < s.size(); ++i) {
        const int d = digitValue(s[i]);
        if (d < 0) return std::nullopt;
        acc = acc * 10 + d;
        if (acc > 2147483648LL) return std::nullopt;
    }
    if (negative) return static_cast<int>(-acc);
    if (acc > 2147483647LL) return std::nullopt;
    return static_cast<int>(acc);
}

// ── Case ────────────────────────────────────────────────────────────────────────────────────────────────────

// Character.toLowerCase(int) for the scripts a music library holds: Latin (Basic, Supplement, Extended-A, the
// Extended Additional block), Greek, Cyrillic, Armenian, fullwidth Latin, and the few compatibility letters that
// lower to those. Latin Extended-B, Greek Extended, Georgian, Cherokee, Glagolitic and the other rare cased
// scripts come back unchanged (a JVM would lower them).
char32_t lowerCp(char32_t c) {
    if (c < 0x80) return (c >= U'A' && c <= U'Z') ? c + 32 : c;
    if (c >= 0x00C0 && c <= 0x00DE) return c == 0x00D7 ? c : c + 32;
    if (c >= 0x0100 && c <= 0x017F) {
        if (c == 0x0130 || c == 0x0131 || c == 0x0138 || c == 0x0149 || c == 0x017F) return c;
        if (c == 0x0178) return 0x00FF;
        if ((c >= 0x0139 && c <= 0x0148) || (c >= 0x0179 && c <= 0x017E)) return (c & 1) ? c + 1 : c;
        return (c & 1) ? c : c + 1;
    }
    if (c >= 0x0386 && c <= 0x03AB) {
        if (c == 0x0386) return 0x03AC;
        if (c >= 0x0388 && c <= 0x038A) return c + 37;
        if (c == 0x038C) return 0x03CC;
        if (c == 0x038E || c == 0x038F) return c + 63;
        if ((c >= 0x0391 && c <= 0x03A1) || (c >= 0x03A3 && c <= 0x03AB)) return c + 32;
        return c;
    }
    if (c >= 0x0400 && c <= 0x042F) return c <= 0x040F ? c + 80 : c + 32;
    if ((c >= 0x0460 && c <= 0x0481) || (c >= 0x048A && c <= 0x04BF) || (c >= 0x04D0 && c <= 0x052F)) {
        return (c & 1) ? c : c + 1;
    }
    if (c == 0x04C0) return 0x04CF;
    if (c >= 0x04C1 && c <= 0x04CE) return (c & 1) ? c + 1 : c;
    if (c >= 0x0531 && c <= 0x0556) return c + 48;
    if ((c >= 0x1E00 && c <= 0x1E95) || (c >= 0x1EA0 && c <= 0x1EFF)) return (c & 1) ? c : c + 1;
    if (c == 0x1E9E) return 0x00DF;
    if (c == 0x2126) return 0x03C9;
    if (c == 0x212A) return U'k';
    if (c == 0x212B) return 0x00E5;
    if (c >= 0x2160 && c <= 0x216F) return c + 16;
    if (c >= 0x24B6 && c <= 0x24CF) return c + 26;
    if (c >= 0xFF21 && c <= 0xFF3A) return c + 32;
    return c;
}

// Cased letters (upper, lower or title case) in the same scripts, for the final-sigma rule.
bool isCased(char32_t c) {
    if (lowerCp(c) != c) return true;
    return (c >= U'a' && c <= U'z') || c == 0x00AA || c == 0x00B5 || c == 0x00BA ||
           (c >= 0x00DF && c <= 0x00FF && c != 0x00F7) || (c >= 0x0100 && c <= 0x024F) ||
           (c >= 0x0250 && c <= 0x02AF) || (c >= 0x0371 && c <= 0x03FF && c != 0x0387 && c != 0x03F6) ||
           (c >= 0x0430 && c <= 0x0481) || (c >= 0x048A && c <= 0x052F) || (c >= 0x0561 && c <= 0x0587) ||
           (c >= 0x1E00 && c <= 0x1FFF) || (c >= 0xFF41 && c <= 0xFF5A) || (c >= 0x2170 && c <= 0x217F) ||
           (c >= 0x24D0 && c <= 0x24E9);
}

// Case-ignorable characters the final-sigma rule steps over (apostrophes, full stops, colon, middle dot,
// modifier letters and combining marks).
bool isCaseIgnorable(char32_t c) {
    switch (c) {
        case U'\'':
        case U'.':
        case U':':
        case U'^':
        case U'`':
        case 0x00A8:
        case 0x00AD:
        case 0x00AF:
        case 0x00B4:
        case 0x00B7:
        case 0x00B8:
        case 0x2018:
        case 0x2019:
        case 0x2024:
        case 0x2027:
        case 0xFE13:
        case 0xFF07:
        case 0xFF0E:
        case 0xFF1A:
            return true;
        default:
            return (c >= 0x0300 && c <= 0x036F) || (c >= 0x02B0 && c <= 0x02FF) || (c >= 0x200B && c <= 0x200F);
    }
}

bool finalSigma(const std::u32string& s, size_t i) {
    size_t before = i;
    bool precededByCased = false;
    while (before > 0) {
        const char32_t p = s[before - 1];
        if (isCaseIgnorable(p)) {
            --before;
            continue;
        }
        precededByCased = isCased(p);
        break;
    }
    if (!precededByCased) return false;
    for (size_t after = i + 1; after < s.size(); ++after) {
        if (isCaseIgnorable(s[after])) continue;
        return !isCased(s[after]);
    }
    return true;
}

bool inAudioExtensions(const std::string& ext) {
    static const char* const kExts[] = {"mp3", "wav", "flac", "ogg", "m4a", "aac", "opus", "aiff", "aif"};
    for (const char* e : kExts) {
        if (ext == e) return true;
    }
    return false;
}

bool isAsciiClassChar(char32_t c) {
    return (c >= U'0' && c <= U'9') || c == U':' || c == U'%' || (c >= U'A' && c <= U'F');
}

}  // namespace

// String.lowercase() in the ROOT locale: each code point lowered, U+0130 as "i" + U+0307, a capital sigma as the
// final form where it ends a word. The Latin, Greek, Cyrillic and Armenian letters are matched; see lowerCp.
std::string lowerRoot(std::string_view utf8) {
    const std::u32string in = decode(utf8);
    std::u32string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        const char32_t c = in[i];
        if (c == 0x0130) {
            out.push_back(U'i');
            out.push_back(0x0307);
        } else if (c == 0x03A3) {
            out.push_back(finalSigma(in, i) ? 0x03C2 : 0x03C3);
        } else {
            out.push_back(lowerCp(c));
        }
    }
    return encodeAll(out);
}

std::string trimKotlin(std::string_view utf8) { return trimText(utf8); }

// CharSequence.isBlank(): every character is Kotlin white space (an empty string is blank).
bool isBlank(std::string_view utf8) {
    for (const char32_t c : decode(utf8)) {
        if (!isKotlinSpace(c)) return false;
    }
    return true;
}

std::u16string toUtf16(std::string_view utf8) {
    std::u16string out;
    for (const char32_t c : decode(utf8)) {
        if (c >= 0x10000) {
            const char32_t v = c - 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (v >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (v & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(c));
        }
    }
    return out;
}

LibraryTrack tagFallbacks(const TagRead& tags, const FileFacts& file) {
    // MediaMetadataRetriever tag: trimmed, a blank one is none.
    auto tag = [](const std::optional<std::string>& raw) -> std::optional<std::string> {
        if (!raw) return std::nullopt;
        std::string t = trimText(*raw);
        if (t.empty()) return std::nullopt;
        return t;
    };
    auto beforeLast = [](const std::string& s, char sep) {  // substringBeforeLast(sep): s itself with no sep
        const size_t at = s.rfind(sep);
        return at == std::string::npos ? s : s.substr(0, at);
    };
    LibraryTrack t;
    t.uri = file.uri;
    std::optional<std::string> title = tag(tags.title);
    t.artist = tag(tags.artist).value_or("");
    t.album = tag(tags.album).value_or("");
    t.genre = tag(tags.genre).value_or("");
    if (const auto year = tag(tags.year)) {
        std::u32string digits;
        for (const char32_t c : decode(*year)) {
            if (digits.size() == 4) break;
            if (digitValue(c) >= 0) digits.push_back(c);
        }
        t.year = toIntOrNull(digits).value_or(0);
    }
    if (const auto number = tag(tags.trackNumber)) {
        const size_t slash = number->find('/');
        const std::string head = trimText(slash == std::string::npos ? *number : number->substr(0, slash));
        t.trackNo = toIntOrNull(decode(head)).value_or(0);
    }
    if (!title && !isBlank(file.displayName)) title = beforeLast(file.displayName, '.');
    if (!title && file.lastPathSegment) {
        const std::string& seg = *file.lastPathSegment;
        const size_t slash = seg.rfind('/');
        title = beforeLast(slash == std::string::npos ? seg : seg.substr(slash + 1), '.');
    }
    t.title = title ? *title : file.untitled;
    t.fileName = file.displayName;
    t.sizeBytes = file.sizeBytes;
    return t;
}

WalkAction walkAction(std::optional<std::string_view> name, bool isDirectory, std::optional<std::string_view> mimeType,
                      int parentDepth) {
    if (!name) return WalkAction::Skip;
    if (!name->empty() && name->front() == '.') return WalkAction::Skip;
    if (isDirectory) return parentDepth + 1 > 8 ? WalkAction::Skip : WalkAction::Descend;
    if (mimeType && mimeType->substr(0, 6) == "audio/") return WalkAction::KeepAudio;
    const size_t dot = name->rfind('.');
    const std::string ext = dot == std::string_view::npos ? std::string() : lowerRoot(name->substr(dot + 1));
    return inAudioExtensions(ext) ? WalkAction::KeepAudio : WalkAction::Skip;
}

// Regex("^[0-9:%A-F]{4,}$") or (Regex("^\\d+$") with no artist), each as a whole-string match; \d is ASCII.
bool needsTitleRefresh(std::string_view title, std::string_view artist) {
    const bool inClass = std::all_of(title.begin(), title.end(), [](char ch) {
        return isAsciiClassChar(static_cast<unsigned char>(ch));
    });
    if (inClass && title.size() >= 4) return true;
    if (!artist.empty() || title.empty()) return false;
    return std::all_of(title.begin(), title.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
}

bool minTrackKeeps(long long durationMs, int minSeconds) {
    return minSeconds <= 0 || durationMs <= 0 || durationMs >= static_cast<long long>(minSeconds) * 1000LL;
}

int minTrackSecondsStored(int seconds) { return seconds < 0 ? 0 : seconds; }

std::string tagOr(std::optional<std::string_view> value, std::string_view fallback) {
    if (!value || isBlank(*value) || *value == "<unknown>") return std::string(fallback);
    return std::string(*value);
}

std::string folderOf(std::string_view path) {
    const size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string() : std::string(path.substr(0, slash));
}

PlaylistResolution resolvePlaylistEntries(const std::vector<std::string>& baseNames,
                                          const std::vector<std::string>& libraryFileNames,
                                          const std::vector<std::string>& libraryUris) {
    std::unordered_map<std::string, std::vector<size_t>> byName;
    const size_t n = std::min(libraryFileNames.size(), libraryUris.size());
    for (size_t i = 0; i < n; ++i) {
        if (!isBlank(libraryFileNames[i])) byName[lowerRoot(libraryFileNames[i])].push_back(i);
    }
    PlaylistResolution r;
    for (size_t e = 0; e < baseNames.size(); ++e) {
        const auto it = byName.find(lowerRoot(baseNames[e]));
        if (it == byName.end() || it->second.empty()) {
            r.missing.push_back(static_cast<int>(e));
            continue;
        }
        r.uris.push_back(libraryUris[it->second.front()]);
        if (it->second.size() > 1) r.ambiguous.push_back(static_cast<int>(e));
    }
    return r;
}

}  // namespace geode::library
