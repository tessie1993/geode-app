// Host-side checks for the lyrics parse (core/library/Lyrics): an .lrc as timed lines sorted by time, plain
// lyrics in order, the header lines left out, and the line sung at a position. Built and run by CI ("Native
// host tests") with the system compiler; no device.
#include <cstdio>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "library/Lyrics.hpp"

using geode::library::lyricsIndexAt;
using geode::library::parseLyrics;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

std::string wireOf(const char* text) {
    const int need = geode_lyrics_parse(text, nullptr, 0);
    std::string out(static_cast<size_t>(need) + 1, 'x');
    geode_lyrics_parse(text, out.data(), static_cast<int>(out.size()));
    out.resize(static_cast<size_t>(need));
    return out;
}

std::vector<long long> timesOf(const geode::library::ParsedLyrics& lyrics) {
    std::vector<long long> times;
    for (const auto& line : lyrics.lines) times.push_back(line.timeMs);
    return times;
}

void timedLines() {
    const auto lyrics = parseLyrics("[ti:Song]\n[ar:Someone]\n[00:12.50]Second\n[00:05.5]First\n[01:02:030]Third\n");
    check(lyrics.synced, "stamps make synced lyrics");
    check(lyrics.lines.size() == 3, "header lines are not lyrics");
    check(lyrics.lines[0].text == "First" && lyrics.lines[0].timeMs == 5500, "one fraction digit is tenths: 5500 ms");
    check(lyrics.lines[1].text == "Second" && lyrics.lines[1].timeMs == 12500, "two fraction digits are hundredths");
    check(lyrics.lines[2].text == "Third" && lyrics.lines[2].timeMs == 62030, "a colon fraction of three digits is milliseconds");
}

void stamps() {
    const auto none = parseLyrics("[01:07]La\n[00:00]Intro\n[00:20]\n");
    check(none.lines.size() == 2, "a stamp with no text is dropped");
    check(none.lines[0].timeMs == 0 && none.lines[1].timeMs == 67000, "no fraction is zero");
    const auto many = parseLyrics("[00:30.00][01:41.00] Chorus \n");
    check(many.lines.size() == 2, "a line with two stamps is two lines");
    check(many.lines[0].timeMs == 30000 && many.lines[1].timeMs == 101000, "each stamp keeps its time");
    check(many.lines[0].text == "Chorus" && many.lines[1].text == "Chorus", "the text is trimmed");
    const auto wide = parseLyrics("[00:01.00]\xE3\x80\x80\n[00:02.00]\xE3\x80\x80Wide\xC2\xA0\n");
    check(wide.lines.size() == 1 && wide.lines[0].text == "Wide", "ideographic and no-break spaces trim like blanks");
    const auto same = parseLyrics("[00:10.00]B\n[00:10.00]A\n");
    check(same.lines[0].text == "B" && same.lines[1].text == "A", "equal times keep their order");
    const auto inside = parseLyrics("[00:05.00]Sing [00:06.00]along\n");
    check(inside.lines.size() == 2 && inside.lines[0].text == "Sing along", "a stamp inside the text comes out of it");
    const auto bad = parseLyrics("[1234:05]Not a stamp\n[00:123]Nor this\n[00:12.]Nor this\n");
    check(!bad.synced && bad.lines.size() == 3, "malformed stamps are plain text");
    check(bad.lines[0].text == "[1234:05]Not a stamp", "a plain line keeps its brackets");
}

void plainLines() {
    const auto lyrics = parseLyrics("First line\r\n\r\n[ar:Someone]\r\nSecond line\r\n");
    check(!lyrics.synced, "no stamp: not synced");
    check(lyrics.lines.size() == 2, "blank and header lines are skipped, CRLF is one break");
    check(lyrics.lines[0].text == "First line" && lyrics.lines[1].text == "Second line", "plain lines in order");
    check(lyrics.lines[0].timeMs == -1, "a plain line has no time");
    check(parseLyrics("[length: 3:21]\nreal words\n").lines.size() == 1, "a header with spaces is still a header");
    check(parseLyrics("[Chorus]\nwords\n").lines.size() == 2, "a bracket with no colon is a lyric");
}

void nothing() {
    check(parseLyrics("").lines.empty(), "empty text");
    check(parseLyrics("  \n\t\n").lines.empty(), "blank text");
    check(parseLyrics("[ti:Only]\n[ar:Header]\n").lines.empty(), "only headers");
    check(parseLyrics("[00:05.00]\n").lines.empty(), "only empty stamps");
    check(wireOf("").empty(), "no lyrics: an empty wire");
}

void wire() {
    check(wireOf("[00:05.50]Hello\tthere\n[00:01]Hi\n") == "S\n1000\tHi\n5500\tHello\tthere\n", "timed wire");
    check(wireOf("one\ntwo\n") == "P\n-1\tone\n-1\ttwo\n", "plain wire");
    check(geode_lyrics_parse(nullptr, nullptr, 0) == 0, "a null text is no lyrics");
    char small[4];
    check(geode_lyrics_parse("a\nb\n", small, 4) == static_cast<int>(std::string("P\n-1\ta\n-1\tb\n").size()), "a short buffer gets the size needed");
    check(small[3] == '\0', "a short buffer is still terminated");
}

void indexAt() {
    const std::vector<long long> times = {5000, 12000, 12000, 30000};
    const long long* t = times.data();
    check(lyricsIndexAt(t, 4, 0) == -1, "before the first line: -1");
    check(lyricsIndexAt(t, 4, 4999) == -1, "just before the first line: -1");
    check(lyricsIndexAt(t, 4, 5000) == 0, "at the first line");
    check(lyricsIndexAt(t, 4, 11999) == 0, "between lines");
    check(lyricsIndexAt(t, 4, 12000) == 2, "equal times: the last of them");
    check(lyricsIndexAt(t, 4, 999999) == 3, "after the last line");
    check(lyricsIndexAt(t, 0, 5000) == -1, "no lines");
    check(lyricsIndexAt(nullptr, 3, 5000) == -1, "no array");
    const std::vector<long long> plain = {-1, -1};
    check(lyricsIndexAt(plain.data(), 2, 5000) == -1, "plain lyrics have no current line");
    const std::vector<long long> parsed = timesOf(parseLyrics("[00:02]a\n[00:09]b\n"));
    check(geode_lyrics_index_at(parsed.data(), 2, 9000) == 1, "the C ABI answers the same");
}

}  // namespace

int main() {
    timedLines();
    stamps();
    plainLines();
    nothing();
    wire();
    indexAt();
    if (failures == 0) std::printf("lyrics_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
