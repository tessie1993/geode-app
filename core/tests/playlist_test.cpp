// Host-side checks for the playlist file reader (core/library/Playlist): M3U, PLS and XSPF, the names, the
// durations and the DOCTYPE refusal, then the C ABI's wire text. Built and run by CI ("Native host tests").
#include <cstdio>
#include <string>

#include "api/geode_api.h"
#include "library/Playlist.hpp"

using geode::library::kUnknownDuration;
using geode::library::parsePlaylist;
using geode::library::Playlist;
using geode::library::PlaylistFormat;
using geode::library::playlistBaseName;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void m3uKeepsTitlesAndDurationsPerEntry() {
    const Playlist p = parsePlaylist("mix.m3u",
                                     "#EXTM3U\n#PLAYLIST: Road trip \n#EXTINF:215, Song A\nmusic/a.mp3\n"
                                     "\n#EXTINF:-1,Song B\r\nb.flac\n#EXTINF:93\nc.ogg\nd.wav\n");
    check(p.ok && p.format == PlaylistFormat::M3u, "m3u read");
    check(p.name == "Road trip", "#PLAYLIST names it");
    check(p.entries.size() == 4, "four entries");
    check(p.entries[0].location == "music/a.mp3" && p.entries[0].title == "Song A", "title is trimmed");
    check(p.entries[0].durationMs == 215000, "seconds become ms");
    check(p.entries[1].title == "Song B" && p.entries[1].durationMs == kUnknownDuration, "-1 is unknown");
    check(p.entries[2].title.empty() && p.entries[2].durationMs == 93000, "an EXTINF with no comma has no title");
    check(p.entries[3].title.empty() && p.entries[3].durationMs == kUnknownDuration, "info is used once");
}

void m3uWithoutAHeaderOrNameUsesTheFileName() {
    const Playlist p = parsePlaylist("Evening.M3U8", "\xEF\xBB\xBF" "one.mp3\r\ntwo.mp3\r");
    check(p.ok && p.format == PlaylistFormat::M3u, "the extension picks m3u when the text says nothing");
    check(p.name == "Evening", "the name is the file's stem");
    check(p.entries.size() == 2 && p.entries[0].location == "one.mp3", "a BOM and CR line ends are skipped");
}

void plsIsOrderedByItsNumbers() {
    const Playlist p = parsePlaylist("list.pls",
                                     "[playlist]\nNumberOfEntries=3\nFile2=b.mp3\nTitle2=Beta\nLength2=61\n"
                                     "File1=a.mp3\nLength1=-1\nFile10=c.mp3\nTitle10= Gamma \nLength10=oops\nVersion=2\n");
    check(p.ok && p.format == PlaylistFormat::Pls, "pls read");
    check(p.name == "list", "a pls is named by its file");
    check(p.entries.size() == 3, "three entries");
    check(p.entries[0].location == "a.mp3" && p.entries[0].durationMs == kUnknownDuration, "file 1 first");
    check(p.entries[1].location == "b.mp3" && p.entries[1].title == "Beta" && p.entries[1].durationMs == 61000,
          "file 2 second");
    check(p.entries[2].location == "c.mp3" && p.entries[2].title == "Gamma" && p.entries[2].durationMs == kUnknownDuration,
          "file 10 after 2, a bad length unknown");
}

void xspfReadsTracksAndDecodesText() {
    const Playlist p = parsePlaylist(
        "x.xspf",
        "<?xml version=\"1.0\"?><playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\"><title>Fav &amp; more</title>"
        "<trackList><track><location>file:///sdcard/Music/Caf%C3%A9.mp3</location><title>A &lt;b&gt; &#65;</title>"
        "<duration>180000</duration></track><track><title>No place</title></track>"
        "<x:track><x:location>b.mp3</x:location><x:duration>-5</x:duration></x:track></trackList></playlist>");
    check(p.ok && p.format == PlaylistFormat::Xspf, "xspf read");
    check(p.name == "Fav & more", "the playlist title is decoded");
    check(p.entries.size() == 2, "a track with no location is dropped");
    check(p.entries[0].title == "A <b> A" && p.entries[0].durationMs == 180000, "entities and ms");
    check(p.entries[0].baseName == "Caf\xC3\xA9.mp3", "the base name is percent-decoded");
    check(p.entries[1].location == "b.mp3" && p.entries[1].durationMs == kUnknownDuration, "prefixed tags, negative unknown");
}

void xspfRefusesWhatCannotBeTrusted() {
    const Playlist doctype = parsePlaylist("x.xspf", "<!DOCTYPE playlist [<!ENTITY e SYSTEM \"file:///x\">]><playlist><trackList/></playlist>");
    check(!doctype.ok && doctype.why.find("DOCTYPE") != std::string::npos, "a DOCTYPE is refused");
    const Playlist none = parsePlaylist("x.xspf", "<playlist><title>t</title></playlist>");
    check(!none.ok && none.why == "no <trackList> element", "no trackList");
    const Playlist unclosed = parsePlaylist("x.xspf", "<playlist><trackList><track><location>a</location>");
    check(!unclosed.ok, "an unclosed trackList is not read");
}

void unknownFilesAreUnreadable() {
    const Playlist p = parsePlaylist("notes.txt", "hello\nworld\n");
    check(!p.ok && p.why == "not an M3U, PLS or XSPF playlist", "plain text is refused");
    check(parsePlaylist("", "").format == PlaylistFormat::None, "empty is none");
}

void contentBeatsTheExtension() {
    check(parsePlaylist("a.txt", "#EXTM3U\nx.mp3").format == PlaylistFormat::M3u, "header wins over .txt");
    check(parsePlaylist("a.m3u", "[Playlist]\nFile1=x.mp3").format == PlaylistFormat::Pls, "[playlist] wins over .m3u");
}

void baseNamesIgnoreFoldersQueriesAndEscapes() {
    check(playlistBaseName("C:\\Music\\Song.MP3") == "Song.MP3", "windows folders");
    check(playlistBaseName("/a/b/c.flac") == "c.flac", "unix folders");
    check(playlistBaseName("http://h/p/My%20Song.mp3?x=1#t") == "My Song.mp3", "url query, fragment, escape");
    check(playlistBaseName("odd%2") == "odd%2", "a broken escape stays");
    check(playlistBaseName("a?b/c.mp3") == "c.mp3", "a question mark outside a url is part of the path");
}

void cAbiHandsOverTheWireText() {
    const std::string file = "#EXTM3U\n#EXTINF:77,T\nf/a.mp3\n";
    const auto* data = reinterpret_cast<const unsigned char*>(file.data());
    char tiny[4];
    const int need = geode_playlist_parse("p.m3u", data, static_cast<int>(file.size()), tiny, sizeof tiny);
    std::string text(static_cast<size_t>(need) + 1, '\0');
    check(geode_playlist_parse("p.m3u", data, static_cast<int>(file.size()), text.data(), static_cast<int>(text.size())) == need,
          "the same length twice");
    text.resize(static_cast<size_t>(need));
    check(text == std::string("O\x1f") + "p\x1e" + "f/a.mp3\x1f" + "T\x1f" + "77000\x1f" + "a.mp3", "the wire text");
    const int refused = geode_playlist_parse(nullptr, nullptr, 0, nullptr, 0);
    check(refused > 0, "null arguments read as an empty, unreadable file");
    std::string bad(static_cast<size_t>(refused) + 1, '\0');
    geode_playlist_parse(nullptr, nullptr, 0, bad.data(), static_cast<int>(bad.size()));
    check(bad[0] == 'E', "an unreadable file starts with E");
}

}  // namespace

int main() {
    m3uKeepsTitlesAndDurationsPerEntry();
    m3uWithoutAHeaderOrNameUsesTheFileName();
    plsIsOrderedByItsNumbers();
    xspfReadsTracksAndDecodesText();
    xspfRefusesWhatCannotBeTrusted();
    unknownFilesAreUnreadable();
    contentBeatsTheExtension();
    baseNamesIgnoreFoldersQueriesAndEscapes();
    cAbiHandsOverTheWireText();
    if (failures == 0) std::printf("playlist_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
