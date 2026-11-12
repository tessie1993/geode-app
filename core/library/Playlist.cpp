#include "library/Playlist.hpp"

#include <climits>
#include <map>
#include <optional>

namespace geode::library {

namespace {

constexpr size_t kDetectWindow = 512;
constexpr char kFieldSep = '\x1f';
constexpr char kRecordSep = '\x1e';

// The code point at `i`; a byte that starts no whole sequence reads as U+FFFD and takes one byte.
char32_t decodeAt(std::string_view s, size_t i, size_t& len) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    const size_t need = b0 >= 0xF0 ? 4 : b0 >= 0xE0 ? 3 : b0 >= 0xC0 ? 2 : 1;
    len = 1;
    if (need == 1) return b0 < 0x80 ? b0 : 0xFFFD;
    if (i + need > s.size()) return 0xFFFD;
    char32_t c = b0 & (0x7Fu >> need);
    for (size_t k = 1; k < need; ++k) c = (c << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
    len = need;
    return c;
}

// Kotlin's Char.isWhitespace: Java whitespace or a Unicode space separator.
bool isSpace(char32_t c) {
    return c == ' ' || (c >= 9 && c <= 13) || (c >= 0x1C && c <= 0x1F) || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}

std::string_view trimStart(std::string_view s) {
    size_t i = 0;
    while (i < s.size()) {
        size_t len = 1;
        if (!isSpace(decodeAt(s, i, len))) break;
        i += len;
    }
    return s.substr(i);
}

std::string_view trimEnd(std::string_view s) {
    while (!s.empty()) {
        size_t start = s.size() - 1;
        while (start > 0 && s.size() - start < 4 && (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80) --start;
        size_t len = 1;
        char32_t c = decodeAt(s, start, len);
        if (start + len != s.size()) {
            start = s.size() - 1;
            c = decodeAt(s, start, len);
        }
        if (!isSpace(c)) break;
        s.remove_suffix(s.size() - start);
    }
    return s;
}

std::string_view trim(std::string_view s) { return trimEnd(trimStart(s)); }

char lowerAscii(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lowerAscii(c);
    return out;
}

bool startsWithCi(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (lowerAscii(s[i]) != lowerAscii(prefix[i])) return false;
    }
    return true;
}

bool containsCi(std::string_view s, std::string_view needle) {
    for (size_t i = 0; i + needle.size() <= s.size(); ++i) {
        if (startsWithCi(s.substr(i), needle)) return true;
    }
    return false;
}

bool isWord(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Kotlin's toLongOrNull: an optional sign, then digits only.
std::optional<long long> parseLong(std::string_view s) {
    if (s.empty()) return std::nullopt;
    size_t i = 0;
    bool negative = false;
    if (s[0] == '-' || s[0] == '+') {
        negative = s[0] == '-';
        i = 1;
    }
    if (i == s.size()) return std::nullopt;
    long long value = 0;
    for (; i < s.size(); ++i) {
        if (!isDigit(s[i])) return std::nullopt;
        const int digit = s[i] - '0';
        if (value > (LLONG_MAX - digit) / 10) return std::nullopt;
        value = value * 10 + digit;
    }
    return negative ? -value : value;
}

long long secondsToMs(std::optional<long long> seconds) {
    if (!seconds || *seconds < 0 || *seconds > LLONG_MAX / 1000) return kUnknownDuration;
    return *seconds * 1000;
}

std::vector<std::string_view> splitLines(std::string_view s) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\n' && s[i] != '\r') continue;
        lines.push_back(s.substr(start, i - start));
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') ++i;
        start = i + 1;
    }
    lines.push_back(s.substr(start));
    return lines;
}

std::string stemOf(std::string_view fileName) {
    const size_t slash = fileName.find_last_of("/\\");
    const std::string_view name = slash == std::string_view::npos ? fileName : fileName.substr(slash + 1);
    const size_t dot = name.rfind('.');
    return std::string(dot == std::string_view::npos ? name : name.substr(0, dot));
}

PlaylistFormat byExtension(std::string_view fileName) {
    const size_t dot = fileName.rfind('.');
    if (dot == std::string_view::npos) return PlaylistFormat::None;
    const std::string ext = lower(fileName.substr(dot + 1));
    if (ext == "m3u" || ext == "m3u8") return PlaylistFormat::M3u;
    if (ext == "pls") return PlaylistFormat::Pls;
    if (ext == "xspf") return PlaylistFormat::Xspf;
    return PlaylistFormat::None;
}

PlaylistFormat formatOf(std::string_view fileName, std::string_view text) {
    const std::string_view head = trimStart(text.substr(0, kDetectWindow));
    if (startsWithCi(head, "#EXTM3U")) return PlaylistFormat::M3u;
    if (!head.empty() && head[0] == '<' && containsCi(head, "playlist")) return PlaylistFormat::Xspf;
    if (containsCi(head, "[playlist]")) return PlaylistFormat::Pls;
    return byExtension(fileName);
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string percentDecoded(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        const int hi = text[i] == '%' && i + 2 < text.size() ? hexValue(text[i + 1]) : -1;
        const int lo = hi >= 0 ? hexValue(text[i + 2]) : -1;
        if (lo >= 0) {
            out += static_cast<char>(hi * 16 + lo);
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

Playlist parsed(PlaylistFormat format, std::string name, std::vector<PlaylistEntry> entries) {
    Playlist out;
    out.ok = true;
    out.format = format;
    out.name = std::move(name);
    out.entries = std::move(entries);
    for (PlaylistEntry& e : out.entries) e.baseName = playlistBaseName(e.location);
    return out;
}

Playlist unreadable(std::string why) {
    Playlist out;
    out.why = std::move(why);
    return out;
}

Playlist parseM3u(std::string_view fileName, std::string_view text) {
    constexpr std::string_view kExtInf = "#EXTINF:";
    constexpr std::string_view kPlaylistName = "#PLAYLIST:";
    std::string name;
    std::string title;
    long long durationMs = kUnknownDuration;
    std::vector<PlaylistEntry> entries;
    for (const std::string_view raw : splitLines(text)) {
        const std::string_view line = trim(raw);
        if (line.empty()) continue;
        if (line[0] != '#') {
            entries.push_back({std::string(line), title, durationMs, {}});
            title.clear();
            durationMs = kUnknownDuration;
        } else if (startsWithCi(line, kPlaylistName)) {
            name = std::string(trim(line.substr(kPlaylistName.size())));
        } else if (startsWithCi(line, kExtInf)) {
            const std::string_view rest = line.substr(kExtInf.size());
            const size_t comma = rest.find(',');
            durationMs = secondsToMs(parseLong(trim(comma == std::string_view::npos ? rest : rest.substr(0, comma))));
            title = comma == std::string_view::npos ? std::string() : std::string(trim(rest.substr(comma + 1)));
        }
    }
    return parsed(PlaylistFormat::M3u, name.empty() ? stemOf(fileName) : name, std::move(entries));
}

Playlist parsePls(std::string_view fileName, std::string_view text) {
    std::map<int, std::string> files;
    std::map<int, std::string> titles;
    std::map<int, long long> lengths;
    for (const std::string_view line : splitLines(text)) {
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0) continue;
        const std::string key = lower(trim(line.substr(0, eq)));
        size_t digits = 0;
        while (digits < key.size() && !isDigit(key[digits])) ++digits;
        const std::optional<long long> index = parseLong(std::string_view(key).substr(digits));
        if (!index || *index > INT_MAX) continue;
        const std::string_view value = trim(line.substr(eq + 1));
        const int at = static_cast<int>(*index);
        if (key.rfind("file", 0) == 0) {
            files[at] = std::string(value);
        } else if (key.rfind("title", 0) == 0) {
            titles[at] = std::string(value);
        } else if (key.rfind("length", 0) == 0) {
            lengths[at] = parseLong(value).value_or(kUnknownDuration);
        }
    }
    std::vector<PlaylistEntry> entries;
    for (const auto& [at, location] : files) {
        const auto title = titles.find(at);
        const auto length = lengths.find(at);
        entries.push_back({location, title == titles.end() ? std::string() : title->second,
                           length == lengths.end() ? kUnknownDuration : secondsToMs(length->second), {}});
    }
    return parsed(PlaylistFormat::Pls, stemOf(fileName), std::move(entries));
}

// `<[prefix:]tag\b[^>]*>` opening at the '<' at `pos`: the index of its '>', or npos.
size_t openTagEnd(std::string_view t, size_t pos, std::string_view tag) {
    const auto at = [&](size_t nameStart) -> size_t {
        if (t.compare(nameStart, tag.size(), tag) != 0) return std::string_view::npos;
        const size_t after = nameStart + tag.size();
        if (after < t.size() && isWord(t[after])) return std::string_view::npos;
        return t.find('>', after);
    };
    size_t q = pos + 1;
    while (q < t.size() && isWord(t[q])) ++q;
    if (q > pos + 1 && q < t.size() && t[q] == ':') {
        const size_t hit = at(q + 1);
        if (hit != std::string_view::npos) return hit;
    }
    return at(pos + 1);
}

// `</[prefix:]tag>` at `pos`: the index just past its '>', or npos.
size_t closeTagEnd(std::string_view t, size_t pos, std::string_view tag) {
    const auto at = [&](size_t nameStart) -> size_t {
        if (t.compare(nameStart, tag.size(), tag) != 0) return std::string_view::npos;
        const size_t after = nameStart + tag.size();
        return after < t.size() && t[after] == '>' ? after + 1 : std::string_view::npos;
    };
    size_t q = pos + 2;
    while (q < t.size() && isWord(t[q])) ++q;
    if (q > pos + 2 && q < t.size() && t[q] == ':') {
        const size_t hit = at(q + 1);
        if (hit != std::string_view::npos) return hit;
    }
    return at(pos + 2);
}

struct Element {
    bool found = false;
    size_t start = 0;  // the '<' of the opening tag
    size_t end = 0;    // just past the closing tag
    std::string_view content;
};

// The first element named `tag` from `from`, closed by the nearest closing tag (the farthest when `greedy`).
Element findElement(std::string_view t, std::string_view tag, size_t from, bool greedy) {
    for (size_t pos = t.find('<', from); pos != std::string_view::npos; pos = t.find('<', pos + 1)) {
        const size_t gt = openTagEnd(t, pos, tag);
        if (gt == std::string_view::npos) continue;
        const size_t contentStart = gt + 1;
        size_t closeStart = std::string_view::npos;
        size_t closeEnd = 0;
        for (size_t c = t.find("</", contentStart); c != std::string_view::npos; c = t.find("</", c + 1)) {
            const size_t e = closeTagEnd(t, c, tag);
            if (e == std::string_view::npos) continue;
            closeStart = c;
            closeEnd = e;
            if (!greedy) break;
        }
        // No closing tag after this opening means none after any later opening either.
        if (closeStart == std::string_view::npos) return {};
        return {true, pos, closeEnd, t.substr(contentStart, closeStart - contentStart)};
    }
    return {};
}

void appendUtf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

std::string replaceAll(std::string text, std::string_view from, std::string_view to) {
    size_t at = 0;
    while ((at = text.find(from, at)) != std::string::npos) {
        text.replace(at, from.size(), to);
        at += to.size();
    }
    return text;
}

// Numeric references first (1..65535, never a surrogate), then the five named ones, `&amp;` last.
std::string decodeEntities(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '&' && i + 1 < text.size() && text[i + 1] == '#') {
            size_t j = i + 2;
            long long code = 0;
            while (j < text.size() && isDigit(text[j]) && code <= 0xFFFF) code = code * 10 + (text[j++] - '0');
            const bool valid = j > i + 2 && j < text.size() && text[j] == ';' && code >= 1 && code <= 0xFFFF &&
                               !(code >= 0xD800 && code <= 0xDFFF);
            if (valid) {
                appendUtf8(out, static_cast<char32_t>(code));
                i = j + 1;
                continue;
            }
        }
        out += text[i++];
    }
    out = replaceAll(std::move(out), "&lt;", "<");
    out = replaceAll(std::move(out), "&gt;", ">");
    out = replaceAll(std::move(out), "&quot;", "\"");
    out = replaceAll(std::move(out), "&apos;", "'");
    return replaceAll(std::move(out), "&amp;", "&");
}

std::string childText(std::string_view xml, std::string_view tag) {
    const Element el = findElement(xml, tag, 0, false);
    return el.found ? std::string(trim(decodeEntities(el.content))) : std::string();
}

Playlist parseXspf(std::string_view fileName, std::string_view text) {
    if (containsCi(text, "<!DOCTYPE")) {
        return unreadable("refused: a playlist with a DOCTYPE can name files the user did not pick");
    }
    const Element list = findElement(text, "trackList", 0, true);
    if (!list.found) return unreadable("no <trackList> element");
    std::string name = childText(text.substr(0, list.start), "title");
    std::vector<PlaylistEntry> entries;
    for (Element track = findElement(list.content, "track", 0, false); track.found;
         track = findElement(list.content, "track", track.end, false)) {
        std::string location = childText(track.content, "location");
        if (location.empty()) continue;
        const std::optional<long long> duration = parseLong(childText(track.content, "duration"));
        entries.push_back({std::move(location), childText(track.content, "title"),
                           duration && *duration >= 0 ? *duration : kUnknownDuration, {}});
    }
    return parsed(PlaylistFormat::Xspf, name.empty() ? stemOf(fileName) : std::move(name), std::move(entries));
}

void appendField(std::string& out, std::string_view field) {
    for (const char c : field) out += (c == kFieldSep || c == kRecordSep || c == '\0') ? ' ' : c;
}

}  // namespace

std::string playlistBaseName(std::string_view location) {
    std::string_view path = location;
    if (location.find("://") != std::string_view::npos) {
        path = path.substr(0, path.find('?'));
        path = path.substr(0, path.find('#'));
    }
    const std::string decoded = percentDecoded(path);
    const size_t slash = decoded.find_last_of("/\\");
    return slash == std::string::npos ? decoded : decoded.substr(slash + 1);
}

Playlist parsePlaylist(std::string_view fileName, std::string_view bytes) {
    if (bytes.substr(0, 3) == "\xEF\xBB\xBF") bytes.remove_prefix(3);
    switch (formatOf(fileName, bytes)) {
        case PlaylistFormat::M3u:
            return parseM3u(fileName, bytes);
        case PlaylistFormat::Pls:
            return parsePls(fileName, bytes);
        case PlaylistFormat::Xspf:
            return parseXspf(fileName, bytes);
        case PlaylistFormat::None:
            break;
    }
    return unreadable("not an M3U, PLS or XSPF playlist");
}

std::string playlistWire(const Playlist& playlist) {
    std::string out = playlist.ok ? "O" : "E";
    out += kFieldSep;
    appendField(out, playlist.ok ? playlist.name : playlist.why);
    for (const PlaylistEntry& e : playlist.entries) {
        out += kRecordSep;
        appendField(out, e.location);
        out += kFieldSep;
        appendField(out, e.title);
        out += kFieldSep;
        out += std::to_string(e.durationMs);
        out += kFieldSep;
        appendField(out, e.baseName);
    }
    return out;
}

}  // namespace geode::library
