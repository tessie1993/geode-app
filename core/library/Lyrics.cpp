#include "library/Lyrics.hpp"

#include <algorithm>
#include <utility>

namespace geode::library {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

size_t digitsAt(std::string_view s, size_t at, size_t most) {
    size_t n = 0;
    while (n < most && at + n < s.size() && isDigit(s[at + n])) ++n;
    return n;
}

long long numberOf(std::string_view s, size_t at, size_t n) {
    long long v = 0;
    for (size_t i = 0; i < n; ++i) v = v * 10 + (s[at + i] - '0');
    return v;
}

// "[mm:ss]", "[mm:ss.x]" or "[mm:ss:xxx]" at s[i]: minutes 1-3 digits, seconds 1-2, an optional fraction of 1-3.
// Returns the stamp's length (0 when none starts here) and its time.
size_t stampAt(std::string_view s, size_t i, long long& timeMs) {
    if (i >= s.size() || s[i] != '[') return 0;
    size_t at = i + 1;
    const size_t minutes = digitsAt(s, at, 3);
    if (minutes == 0 || at + minutes >= s.size() || s[at + minutes] != ':') return 0;
    const long long min = numberOf(s, at, minutes);
    at += minutes + 1;
    const size_t seconds = digitsAt(s, at, 2);
    if (seconds == 0) return 0;
    const long long sec = numberOf(s, at, seconds);
    at += seconds;
    long long fractionMs = 0;
    if (at < s.size() && (s[at] == '.' || s[at] == ':')) {
        const size_t fraction = digitsAt(s, at + 1, 3);
        if (fraction == 0) return 0;
        const long long value = numberOf(s, at + 1, fraction);
        fractionMs = fraction == 1 ? value * 100 : fraction == 2 ? value * 10 : value;
        at += 1 + fraction;
    }
    if (at >= s.size() || s[at] != ']') return 0;
    timeMs = min * 60000 + sec * 1000 + fractionMs;
    return at + 1 - i;
}

// "[ar:Someone]": an LRC header line, not a lyric.
bool isMetadata(std::string_view s) {
    if (s.size() < 4 || s.front() != '[' || s.back() != ']') return false;
    size_t n = 1;
    while (n < s.size() && ((s[n] >= 'a' && s[n] <= 'z') || (s[n] >= 'A' && s[n] <= 'Z'))) ++n;
    return n > 1 && n < s.size() && s[n] == ':';
}

}  // namespace

ParsedLyrics parseLyrics(std::string_view text) {
    std::vector<LyricsLine> timed;
    std::vector<LyricsLine> plain;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = start;
        while (end < text.size() && text[end] != '\n' && text[end] != '\r') ++end;
        const std::string_view raw = text.substr(start, end - start);
        const bool crlf = end + 1 < text.size() && text[end] == '\r' && text[end + 1] == '\n';
        start = end + (crlf ? 2 : 1);

        std::vector<long long> stamps;
        std::string body;
        for (size_t i = 0; i < raw.size();) {
            long long at = 0;
            const size_t len = stampAt(raw, i, at);
            if (len > 0) {
                stamps.push_back(at);
                i += len;
            } else {
                body.push_back(raw[i++]);
            }
        }
        const std::string_view line = trimmed(body);
        if (stamps.empty()) {
            if (!line.empty() && !isMetadata(trimmed(raw))) plain.push_back({-1, std::string(line)});
            continue;
        }
        if (line.empty()) continue;
        for (const long long at : stamps) timed.push_back({at, std::string(line)});
    }
    ParsedLyrics out;
    if (!timed.empty()) {
        std::stable_sort(timed.begin(), timed.end(),
                         [](const LyricsLine& a, const LyricsLine& b) { return a.timeMs < b.timeMs; });
        out.lines = std::move(timed);
        out.synced = true;
    } else {
        out.lines = std::move(plain);
    }
    return out;
}

int lyricsIndexAt(const long long* timesMs, int count, long long positionMs) {
    if (!timesMs || count <= 0 || timesMs[0] < 0) return -1;
    int lo = 0;
    int hi = count - 1;
    int found = -1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        if (timesMs[mid] <= positionMs) {
            found = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return found;
}

std::string lyricsWire(const ParsedLyrics& lyrics) {
    if (lyrics.lines.empty()) return std::string();
    std::string out = lyrics.synced ? "S\n" : "P\n";
    for (const LyricsLine& line : lyrics.lines) {
        out += std::to_string(line.timeMs);
        out += '\t';
        out += line.text;
        out += '\n';
    }
    return out;
}

}  // namespace geode::library
