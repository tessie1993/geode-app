// Host-side checks for the playlist rules (core/library/PlaylistRules): the playlist file's JSON, the order the
// store lists in, the add, remove, move, rename, create and import edits (clamps and refusals included), the free
// name an import takes, and the one-time rename of files saved under an older name rule. The cases of the
// Kotlin MusicPlaylistStoreTest (legacy "old-name.json" holding "Road" moves to "Road.json" on the first use)
// are the migration cases here. No device, no files.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/library_playlist_test.cpp
//     core/library/PlaylistRules.cpp core/util/Json.cpp -o library_playlist_test && ./library_playlist_test
#include <cstdio>
#include <string>
#include <vector>

#include "library/PlaylistRules.hpp"

using namespace geode::library;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

using Names = std::vector<std::string>;

const StemFn kIdentity = [](const std::string& s) { return s; };
// A stand-in for the real stem: a name's letters and digits only, so "A/B" and "AB" share a file.
const StemFn kAlnum = [](const std::string& s) {
    std::string out;
    for (const char c : s) {
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) out += c;
    }
    return out;
};

PlaylistFolder folderOf(const std::vector<MusicPlaylist>& lists, const std::optional<std::string>& named = std::nullopt) {
    PlaylistFolder f;
    for (const auto& p : lists) f.fileTexts.push_back(playlistToJson(p));
    f.namedFileText = named;
    return f;
}

PlaylistEdit edit(PlaylistEditKind kind, const std::string& name) {
    PlaylistEdit e;
    e.kind = kind;
    e.name = name;
    return e;
}

void jsonRules() {
    check(playlistToJson({"Road", {"a"}}) == R"({"name":"Road","tracks":["a"]})", "written in the store's field order");
    check(playlistToJson({"E", {}}) == R"({"name":"E","tracks":[]})", "an empty playlist writes an empty array");
    check(playlistToJson({"a/b\"c", {"content://m/1"}}) == R"({"name":"a\/b\"c","tracks":["content:\/\/m\/1"]})",
          "slash and quote are escaped as org.json writes them");
    const auto p = playlistParse(R"({"name":"Road","tracks":["a","b"]})");
    check(p && p->name == "Road" && p->tracks == Names({"a", "b"}), "a playlist file reads");
    check(!playlistParse("[]"), "an array is no playlist");
    check(!playlistParse("not json"), "garbage is no playlist");
    check(!playlistParse(R"({"tracks":[]})"), "no name is no playlist");
    check(!playlistParse(R"({"name":"x"})"), "no tracks is no playlist");
    check(!playlistParse(R"({"name":"x","tracks":"a"})"), "tracks that is not an array is no playlist");
    const auto n = playlistParse(R"({"name":7,"tracks":[5,true,null]})");
    check(n && n->name == "7" && n->tracks == Names({"5", "true", "null"}), "org.json's getString spells other values");
    const auto trailing = playlistParse(R"({"name":"T","tracks":[]} and a sentence)");
    check(trailing && trailing->name == "T", "text after the object is ignored, as JSONObject(String) did");
}

void listing() {
    PlaylistFolder f;
    f.fileTexts = {playlistToJson({"beta", {}}), "broken", playlistToJson({"alpha", {"1"}}), playlistToJson({"Alpha", {"2"}}),
                   playlistToJson({"\xC3\x89t\xC3\xA9", {}}), playlistToJson({"zed", {}})};
    const auto list = playlistList(f.fileTexts);
    check(list.size() == 5, "the unreadable file is skipped");
    check(list[0].name == "alpha" && list[1].name == "Alpha", "equal lower-case names keep the order given");
    check(list[2].name == "beta", "then by lower-case name");
    // UTF-16 order: U+00E9 (a lower-cased E-acute) sorts after 'z'.
    check(list[3].name == "zed" && list[4].name == "\xC3\x89t\xC3\xA9", "a letter beyond ASCII sorts by code unit");
}

void adding() {
    auto out = playlistApply(folderOf({{"Road", {"a"}}}), [] { auto e = edit(PlaylistEditKind::AddTrack, "Road"); e.track = "b"; return e; }(), kIdentity);
    check(out.write && out.playlist.tracks == Names({"a", "b"}), "a new track is appended");
    check(out.json == R"({"name":"Road","tracks":["a","b"]})", "and written");
    out = playlistApply(folderOf({{"Road", {"a"}}}), [] { auto e = edit(PlaylistEditKind::AddTrack, "Road"); e.track = "a"; return e; }(), kIdentity);
    check(out.write && out.playlist.tracks == Names({"a"}), "a track already there is not added twice (still saved)");
    out = playlistApply(folderOf({}), [] { auto e = edit(PlaylistEditKind::AddTrack, "New"); e.track = "x"; return e; }(), kIdentity);
    check(out.write && out.playlist.name == "New" && out.playlist.tracks == Names({"x"}), "adding to a playlist that is not there makes it");
    out = playlistApply(folderOf({}, std::string("garbage")), [] { auto e = edit(PlaylistEditKind::AddTrack, "New"); e.track = "x"; return e; }(), kIdentity);
    check(!out.write && out.playlist.name == "New" && out.playlist.tracks.empty(), "an unreadable file of that name is never overwritten");
    out = playlistApply(folderOf({}, std::string(R"({"name":"New","tracks":[]})")), [] { auto e = edit(PlaylistEditKind::AddTrack, "New"); e.track = "x"; return e; }(), kIdentity);
    check(out.write, "a readable file of that name that no list entry matches counts as empty and is written");
}

void removing() {
    auto e = edit(PlaylistEditKind::RemoveTrack, "Road");
    e.track = "a";
    auto out = playlistApply(folderOf({{"Road", {"a", "b", "a"}}}), e, kIdentity);
    check(out.write && out.playlist.tracks == Names({"b"}), "every copy of the track goes");
    out = playlistApply(folderOf({{"Road", {"b"}}}), e, kIdentity);
    check(out.write && out.playlist.tracks == Names({"b"}), "a track not there leaves it, still saved");
    out = playlistApply(folderOf({}), e, kIdentity);
    check(out.write && out.playlist.tracks.empty() && out.playlist.name == "Road", "removing from a missing playlist writes it empty");
}

void moving() {
    const std::vector<MusicPlaylist> one = {{"P", {"a", "b", "c", "d"}}};
    const auto move = [&](int from, int to) {
        auto e = edit(PlaylistEditKind::Move, "P");
        e.from = from;
        e.to = to;
        return playlistApply(folderOf(one), e, kIdentity);
    };
    check(move(0, 2).playlist.tracks == Names({"b", "c", "a", "d"}), "forward");
    check(move(3, 0).playlist.tracks == Names({"d", "a", "b", "c"}), "back");
    check(move(0, 99).playlist.tracks == Names({"b", "c", "d", "a"}), "a target past the end goes to the end");
    check(move(3, -5).playlist.tracks == Names({"d", "a", "b", "c"}), "a target before the start goes to the start");
    check(move(1, 1).write && move(1, 1).playlist.tracks == Names({"a", "b", "c", "d"}), "to the same place still saves");
    check(!move(4, 0).write && move(4, 0).playlist.tracks == Names({"a", "b", "c", "d"}), "a source off the end changes nothing, nothing written");
    check(!move(-1, 0).write, "a negative source changes nothing");
    auto e = edit(PlaylistEditKind::Move, "Gone");
    const auto gone = playlistApply(folderOf({}), e, kIdentity);
    check(!gone.write && gone.playlist.name == "Gone" && gone.playlist.tracks.empty(), "a move in a missing playlist writes nothing");
}

void renaming() {
    const std::vector<MusicPlaylist> lists = {{"Old", {"a"}}, {"Taken", {}}};
    const auto rename = [&](const std::string& from, const std::string& to, const StemFn& stem) {
        auto e = edit(PlaylistEditKind::Rename, from);
        e.newName = to;
        return playlistApply(folderOf(lists), e, stem);
    };
    auto out = rename("Old", "New", kIdentity);
    check(out.ok && out.write && out.playlist.name == "New" && out.playlist.tracks == Names({"a"}), "renamed, tracks kept");
    check(out.json == R"({"name":"New","tracks":["a"]})" && out.deleteName == "Old", "written under the new name, the old file goes");
    out = rename("Old", "  New  ", kIdentity);
    check(out.ok && out.playlist.name == "New", "the new name is trimmed first");
    out = rename("Old", "O/ld", kAlnum);
    check(out.ok && out.write && out.deleteName.empty(), "the same file stem keeps the one file");
    check(!rename("Old", "", kIdentity).ok && !rename("Old", "   ", kIdentity).write, "a blank name is refused");
    check(!rename("Old", "Taken", kIdentity).ok, "a taken name is refused");
    check(!rename("Old", "Old", kIdentity).ok, "so is the same name");
    check(!rename("Nope", "New", kIdentity).ok, "a playlist that is not there cannot be renamed");
    check(rename("Old", "taken", kIdentity).ok, "names are compared exactly, not by case");
}

void creating() {
    auto e = edit(PlaylistEditKind::Create, "  Mix  ");
    e.tracks = {"a", "b", "a", "c", "b"};
    auto out = playlistApply(folderOf({}), e, kIdentity);
    check(out.write && out.playlist.name == "Mix" && out.playlist.tracks == Names({"a", "b", "c"}), "trimmed, each track once, in order");
    e.name = " \t ";
    out = playlistApply(folderOf({}), e, kIdentity);
    check(!out.write, "a blank name creates nothing");
    e.name = "Mix";
    out = playlistApply(folderOf({{"Mix", {"old"}}}), e, kIdentity);
    check(out.write && out.playlist.tracks == Names({"a", "b", "c"}), "creating over a name replaces it, as save did");
}

void importing() {
    auto e = edit(PlaylistEditKind::Import, "Mix");
    e.tracks = {"a", "a", "b"};
    auto out = playlistApply(folderOf({}), e, kIdentity);
    check(out.write && out.playlist.name == "Mix" && out.playlist.tracks == Names({"a", "a", "b"}), "free name used, tracks as given");
    out = playlistApply(folderOf({{"Mix", {}}, {"Mix (2)", {}}}), e, kIdentity);
    check(out.playlist.name == "Mix (3)", "taken names count up");
    check(playlistUniqueName({}, "X") == "X", "a free base is kept");
    check(playlistUniqueName({"X", "X (3)"}, "X") == "X (2)", "the first free suffix");
    check(playlistUniqueName({"x"}, "X") == "X", "taken names match exactly");
    check(playlistImportBase("Chosen", "file.m3u", "Untitled") == "Chosen", "the playlist's own name first");
    check(playlistImportBase("  ", "file.m3u", "Untitled") == "file.m3u", "then the file's name");
    check(playlistImportBase("", "", "Untitled") == "Untitled", "then the untitled name");
    check(playlistImportBase(" a ", "f", "u") == " a ", "a name that is not blank is kept as it is");
}

void migrating() {
    const std::string road = R"({"name":"Road","tracks":["a"]})";
    // The Kotlin test's case: the legacy file moves to Road.json on first use.
    auto plan = playlistMigrationPlan({{"old-name.json", road}}, kIdentity);
    check(plan.size() == 1 && plan[0].from == "old-name.json" && plan[0].to == "Road.json", "a legacy name is renamed to its stem");
    check(playlistMigrationPlan({{"Road.json", road}}, kIdentity).empty(), "a file already under its stem stays");
    check(playlistMigrationPlan({{"old.json", road}, {"Road.json", road}}, kIdentity).empty(), "an existing target is never replaced");
    check(playlistMigrationPlan({{"old.json", "garbage"}}, kIdentity).empty(), "an unreadable file is left");
    check(playlistMigrationPlan({{"old.txt", road}, {"old.JSON", road}, {"noext", road}}, kIdentity).empty(), "only .json files, by exact extension");
    plan = playlistMigrationPlan({{"a.json", road}, {"b.json", road}}, kIdentity);
    check(plan.size() == 1 && plan[0].from == "a.json", "two files for one stem: the first moves, the second finds the target taken");
    plan = playlistMigrationPlan({{"x.y.json", road}}, kIdentity);
    check(plan.size() == 1 && plan[0].to == "Road.json", "the stem is the name before the last dot");
    plan = playlistMigrationPlan({{"Road.v2.json", road}}, kIdentity);
    check(plan.size() == 1, "a dotted stem differs from the playlist's");
    plan = playlistMigrationPlan({{"old.json", R"({"name":"A/B","tracks":[]})"}}, kAlnum);
    check(plan.size() == 1 && plan[0].to == "AB.json", "the stem function decides the file name");
}
}  // namespace

int main() {
    jsonRules();
    listing();
    adding();
    removing();
    moving();
    renaming();
    creating();
    importing();
    migrating();
    if (failures == 0) std::printf("library_playlist_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
