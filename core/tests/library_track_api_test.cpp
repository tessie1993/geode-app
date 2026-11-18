// The C ABI of the track library (core/api/geode_track_library_api.cpp): the batches the shell packs, the records it
// unpacks. The rules behind each call are tested in library_tracks_test.cpp; this holds the packing.
#include <cstdio>
#include <string>
#include <vector>

#include "api/geode_api.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

constexpr char US = '\x1F';
constexpr char RS = '\x1E';

// One record: its fields split by U+001F, then U+001E.
std::vector<std::vector<std::string>> records(const char* wire) {
    std::vector<std::vector<std::string>> out;
    std::vector<std::string> cur;
    std::string field;
    for (const char* p = wire; p && *p; ++p) {
        if (*p == US) {
            cur.push_back(field);
            field.clear();
        } else if (*p == RS) {
            cur.push_back(field);
            field.clear();
            if (cur.size() == 1 && cur[0].empty()) cur.clear();
            out.push_back(cur);
            cur.clear();
        } else {
            field.push_back(*p);
        }
    }
    return out;
}

void addAndRead() {
    // Two tracks in, one already held under the same file and size: dropped; sorted by lower-case title.
    const std::string held = "{\"version\":3,\"tracks\":[{\"uri\":\"u1\",\"title\":\"banana\",\"fileName\":\"B.mp3\","
                             "\"sizeBytes\":10}]}";
    const char* added[] = {"u2", "Apple", "", "0", "", "", "0", "0", "", "",      "0",
                           "u3", "Banana", "", "0", "", "", "0", "0", "", "b.MP3", "10"};
    const auto edit = records(geode_track_library_add(held.c_str(), added, 2));
    check(edit.size() == 3, "a head record and two tracks");
    if (edit.size() == 3) {
        check(edit[0][0] == "1" && edit[0][1] == "0", "an add is a change of a readable library");
        check(edit[0].size() == 3 && edit[0][2].find("\"version\":3") != std::string::npos, "the json to write");
        check(edit[1].size() == GEODE_TRACK_FIELDS && edit[1][1] == "Apple", "the first track is Apple");
        check(edit[2][0] == "u1", "the held banana stays");
    }

    const auto bad = records(geode_track_library_read("garbage"));
    check(bad.size() == 1 && bad[0][0] == "0" && bad[0][1] == "1", "garbage reads as corrupt with no tracks");
    const auto blank = records(geode_track_library_read(""));
    check(blank.size() == 1 && blank[0][1] == "0", "blank text is an empty library, not corrupt");
}

void infoAndFresh() {
    const std::string held = "{\"tracks\":[{\"uri\":\"u1\",\"title\":\"1234\",\"artist\":\"\",\"album\":\"Al\"}]}";
    const auto info = records(geode_track_library_save_info(held.c_str(), "u1", "T", "A", "B", "G", 2020, 5, "c"));
    check(info.size() == 2 && info[1][1] == "T" && info[1][2] == "A" && info[1][6] == "2020" && info[1][7] == "5",
          "saved info reaches the track");

    const char* fresh[] = {"u1", "Real", "Someone", "nope", "X", "Y"};
    const auto edit = records(geode_track_library_apply_fresh(held.c_str(), fresh, 2));
    check(edit.size() == 2 && edit[0][0] == "1" && edit[1][1] == "Real" && edit[1][2] == "Someone", "a fresh title applies");
    check(edit[1][4] == "Al", "the other info is kept");
    const char* same[] = {"u1", "1234", ""};
    const auto none = records(geode_track_library_apply_fresh(held.c_str(), same, 1));
    check(none[0][0] == "0", "a title that matches changes nothing");
}

void fallbacks() {
    // Tags as the reader gave them (NULL for none), uri, display name, last path segment.
    const char* files[] = {" Song ", nullptr, nullptr, nullptr, "2019-05-01", "3/12", "u1", "d.mp3", nullptr,
                           nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "u2", "", "dir/x.flac"};
    const long long sizes[] = {42, 0};
    const auto tracks = records(geode_track_tag_fallbacks(files, sizes, 2, "Untitled"));
    check(tracks.size() == 2, "a track per file");
    if (tracks.size() == 2) {
        check(tracks[0][1] == "Song" && tracks[0][6] == "2019" && tracks[0][7] == "3" && tracks[0][10] == "42",
              "the tags are trimmed, the year and track number read");
        check(tracks[1][1] == "x", "no title: the path segment without its extension");
    }
    const char* none[] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "u3", "", nullptr};
    const long long zero[] = {0};
    const auto untitled = records(geode_track_tag_fallbacks(none, zero, 1, "Untitled"));
    check(untitled.size() == 1 && untitled[0][1] == "Untitled", "nothing at all is the localized untitled");
}

void walking() {
    const char* names[] = {".hidden", "song.MP3", "notes.txt", nullptr, "Sub", "clip"};
    const char* mimes[] = {nullptr, nullptr, nullptr, nullptr, nullptr, "audio/ogg"};
    const int dirs[] = {0, 0, 0, 0, 1, 0};
    int out[6] = {};
    check(geode_track_walk_actions(names, mimes, dirs, 6, 0, out) == 6, "one action per entry");
    const int want[] = {0, 2, 0, 0, 1, 2};
    for (int i = 0; i < 6; ++i) check(out[i] == want[i], "walk action " + std::to_string(i));
    int deep[1] = {};
    const char* folder[] = {"Sub"};
    const int isDir[] = {1};
    geode_track_walk_actions(folder, nullptr, isDir, 1, 8, deep);
    check(deep[0] == 0, "a folder past depth 8 is skipped");
}

void refreshAndRows() {
    const char* titles[] = {"12345", "", "Song", "", "99", "Someone", "99", ""};
    int flags[4] = {};
    check(geode_track_title_refresh(titles, 4, flags) == 4, "one flag per track");
    check(flags[0] == 1 && flags[1] == 0 && flags[2] == 0 && flags[3] == 1, "numeric titles are flagged");

    const char* rows[] = {"T1", "<unknown>", nullptr, "/music/a/one.mp3", "T2", "B", "C", "noslash"};
    const long long durations[] = {215000, 3000};
    const char* names[] = {"Unknown title", "Unknown artist", "Unknown album"};
    const auto kept = records(geode_track_device_rows(rows, durations, 2, 30, names));
    check(kept.size() == 2, "one kept row and the count");
    if (kept.size() == 2) {
        check(kept[0][0] == "0" && kept[0][1] == "T1" && kept[0][2] == "Unknown artist" && kept[0][3] == "Unknown album" &&
                  kept[0][4] == "/music/a",
              "words and folder");
        check(kept[1][0] == "1", "one row left out");
    }
    const auto all = records(geode_track_device_rows(rows, durations, 2, 0, names));
    check(all.size() == 3 && all[1].size() == 5 && all[1][4].empty(), "off keeps both; a row with no folder has an empty one");
}

void resolving() {
    const char* names[] = {"A.mp3", "gone.mp3", "dup.mp3"};
    const char* library[] = {"a.MP3", "ua", "dup.mp3", "u1", "DUP.mp3", "u2", "", "u3"};
    const auto r = records(geode_track_resolve_entries(names, 3, library, 4));
    check(r.size() == 3, "three records");
    if (r.size() == 3) {
        check(r[0] == std::vector<std::string>({"ua", "u1"}), "uris: the first in library order");
        check(r[1] == std::vector<std::string>({"1"}), "the missing entry's index");
        check(r[2] == std::vector<std::string>({"2"}), "the ambiguous entry's index");
    }
    const auto none = records(geode_track_resolve_entries(nullptr, 0, nullptr, 0));
    check(none.size() == 3 && none[0].empty() && none[1].empty() && none[2].empty(), "nothing in, three empty records");
}

}  // namespace

int main() {
    addAndRead();
    infoAndFresh();
    fallbacks();
    walking();
    refreshAndRows();
    resolving();
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
