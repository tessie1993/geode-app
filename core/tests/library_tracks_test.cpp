// Host-side checks for the music library's rules (core/library/TrackRules, TrackLibraryDoc), ported from the
// Kotlin shell: the tag fallback chain, the folder walk's keep rule, the unread-number title rule, the minimum
// track length, MediaStore's "<unknown>" tag, playlist entry resolution, and library.json (identity, merge,
// sort, info edits, the v3 schema read and written field by field). Each case is the Kotlin behaviour read off
// MusicLibraryRepository.kt, TrackLibrary.kt, DeviceTrackQuery.kt, LibraryPrefsStore.kt and
// PlaylistFormats.kt, plus the cases of MinTrackLengthTest and DeviceTrackTagTest. Built and run by CI
// ("Native host tests"); no device.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/library_tracks_test.cpp
//     core/library/TrackRules.cpp core/library/TrackLibraryDoc.cpp core/util/Json.cpp -o library_tracks_test
#include <cstdio>
#include <string>
#include <vector>

#include "library/TrackLibraryDoc.hpp"
#include "library/TrackRules.hpp"

using namespace geode::library;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

template <class A, class B>
void eq(const A& got, const B& want, const std::string& what) {
    check(got == want, what);
}

LibraryTrack track(const std::string& uri, const std::string& title, const std::string& fileName = "",
                   long long size = 0) {
    LibraryTrack t;
    t.uri = uri;
    t.title = title;
    t.fileName = fileName;
    t.sizeBytes = size;
    return t;
}

std::string doc(const std::vector<LibraryTrack>& tracks) { return writeLibrary(tracks); }

std::vector<std::string> titles(const LibraryEdit& e) {
    std::vector<std::string> out;
    for (const auto& t : e.tracks) out.push_back(t.title);
    return out;
}

std::vector<std::string> uris(const LibraryEdit& e) {
    std::vector<std::string> out;
    for (const auto& t : e.tracks) out.push_back(t.uri);
    return out;
}

using V = std::vector<std::string>;

FileFacts facts(const std::string& displayName = "", std::optional<std::string> segment = std::nullopt) {
    FileFacts f;
    f.uri = "content://m/1";
    f.displayName = displayName;
    f.sizeBytes = 4096;
    f.lastPathSegment = segment;
    f.untitled = "Untitled track";
    return f;
}

// ── The tag fallback chain ──────────────────────────────────────────────────────────────────────────────────

void tagChain() {
    TagRead all;
    all.title = "  Song  ";
    all.artist = "Artist\t";
    all.album = " Album";
    all.genre = "Rock ";
    all.year = "2019-05-01";
    all.trackNumber = "3/12";
    const LibraryTrack t = tagFallbacks(all, facts("file.mp3"));
    eq(t.uri, "content://m/1", "uri passes through");
    eq(t.title, "Song", "title trimmed");
    eq(t.artist, "Artist", "artist trimmed");
    eq(t.album, "Album", "album trimmed");
    eq(t.genre, "Rock", "genre trimmed");
    eq(t.year, 2019, "year: the first four digits of 20190501");
    eq(t.trackNo, 3, "track 3/12 is 3");
    eq(t.fileName, "file.mp3", "fileName is the display name");
    eq(t.sizeBytes, 4096LL, "size passes through");
    eq(t.durationMs, 0LL, "duration is unknown");

    auto year = [](const char* text) {
        TagRead r;
        r.year = text;
        return tagFallbacks(r, facts("x.mp3")).year;
    };
    eq(year("Released 1999"), 1999, "year keeps only digits");
    eq(year("99999"), 9999, "year takes four digits");
    eq(year("12"), 12, "short year");
    eq(year("   "), 0, "blank year is 0");
    eq(year("n/a"), 0, "no digits is 0");
    eq(year("\xD9\xA2\xD9\xA0\xD9\xA2\xD9\xA4"), 2024, "Arabic-Indic digits are digits, as Character.isDigit");
    eq(year("\xEF\xBC\x92\xEF\xBC\x90\xEF\xBC\x91\xEF\xBC\x99"), 2019, "fullwidth digits");
    eq(year("19 99"), 1999, "digits are filtered across spaces");

    auto no = [](const char* text) {
        TagRead r;
        r.trackNumber = text;
        return tagFallbacks(r, facts("x.mp3")).trackNo;
    };
    eq(no("7"), 7, "plain track number");
    eq(no(" 7 / 9 "), 7, "spaces round the number");
    eq(no("abc"), 0, "not a number");
    eq(no("/12"), 0, "empty before the slash");
    eq(no("+4"), 4, "plus sign, as toIntOrNull");
    eq(no("-2"), -2, "minus sign, as toIntOrNull");
    eq(no("+"), 0, "a sign alone");
    eq(no("99999999999"), 0, "overflow is not an int");
    eq(no("2147483647"), 2147483647, "int max");
    eq(no("2147483648"), 0, "int max plus one");
    eq(no("-2147483648"), -2147483647 - 1, "int min");
    eq(no("1 2"), 0, "inner space");
    eq(no("3.5"), 0, "decimal");

    TagRead none;
    auto title = [&](const FileFacts& f) { return tagFallbacks(none, f).title; };
    eq(title(facts("Song Name.mp3")), "Song Name", "title from the display name without extension");
    eq(title(facts("a.b.flac")), "a.b", "only the last extension goes");
    eq(title(facts("noext")), "noext", "no extension");
    eq(title(facts(".hidden")), "", "a leading dot leaves an empty (but present) title");
    eq(title(facts(" lead.mp3")), " lead", "the display name is not trimmed");
    eq(title(facts("", std::string("primary:Music/Track 01.m4a"))), "Track 01", "path segment's file name");
    eq(title(facts("  ", std::string("a/b/c.ogg"))), "c", "blank display name falls to the path segment");
    eq(title(facts("", std::string("plain"))), "plain", "path segment without extension");
    eq(title(facts("", std::nullopt)), "Untitled track", "nothing left reads as untitled");
    eq(title(facts("", std::string(""))), "", "an empty path segment is a title, as the Kotlin elvis");

    TagRead blank;
    blank.title = "   ";
    blank.artist = " ";
    eq(tagFallbacks(blank, facts("From File.wav")).title, "From File", "a blank title tag is none");
    eq(tagFallbacks(blank, facts("x.wav")).artist, "", "a missing artist is empty");
    TagRead nbsp;
    nbsp.title = "\xC2\xA0";
    eq(tagFallbacks(nbsp, facts("n.wav")).title, "n", "no-break space is white space to Kotlin trim");
    TagRead rep;
    rep.title = "\xC3\x89lan ";
    eq(tagFallbacks(rep, facts("n.wav")).title, "\xC3\x89lan", "non-ASCII title kept");
}

// ── The folder walk ─────────────────────────────────────────────────────────────────────────────────────────

void walk() {
    using S = std::optional<std::string_view>;
    eq(walkAction(std::nullopt, false, S("audio/mpeg"), 0), WalkAction::Skip, "nameless entry skipped");
    eq(walkAction(S(".nomedia"), false, std::nullopt, 0), WalkAction::Skip, "dot file skipped");
    eq(walkAction(S(".git"), true, std::nullopt, 0), WalkAction::Skip, "dot folder skipped");
    eq(walkAction(S("Album"), true, std::nullopt, 0), WalkAction::Descend, "folder entered");
    eq(walkAction(S("Album"), true, std::nullopt, 7), WalkAction::Descend, "folder at depth 8 is walked");
    eq(walkAction(S("Album"), true, std::nullopt, 8), WalkAction::Skip, "folder at depth 9 is not");
    eq(walkAction(S("a.mp3"), false, std::nullopt, 8), WalkAction::KeepAudio, "a file at the deepest level is kept");
    eq(walkAction(S("a.bin"), false, S("audio/x-weird"), 0), WalkAction::KeepAudio, "audio MIME keeps");
    eq(walkAction(S("a.bin"), false, S("video/mp4"), 0), WalkAction::Skip, "video MIME does not");
    eq(walkAction(S("a.bin"), false, S("Audio/mpeg"), 0), WalkAction::Skip, "MIME prefix is case-sensitive");
    for (const char* name : {"a.mp3", "a.WAV", "a.Flac", "a.ogg", "a.m4a", "a.aac", "a.opus", "a.aiff", "a.aif"}) {
        eq(walkAction(S(name), false, std::nullopt, 0), WalkAction::KeepAudio, std::string("extension ") + name);
    }
    for (const char* name : {"a.txt", "mp3", "a.mp3x", "a.jpg", "a.", "a.mp3.part"}) {
        eq(walkAction(S(name), false, std::nullopt, 0), WalkAction::Skip, std::string("not audio ") + name);
    }
    eq(walkAction(S("x.mp3"), false, S("application/octet-stream"), 0), WalkAction::KeepAudio,
       "extension keeps when the MIME says nothing useful");
    // substringAfterLast('.', "") of "no.dots.here" is "here"; of ".mp3" is "mp3".
    eq(walkAction(S("dir.mp3"), true, std::nullopt, 0), WalkAction::Descend, "a folder named like a file is a folder");
}

// ── Titles that are unread numbers ──────────────────────────────────────────────────────────────────────────

void numericTitles() {
    eq(needsTitleRefresh("0123", "A"), true, "four digits");
    eq(needsTitleRefresh("ABCDEF", "A"), true, "hex capitals");
    eq(needsTitleRefresh("12:34%AB", "A"), true, "colon and percent");
    eq(needsTitleRefresh("123", "A"), false, "three digits with an artist");
    eq(needsTitleRefresh("123", ""), true, "digits with no artist");
    eq(needsTitleRefresh("1", ""), true, "one digit with no artist");
    eq(needsTitleRefresh("", ""), false, "empty title");
    eq(needsTitleRefresh("abcd", ""), false, "lower-case letters are not in the class");
    eq(needsTitleRefresh("12a4", "A"), false, "one stray lower-case letter");
    eq(needsTitleRefresh("12G4", ""), false, "G is past F");
    eq(needsTitleRefresh("1234\n", "A"), false, "a trailing newline is not matched by $ under matches()");
    eq(needsTitleRefresh("123 ", ""), false, "a trailing space");
    eq(needsTitleRefresh("\xD9\xA1\xD9\xA2", ""), false, "\\d is ASCII only in Java");
    eq(needsTitleRefresh("Song", ""), false, "a real title");
}

// ── The minimum track length (MinTrackLengthTest) ───────────────────────────────────────────────────────────

void minLength() {
    eq(kDefaultMinTrackSeconds, 30, "default is 30 s");
    check(!minTrackKeeps(3000, kDefaultMinTrackSeconds), "3 s hidden");
    check(!minTrackKeeps(29999, kDefaultMinTrackSeconds), "29.999 s hidden");
    check(minTrackKeeps(30000, kDefaultMinTrackSeconds), "30 s kept");
    check(minTrackKeeps(215000, kDefaultMinTrackSeconds), "215 s kept");
    check(minTrackKeeps(1, 0), "zero lists everything");
    check(minTrackKeeps(1, -5), "negative lists everything");
    check(!minTrackKeeps(59000, 60), "59 s under 60");
    check(minTrackKeeps(12000, 10), "12 s over 10");
    check(minTrackKeeps(0, 30), "unknown length kept");
    check(minTrackKeeps(-1, 120), "negative length kept");
    check(!minTrackKeeps(1, 2147483647), "a huge threshold does not overflow");
    eq(minTrackSecondsStored(-4), 0, "stored threshold never below 0");
    eq(minTrackSecondsStored(60), 60, "stored threshold kept");
}

// ── MediaStore tags (DeviceTrackTagTest) ────────────────────────────────────────────────────────────────────

void mediaStoreTags() {
    eq(tagOr(std::string_view("<unknown>"), "Unknown artist"), "Unknown artist", "<unknown> reads as the fallback");
    eq(tagOr(std::nullopt, "Unknown artist"), "Unknown artist", "missing");
    eq(tagOr(std::string_view("  "), "Unknown artist"), "Unknown artist", "blank");
    eq(tagOr(std::string_view(""), "Unknown artist"), "Unknown artist", "empty");
    eq(tagOr(std::string_view("Bj\xC3\xB6rk"), "Unknown artist"), "Bj\xC3\xB6rk", "a real tag is kept");
    eq(tagOr(std::string_view(" <unknown> "), "F"), " <unknown> ", "only the exact marker is the fallback");
    eq(folderOf("/storage/emulated/0/Music/a.mp3"), "/storage/emulated/0/Music", "folder");
    eq(folderOf("a.mp3"), "", "no folder");
    eq(folderOf("/a.mp3"), "", "root folder");
}

// ── Playlist entries against the library ────────────────────────────────────────────────────────────────────

void resolve() {
    const V names = {"Song.MP3", "other.flac", "", "SAME.ogg", "nothing.wav"};
    const V libFiles = {"song.mp3", "Other.FLAC", "", "same.ogg", "Same.OGG", ""};
    const V libUris = {"u1", "u2", "u3", "u4", "u5", "u6"};
    const PlaylistResolution r = resolvePlaylistEntries(names, libFiles, libUris);
    eq(r.uris, V({"u1", "u2", "u4"}), "matched uris, first of several, in entry order");
    eq(r.missing, std::vector<int>({2, 4}), "an empty name and an unknown name are missing");
    eq(r.ambiguous, std::vector<int>({3}), "two tracks with one name is ambiguous");
    const PlaylistResolution none = resolvePlaylistEntries({}, libFiles, libUris);
    check(none.uris.empty() && none.missing.empty() && none.ambiguous.empty(), "no entries");
    const PlaylistResolution ex = resolvePlaylistEntries({"\xC3\x89LAN.mp3"}, {"\xC3\xA9lan.mp3"}, {"x"});
    eq(ex.uris, V({"x"}), "case folding reaches accented letters");
}

// ── library.json: the schema ────────────────────────────────────────────────────────────────────────────────

void schema() {
    LibraryTrack t;
    t.uri = "content://media/1";
    t.title = "T\"q";
    t.artist = "A";
    t.durationMs = 215000;
    t.album = "Al";
    t.genre = "G";
    t.year = 2020;
    t.trackNo = 4;
    t.comment = "c\n";
    t.fileName = "f.mp3";
    t.sizeBytes = 5000000000LL;
    eq(writeLibrary({t}),
       "{\"version\":3,\"tracks\":[{\"uri\":\"content:\\/\\/media\\/1\",\"title\":\"T\\\"q\",\"artist\":\"A\","
       "\"durationMs\":215000,\"album\":\"Al\",\"genre\":\"G\",\"year\":2020,\"trackNo\":4,\"comment\":\"c\\n\","
       "\"fileName\":\"f.mp3\",\"sizeBytes\":5000000000}]}",
       "written: version first, fields in order, slashes escaped, longs whole");
    eq(writeLibrary({}), "{\"version\":3,\"tracks\":[]}", "an empty library");

    const LibraryRead back = readLibrary(writeLibrary({t}));
    check(!back.corrupt && back.tracks.size() == 1, "written then read");
    if (back.tracks.size() == 1) {
        const LibraryTrack& b = back.tracks[0];
        check(b.uri == t.uri && b.title == t.title && b.artist == t.artist && b.durationMs == t.durationMs &&
                  b.album == t.album && b.genre == t.genre && b.year == t.year && b.trackNo == t.trackNo &&
                  b.comment == t.comment && b.fileName == t.fileName && b.sizeBytes == t.sizeBytes,
              "every field survives a round trip");
    }
    eq(writeLibrary(back.tracks), writeLibrary({t}), "read then written is identical");

    LibraryRead r = readLibrary("{\"tracks\":[{\"uri\":\"a\"}]}");
    check(!r.corrupt && r.tracks.size() == 1, "a minimal track reads");
    if (r.tracks.size() == 1) {
        eq(r.tracks[0].title, "Track", "missing title reads as Track");
        check(r.tracks[0].artist.empty() && r.tracks[0].year == 0 && r.tracks[0].durationMs == 0 &&
                  r.tracks[0].sizeBytes == 0 && r.tracks[0].fileName.empty(),
              "other missing fields are empty or 0");
    }
    r = readLibrary("[{\"uri\":\"a\",\"title\":\"X\"}]");
    check(!r.corrupt && r.tracks.size() == 1 && r.tracks[0].title == "X", "a bare array reads");
    r = readLibrary("{\"version\":99,\"tracks\":[{\"uri\":\"a\"}]}");
    check(!r.corrupt && r.tracks.size() == 1, "any version reads");
    r = readLibrary("  \n\t ");
    check(!r.corrupt && r.tracks.empty(), "blank text is an empty library");
    r = readLibrary("");
    check(!r.corrupt && r.tracks.empty(), "empty text is an empty library");
    r = readLibrary("\xC2\xA0{\"tracks\":[]}");
    check(!r.corrupt, "Kotlin trim() drops a no-break space before the document");
    r = readLibrary("{\"tracks\":[]} trailing words");
    check(!r.corrupt, "what follows the first value is ignored, as org.json");
    for (const char* bad : {"garbage", "{\"version\":3}", "{\"tracks\":5}", "5", "{\"tracks\":[{\"uri\":\"a\"},",
                            "\"str\"", "{\"tracks\":"}) {
        r = readLibrary(bad);
        check(r.corrupt && r.tracks.empty(), std::string("corrupt: ") + bad);
    }
    // TrackLibraryTest.anEntryWithNoUriDropsOnlyItself: one bad entry costs only itself.
    r = readLibrary("{\"version\":3,\"tracks\":[{\"uri\":\"content://a\",\"title\":\"A\"},{\"title\":\"no uri\"},7,"
                    "null,{\"uri\":\"content://b\",\"title\":\"B\"}]}");
    check(!r.corrupt && r.tracks.size() == 2 && r.tracks[0].uri == "content://a" && r.tracks[1].uri == "content://b",
          "an entry with no uri, or not an object, drops only itself");
    r = readLibrary("[1]");
    check(!r.corrupt && r.tracks.empty(), "a bare array of non-objects is an empty library");
    r = readLibrary("{\"tracks\":[{\"uri\":5,\"title\":null,\"year\":\"2021\",\"trackNo\":7.9,"
                    "\"durationMs\":\"1500\",\"sizeBytes\":12.0,\"artist\":true}]}");
    check(!r.corrupt && r.tracks.size() == 1, "loose types read");
    if (r.tracks.size() == 1) {
        const LibraryTrack& b = r.tracks[0];
        eq(b.uri, "5", "a number uri is its text (getString)");
        eq(b.title, "null", "a JSON null title is the text null (optString)");
        eq(b.year, 2021, "a string year is parsed (optInt)");
        eq(b.trackNo, 7, "a fractional track number truncates");
        eq(b.durationMs, 1500LL, "a string duration is parsed (optLong)");
        eq(b.sizeBytes, 12LL, "a whole double size");
        eq(b.artist, "true", "a boolean artist is its text");
    }
    r = readLibrary("{\"tracks\":[{\"uri\":\"a\",\"year\":\"abc\",\"trackNo\":null,\"durationMs\":[]}]}");
    check(!r.corrupt && r.tracks[0].year == 0 && r.tracks[0].trackNo == 0 && r.tracks[0].durationMs == 0,
          "unreadable numbers fall back to 0");
}

// ── library.json: identity, merge and sort ──────────────────────────────────────────────────────────────────

void merging() {
    LibraryEdit e = addTracks("", doc({track("u1", "banana"), track("u2", "Apple")}));
    check(e.changed && !e.corruptInput, "an add is a change");
    eq(titles(e), V({"Apple", "banana"}), "sorted by lower-case title");
    eq(e.json, doc(e.tracks), "json is the document of the result");

    e = addTracks(doc({track("u1", "x", "Song.MP3", 100)}), doc({track("u2", "new", "song.mp3", 100)}));
    eq(uris(e), V({"u1"}), "same file name (any case) and size is one track, the held one");
    eq(titles(e), V({"x"}), "the held track's tags stay");

    e = addTracks(doc({track("u1", "x", "Song.MP3", 100)}), doc({track("u2", "new", "song.mp3", 101)}));
    eq(e.tracks.size(), size_t(2), "another size is another track");

    e = addTracks(doc({track("u1", "x", "a.mp3", 0)}), doc({track("u2", "y", "a.mp3", 0)}));
    eq(e.tracks.size(), size_t(2), "no size: identity falls to the uri");
    e = addTracks(doc({track("u1", "x", "", 100)}), doc({track("u2", "y", "", 100)}));
    eq(e.tracks.size(), size_t(2), "no file name: identity falls to the uri");
    e = addTracks(doc({track("u1", "x")}), doc({track("u1", "y")}));
    eq(titles(e), V({"x"}), "the same uri twice is one, the held one");
    e = addTracks(doc({}), doc({track("u1", "x"), track("u1", "y")}));
    eq(titles(e), V({"x"}), "the same uri twice in one add is one, the first");
    e = addTracks(doc({track("u1", "x", "   ", 100)}), doc({track("u2", "y", "   ", 100)}));
    eq(e.tracks.size(), size_t(2), "a blank file name is no file name");

    // A held track with no file name takes the identity of an added track with its uri.
    e = addTracks(doc({track("u1", "held")}), doc({track("u1", "added", "Real.mp3", 77)}));
    eq(e.tracks.size(), size_t(1), "the held track and its re-read are one");
    if (e.tracks.size() == 1) {
        eq(e.tracks[0].title, "held", "the held title stays");
        eq(e.tracks[0].fileName, "Real.mp3", "the held track learns its file name");
        eq(e.tracks[0].sizeBytes, 77LL, "and its size");
    }
    e = addTracks(doc({track("u1", "held"), track("u9", "other", "Real.mp3", 77)}),
                  doc({track("u1", "added", "Real.mp3", 77)}));
    eq(uris(e), V({"u1"}), "two held tracks that become one file: the first wins");
    e = addTracks(doc({track("u1", "held", "", 0)}), doc({track("u1", "a", "Real.mp3", 0)}));
    eq(e.tracks[0].fileName, "", "an added track without a size gives no identity");
    e = addTracks(doc({track("u1", "held", "", 0)}), doc({track("u2", "a", "Real.mp3", 5)}));
    eq(e.tracks.size(), size_t(2), "identity is looked up by uri only");

    e = addTracks(doc({track("a", "b")}), doc({track("b", "B"), track("c", "a")}));
    eq(titles(e), V({"a", "b", "B"}), "equal lower-case titles keep held-then-added order (stable)");
    e = addTracks(doc({track("a", "Zebra"), track("b", "\xC3\x89lan"), track("c", "apple")}), doc({}));
    eq(titles(e), V({"apple", "Zebra", "\xC3\x89lan"}), "UTF-16 order of the lower-cased title: an accent sorts after z");
    e = addTracks(doc({track("a", "\xF0\x9F\x98\x80"), track("b", "\xEF\xBF\xBD")}), doc({}));
    eq(titles(e), V({"\xF0\x9F\x98\x80", "\xEF\xBF\xBD"}), "a surrogate pair sorts before U+FFFD, as UTF-16");
    e = addTracks(doc({track("a", "b"), track("b", "B1"), track("c", "a")}), doc({}));
    eq(titles(e), V({"a", "b", "B1"}), "prefix sorts first");
    e = addTracks(doc({track("a", "\xCE\x91"), track("b", "\xCE\xB1")}), doc({}));
    eq(titles(e), V({"\xCE\x91", "\xCE\xB1"}), "Greek capital and small alpha are equal when lowered: stable");
    e = addTracks(doc({track("a", "\xD0\x96"), track("b", "\xD0\xB0")}), doc({}));
    eq(titles(e), V({"\xD0\xB0", "\xD0\x96"}), "Cyrillic is lower-cased before it is compared");

    // Identity is lower-cased with the JVM's rules.
    e = addTracks(doc({track("a", "x", "\xCE\x9F\xCE\x94\xCE\x9F\xCE\xA3", 5)}),
                  doc({track("b", "y", "\xCE\xBF\xCE\xB4\xCE\xBF\xCF\x82", 5)}));
    eq(e.tracks.size(), size_t(1), "a final capital sigma lowers to the final form");
    e = addTracks(doc({track("a", "x", "\xCE\x9F\xCE\x94\xCE\x9F\xCE\xA3.mp3", 5)}),
                  doc({track("b", "y", "\xCE\xBF\xCE\xB4\xCE\xBF\xCF\x83.mp3", 5)}));
    eq(e.tracks.size(), size_t(1), "a sigma before .mp3 is not final: a cased letter follows the full stop");
    e = addTracks(doc({track("a", "x", "\xCE\xA3\xCE\x91.mp3", 5)}), doc({track("b", "y", "\xCF\x83\xCE\xB1.mp3", 5)}));
    eq(e.tracks.size(), size_t(1), "a sigma at the start lowers to the plain small sigma");
    e = addTracks(doc({track("a", "x", "\xC4\xB0.mp3", 5)}), doc({track("b", "y", "i.mp3", 5)}));
    eq(e.tracks.size(), size_t(2), "capital I with dot lowers to i and a combining dot, not to i");
    e = addTracks(doc({track("a", "x", "I.mp3", 5)}), doc({track("b", "y", "i.mp3", 5)}));
    eq(e.tracks.size(), size_t(1), "ASCII I is i in the ROOT locale");
    e = addTracks(doc({track("a", "x", "\xE2\x84\xAA.mp3", 5)}), doc({track("b", "y", "k.mp3", 5)}));
    eq(e.tracks.size(), size_t(1), "the Kelvin sign lowers to k");

    // Unreadable held file: the edit carries on from an empty library and says so.
    e = addTracks("not json", doc({track("u", "t")}));
    check(e.corruptInput && e.changed && e.tracks.size() == 1, "a corrupt file is set aside and the add goes on");
    e = addTracks(doc({track("u", "t")}), "not json");
    check(!e.corruptInput && e.tracks.size() == 1, "an unreadable add adds nothing");
    e = addTracks("[{\"uri\":\"u\",\"title\":\"t\"}]", "");
    eq(e.json, "{\"version\":3,\"tracks\":[{\"uri\":\"u\",\"title\":\"t\",\"artist\":\"\",\"durationMs\":0,"
               "\"album\":\"\",\"genre\":\"\",\"year\":0,\"trackNo\":0,\"comment\":\"\",\"fileName\":\"\","
               "\"sizeBytes\":0}]}",
       "an old bare-array file is written back as version 3");
}

// ── library.json: info edits ────────────────────────────────────────────────────────────────────────────────

void infoEdits() {
    LibraryTrack a = track("a", "old", "f.mp3", 9);
    a.durationMs = 1234;
    a.artist = "ar";
    a.album = "al";
    a.genre = "ge";
    a.year = 1999;
    a.trackNo = 2;
    a.comment = "co";
    const std::string held = doc({a, track("b", "middle")});

    TrackInfo info;
    info.title = "zzz";
    info.artist = "A2";
    info.album = "B2";
    info.genre = "G2";
    info.year = 2001;
    info.trackNo = 5;
    info.comment = "C2";
    LibraryEdit e = saveTrackInfo(held, "a", info);
    check(e.changed, "an info save is a change");
    eq(titles(e), V({"middle", "zzz"}), "re-sorted by the new title");
    const LibraryTrack& s = e.tracks[1];
    check(s.artist == "A2" && s.album == "B2" && s.genre == "G2" && s.year == 2001 && s.trackNo == 5 &&
              s.comment == "C2",
          "info fields set");
    check(s.durationMs == 1234 && s.fileName == "f.mp3" && s.sizeBytes == 9 && s.uri == "a",
          "duration and file facts kept");

    e = saveTrackInfo(held, "new", info);
    eq(e.tracks.size(), size_t(3), "an unknown uri adds a track");
    check(e.tracks[2].uri == "new" && e.tracks[2].title == "zzz" && e.tracks[2].durationMs == 0 &&
              e.tracks[2].fileName.empty(),
          "the added track holds the info only");

    // Duplicate uris collapse: first place, last value (a LinkedHashMap put, associateBy).
    LibraryTrack d1 = track("a", "x");
    d1.album = "first";
    LibraryTrack d2 = track("a", "y", "f", 3);
    d2.album = "last";
    const std::string dup = doc({d1, track("b", "m"), d2});
    e = saveTrackInfo(dup, "a", info);
    eq(titles(e), V({"m", "zzz"}), "duplicates become one");
    eq(e.tracks[1].fileName, "f", "the base is the last of the duplicates");

    e = saveTitleArtist(dup, "a", "T9", "R9");
    check(e.changed && e.tracks.size() == 2, "title and artist save");
    eq(e.tracks[1].album, "first", "the other info comes from the first of the duplicates");
    eq(e.tracks[1].fileName, "f", "the base is the last of the duplicates");
    eq(e.tracks[1].title, "T9", "new title");
    eq(e.tracks[1].artist, "R9", "new artist");
    e = saveTitleArtist(held, "a", "new title", "new artist");
    check(e.tracks[1].year == 1999 && e.tracks[1].comment == "co" && e.tracks[1].genre == "ge" &&
              e.tracks[1].trackNo == 2 && e.tracks[1].album == "al",
          "the other info is kept");
    e = saveTitleArtist(held, "nobody", "t", "a");
    check(!e.changed && e.tracks.size() == 2, "an unknown uri changes nothing");
    e = saveTitleArtist("junk", "a", "t", "a");
    check(e.corruptInput && !e.changed && e.tracks.empty(), "a corrupt file with no such track");
}

void freshTitles() {
    LibraryTrack n1 = track("n1", "0123");
    LibraryTrack n2 = track("n2", "1234");
    n2.artist = "Known";
    LibraryTrack n3 = track("n3", "77");
    LibraryTrack ok = track("ok", "Real Song");
    const std::string held = doc({n1, n2, n3, ok});
    eq(titleRefreshUris(held), V({"n1", "n2", "n3"}), "unread-number titles, in library order");

    LibraryEdit e = applyFreshTitles(held, {{"n1", "Real One", "Artist One"}});
    check(e.changed, "a re-read title is a change");
    eq(titles(e), V({"1234", "77", "Real One", "Real Song"}), "saved and re-sorted");
    e = applyFreshTitles(held, {{"n1", "0123", ""}});
    check(!e.changed, "a read that matches the held track changes nothing");
    e = applyFreshTitles(held, {{"n2", "1234", "Known"}, {"ghost", "x", "y"}});
    check(!e.changed, "same tags, and an unknown uri, change nothing");
    e = applyFreshTitles(held, {{"n1", "A", "B"}, {"n3", "C", ""}, {"ghost", "x", "y"}});
    check(e.changed && e.tracks.size() == 4, "several in a row");
    eq(titles(e), V({"1234", "A", "C", "Real Song"}), "each applied in turn");
    e = applyFreshTitles("bad", {{"n1", "A", "B"}});
    check(e.corruptInput && !e.changed, "a corrupt file has nothing to refresh");
    check(titleRefreshUris("bad").empty(), "no candidates in a corrupt file");
}

}  // namespace

int main() {
    tagChain();
    walk();
    numericTitles();
    minLength();
    mediaStoreTags();
    resolve();
    schema();
    merging();
    infoEdits();
    freshTitles();
    if (failures == 0) std::printf("library_tracks_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
