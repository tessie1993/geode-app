// Host-side checks for the play history (core/library/History): how history.json reads (an object or a bare
// array, blank entries skipped, a repeated uri keeping its first place), what a play does (count, title,
// artist kept when blank, a last-played stamp that is always newer than any held), listened time, the
// newest-first list (the Kotlin PlayRecorderTest's "recent saw every track, newest first", and its empty-before-
// play case), what survives on disk: the union of the 200 most recent and the 200 most played, and the wire the
// shell splits. No device.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/library_history_test.cpp
//     core/library/History.cpp core/util/Json.cpp -o library_history_test && ./library_history_test
#include <cstdio>
#include <string>
#include <vector>

#include "library/History.hpp"

using namespace geode::library;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

std::vector<std::string> uris(const std::vector<HistoryEntry>& es) {
    std::vector<std::string> out;
    for (const auto& e : es) out.push_back(e.uri);
    return out;
}

std::string entryJson(const std::string& uri, long long last, int count, long long ms = 0) {
    return R"({"uri":")" + uri + R"(","last":)" + std::to_string(last) + R"(,"count":)" + std::to_string(count) +
           R"(,"title":"T","artist":"A","ms":)" + std::to_string(ms) + "}";
}

std::string docOf(const std::vector<std::string>& entries) {
    std::string out = R"({"tracks":[)";
    for (size_t i = 0; i < entries.size(); ++i) out += (i ? "," : "") + entries[i];
    return out + "]}";
}

void reading() {
    check(historyParse("").entries.empty() && !historyParse("").corrupt, "no file text is no history");
    check(historyParse(" \n\t").entries.empty() && !historyParse(" \n\t").corrupt, "blank text is no history, not a broken one");
    const auto doc = historyParse(docOf({entryJson("a", 5, 2, 900), entryJson("b", 7, 1)}));
    check(!doc.corrupt && doc.entries.size() == 2, "an object with tracks reads");
    check(doc.entries[0].uri == "a" && doc.entries[0].lastPlayedMs == 5 && doc.entries[0].playCount == 2 &&
              doc.entries[0].title == "T" && doc.entries[0].artist == "A" && doc.entries[0].listenedMs == 900,
          "every field");
    const auto bare = historyParse("  \n[" + entryJson("a", 1, 1) + "]");
    check(!bare.corrupt && bare.entries.size() == 1, "a bare array reads, after leading space");
    const auto old = historyParse(R"({"tracks":[{"uri":"x","last":3,"count":1,"title":"Only"}]})");
    check(old.entries.size() == 1 && old.entries[0].artist.empty() && old.entries[0].listenedMs == 0, "an older entry has no artist or listened time");
    const auto skip = historyParse(R"({"tracks":[5,{"uri":""},{"uri":"  "},{"title":"x"},{"uri":"k","last":1}]})");
    check(skip.entries.size() == 1 && skip.entries[0].uri == "k", "non-objects and blank uris are skipped");
    const auto dup = historyParse(docOf({entryJson("a", 1, 1), entryJson("b", 2, 1), entryJson("a", 9, 4)}));
    check(dup.entries.size() == 2 && dup.entries[0].uri == "a" && dup.entries[0].lastPlayedMs == 9 && dup.entries[0].playCount == 4,
          "a repeated uri keeps its first place and its last values");
    check(historyParse(R"({"tracks":5})").entries.empty() && !historyParse(R"({"tracks":5})").corrupt, "tracks that is no array is empty");
    check(historyParse("{}").entries.empty() && !historyParse("{}").corrupt, "an object without tracks is empty");
    check(historyParse("[1,2]").entries.empty() && !historyParse("[1,2]").corrupt, "an array of non-objects is empty");
    check(historyParse("{not json").corrupt, "garbage is corrupt");
    check(historyParse("5").corrupt && historyParse("[1,").corrupt, "a bare number and a cut array are corrupt");
    check(historyParse("{not json").entries.empty(), "and holds nothing");
    const auto odd = historyParse(R"({"tracks":[{"uri":"k","last":"12","count":"3.9","title":7}]})");
    check(odd.entries.size() == 1 && odd.entries[0].lastPlayedMs == 12 && odd.entries[0].playCount == 3 && odd.entries[0].title == "7",
          "org.json's coercions: a numeric string reads as a number, a number as text");
}

void playing() {
    check(historyRecent("", 10).empty(), "nothing played yet: the recent list is empty");
    auto u = historyRecordPlay("", "a", "Song A", "Artist", 1000);
    auto e = historyEntry(u.state, "a");
    check(e && e->lastPlayedMs == 1000 && e->playCount == 1 && e->title == "Song A" && e->artist == "Artist" && e->listenedMs == 0,
          "a first play");
    u = historyRecordPlay(u.state, "b", "Song B", "", 1000);
    check(historyEntry(u.state, "b")->lastPlayedMs == 1001, "a second play in the same millisecond is stamped after the first");
    u = historyRecordPlay(u.state, "a", "Renamed", "", 500);
    e = historyEntry(u.state, "a");
    check(e->lastPlayedMs == 1002 && e->playCount == 2, "a clock that went backwards still stamps after the newest");
    check(e->title == "Renamed" && e->artist == "Artist", "the title is set, a blank artist keeps the old one");
    u = historyRecordPlay(u.state, "a", "Renamed", "  ", 5000);
    check(historyEntry(u.state, "a")->artist == "Artist" && historyEntry(u.state, "a")->lastPlayedMs == 5000, "a blank artist is not stored; a later clock wins");
    u = historyRecordPlay(u.state, "a", "Renamed", "New", 6000);
    check(historyEntry(u.state, "a")->artist == "New", "a given artist replaces");
    check(!historyEntry(u.state, "zzz"), "an unknown track has no entry");
    // The Kotlin PlayRecorderTest: six tracks played in turn are listed newest first.
    std::string s;
    for (int i = 1; i <= 6; ++i) s = historyRecordPlay(s, "content://media/" + std::to_string(i), "Track", "Artist", 1000).state;
    const auto recent = uris(historyRecent(s, 10));
    check(recent.size() == 6 && recent[0] == "content://media/6" && recent[5] == "content://media/1", "newest first");
    check(historyRecent(s, 2).size() == 2 && historyRecent(s, 2)[1].uri == "content://media/5", "limited");
    check(historyRecent(s, 0).empty() && historyRecent(s, -3).empty(), "a limit of 0 or less gives none");
    // The state is the file's own schema: parse what record wrote.
    check(historyParse(s).entries.size() == 6 && !historyParse(s).corrupt, "the state reads back");
}

void ordering() {
    const auto tied = docOf({entryJson("x", 5, 1), entryJson("y", 9, 1), entryJson("z", 5, 1), entryJson("w", 9, 1)});
    check(uris(historyRecent(tied, 10)) == std::vector<std::string>({"y", "w", "x", "z"}), "equal stamps keep their held order");
    const auto played = historyRecordPlay(tied, "x", "T", "", 2).state;
    check(uris(historyRecent(played, 1)) == std::vector<std::string>({"x"}), "a played track goes to the front");
}

void listening() {
    auto u = historyRecordPlay("", "a", "A", "", 1);
    u = historyAddListen(u.state, "a", 4000);
    check(historyEntry(u.state, "a")->listenedMs == 4000, "listened time adds");
    u = historyAddListen(u.state, "a", 500);
    check(historyEntry(u.state, "a")->listenedMs == 4500, "and adds again");
    u = historyAddListen(u.state, "a", 0);
    u = historyAddListen(u.state, "a", -9);
    check(historyEntry(u.state, "a")->listenedMs == 4500, "0 or less is ignored");
    const auto before = u.state;
    u = historyAddListen(u.state, "unknown", 100);
    check(u.state == before, "an unknown track gains nothing");
    check(historyEntry(u.state, "a")->playCount == 1 && historyEntry(u.state, "a")->lastPlayedMs == 1, "play count and stamp untouched");
}

void retention() {
    check(historyFileText("") == R"({"tracks":[]})", "an empty history writes an empty array");
    check(historyFileText(docOf({entryJson("u", 5, 1, 7)})) == R"({"tracks":[{"uri":"u","last":5,"count":1,"title":"T","artist":"A","ms":7}]})",
          "an entry is written in the file's field order");
    const auto played = historyRecordPlay("", "a/b", "Ti\"tle", "Ar", 12);
    check(played.file == R"({"tracks":[{"uri":"a\/b","last":12,"count":1,"title":"Ti\"tle","artist":"Ar","ms":0}]})", "escaped as org.json writes");
    check(played.file == historyFileText(played.state), "the update's file is the state's file text");

    // 300 tracks, stamps 1..300, only the first never played beyond the recent window: the recent 200 survive,
    // and so does the one played track that the recency cut alone would have dropped.
    std::vector<std::string> many;
    for (int i = 0; i < 300; ++i) many.push_back(entryJson("e" + std::to_string(i), i + 1, i == 0 ? 1 : 0));
    auto file = historyParse(historyFileText(docOf(many)));
    check(file.entries.size() == 201, "the recent 200 plus the played one");
    check(file.entries[0].uri == "e299" && file.entries[199].uri == "e100", "recent first, newest first");
    check(file.entries[200].uri == "e0", "then the most played that the recent cut dropped");

    // Plays order: count, then listened time, ties in held order; entries already kept do not repeat.
    many.clear();
    for (int i = 0; i < 300; ++i) {
        int count = 0;
        long long ms = 0;
        if (i == 0) count = 5;
        if (i == 1) { count = 5; ms = 100; }
        if (i == 2) count = 9;
        if (i == 299) count = 50;
        many.push_back(entryJson("e" + std::to_string(i), i + 1, count, ms));
    }
    file = historyParse(historyFileText(docOf(many)));
    check(file.entries.size() == 203, "recent 200 and three played that are older");
    check(file.entries[0].uri == "e299", "the recent order leads, even for a played track");
    check(file.entries[200].uri == "e2" && file.entries[201].uri == "e1" && file.entries[202].uri == "e0",
          "most played first, then more listened time first");

    // Everything played once: the 200 first held fill the played list; with the recent 200 that is all 300.
    many.clear();
    for (int i = 0; i < 300; ++i) many.push_back(entryJson("e" + std::to_string(i), i + 1, 1));
    file = historyParse(historyFileText(docOf(many)));
    check(file.entries.size() == 300, "ties among the played fall to the first held");
    check(file.entries[199].uri == "e100" && file.entries[200].uri == "e0" && file.entries[299].uri == "e99", "added in held order after the recent");

    // Within the cut: a short history is written whole, newest first.
    const auto small = historyParse(historyFileText(docOf({entryJson("o", 1, 0), entryJson("n", 2, 0)})));
    check(small.entries.size() == 2 && small.entries[0].uri == "n" && small.entries[1].uri == "o", "newest first on disk");
    check(kHistoryRetainRecent == 200 && kHistoryRetainPlayed == 200, "the cuts are 200 and 200");
    // A play keeps the state whole even when the file drops entries.
    std::string big = docOf(many);
    big = historyRecordPlay(big, "e0", "T", "", 100000).state;
    check(historyParse(big).entries.size() == 300 && historyEntry(big, "e0")->playCount == 2, "the state keeps what the file may drop");
}

void wire() {
    check(historyWire({}).empty(), "no entries is an empty wire");
    HistoryEntry a{"content://a", 12, 3, "Ti\x1etle", "Art\x1fist", 4500};
    HistoryEntry b{"b", -1, 0, "", "", 0};
    check(historyWire({a, b}) == "content://a\x1f" "12\x1f" "3\x1fTitle\x1f" "Artist\x1f" "4500\x1e"
                                 "b\x1f-1\x1f" "0\x1f\x1f\x1f" "0\x1e",
          "fields split by U+001F, entries ended by U+001E, the two dropped from text");
    const auto played = historyRecordPlay("", "u", "T\xC3\xA9", "A", 7).state;
    check(historyWire(historyRecent(played, 5)) == "u\x1f" "7\x1f" "1\x1fT\xC3\xA9\x1f" "A\x1f" "0\x1e", "UTF-8 kept as it is");
}
}  // namespace

int main() {
    reading();
    playing();
    ordering();
    listening();
    retention();
    wire();
    if (failures == 0) std::printf("library_history_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
