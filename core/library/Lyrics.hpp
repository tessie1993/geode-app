#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace geode::library {

struct LyricsLine {
    long long timeMs = -1;  // -1 on a line of plain (untimed) lyrics
    std::string text;
};

struct ParsedLyrics {
    std::vector<LyricsLine> lines;
    bool synced = false;
};

// The most lyric text one parse hands out: the most the shell reads from one lyrics file (ReadCap.LYRICS). A line
// with several stamps is copied once per stamp, so without it a short file of many stamps on one long line would
// ask for gigabytes.
inline constexpr size_t kMaxLyricsTextBytes = size_t{1} << 20;

// An .lrc (or plain lyrics) as lines. Timed lines come back sorted by time, a line with several stamps once per
// stamp until the timed text reaches kMaxLyricsTextBytes; with no stamp anywhere the plain lines come back in order
// and `synced` is false. Blank text, or text with nothing to show, gives no lines.
ParsedLyrics parseLyrics(std::string_view text);

// The line being sung at `positionMs`: the last of `count` ascending `timesMs` at or before it, or -1 before the
// first line, with no lines, or when the lyrics are plain (their times are -1).
int lyricsIndexAt(const long long* timesMs, int count, long long positionMs);

// The wire form the C ABI hands out: "S" (timed) or "P" (plain) and a newline, then one "<timeMs>\t<text>\n" per
// line. A line's text holds no newline, so the first tab ends the time. Empty when there are no lyrics.
std::string lyricsWire(const ParsedLyrics& lyrics);

}  // namespace geode::library
