// Host-side checks for the saved play queue (core/library/SessionDoc): the file's JSON written and read back (the
// Kotlin PlayRecorderTest's "service alone keeps resumption current": six tracks, index 5, 42 s come back whole),
// the version gate (a newer app's file is refused, an absent version reads as 1), the index and position clamps
// (the ResumeSourceTest's index 9 and position -40), and which texts are no session. No device.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/library_session_test.cpp
//     core/library/SessionDoc.cpp core/util/Json.cpp -o library_session_test && ./library_session_test
#include <cstdio>
#include <string>
#include <vector>

#include "library/SessionDoc.hpp"

using namespace geode::library;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

std::vector<SavedTrack> album(int n) {
    std::vector<SavedTrack> out;
    for (int i = 1; i <= n; ++i) out.push_back({"content://media/" + std::to_string(i), "Track " + std::to_string(i), "Artist"});
    return out;
}

bool same(const std::vector<SavedTrack>& a, const std::vector<SavedTrack>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].uri != b[i].uri || a[i].title != b[i].title || a[i].artist != b[i].artist) return false;
    }
    return true;
}

void writing() {
    const auto w = sessionWrite({{{"u", "t", "a"}}, 0, 0});
    check(!w.clear && w.json == R"({"version":1,"tracks":[{"uri":"u","title":"t","artist":"a"}],"index":0,"positionMs":0})",
          "written in the file's field order");
    check(sessionWrite({{}, 3, 500}).clear && sessionWrite({{}, 3, 500}).json.empty(), "no tracks: the file is cleared, not written");
    check(sessionWrite({album(3), 9, 5}).json.find(R"("index":2,)") != std::string::npos, "an index past the end is written clamped");
    check(sessionWrite({album(3), -4, 5}).json.find(R"("index":0,)") != std::string::npos, "a negative index is written as 0");
    check(sessionWrite({album(3), 1, -40}).json.find(R"("positionMs":0})") != std::string::npos, "a negative position is written as 0");
    const auto q = sessionWrite({{{"a/b", "Q\"uote", "\xC3\xA9"}}, 0, 123456789012LL});
    check(q.json == "{\"version\":1,\"tracks\":[{\"uri\":\"a\\/b\",\"title\":\"Q\\\"uote\",\"artist\":\"\xC3\xA9\"}],\"index\":0,\"positionMs\":123456789012}",
          "escapes as org.json does, UTF-8 as it is, a long position whole");
}

void roundTrip() {
    // PlayRecorderTest: six tracks, the sixth playing at 0:42.
    const auto w = sessionWrite({album(6), 5, 42000});
    const auto back = sessionParse(w.json);
    check(back && same(back->tracks, album(6)) && back->index == 5 && back->positionMs == 42000, "a session reads back as written");
}

void clamps() {
    const auto high = sessionParse(R"({"version":1,"tracks":[{"uri":"a"},{"uri":"b"},{"uri":"c"},{"uri":"d"}],"index":9,"positionMs":5})");
    check(high && high->index == 3, "an index past the end reads as the last track");
    const auto low = sessionParse(R"({"tracks":[{"uri":"a"},{"uri":"b"}],"index":-7,"positionMs":-40})");
    check(low && low->index == 0 && low->positionMs == 0, "a negative index and position read as 0");
    const auto none = sessionParse(R"({"tracks":[{"uri":"a"},{"uri":"b"}]})");
    check(none && none->index == 0 && none->positionMs == 0, "no index or position reads as 0");
    const auto big = sessionParse(R"({"tracks":[{"uri":"a"}],"positionMs":99999999999})");
    check(big && big->positionMs == 99999999999LL, "a position past Int reads whole");
    const auto skipped = sessionParse(R"({"tracks":[{"uri":""},{"uri":"a"},5,{"uri":"b"}],"index":1})");
    check(skipped && skipped->tracks.size() == 2 && skipped->index == 1 && skipped->tracks[1].uri == "b",
          "the index is clamped against the tracks that were kept");
    const auto outside = sessionParse(R"({"tracks":[{"uri":""},{"uri":"a"},{"uri":"b"}],"index":2})");
    check(outside && outside->index == 1, "an index that named a skipped item is clamped to what is left");
    const auto strings = sessionParse(R"({"tracks":[{"uri":"a"},{"uri":"b"}],"index":"1","positionMs":"250"})");
    check(strings && strings->index == 1 && strings->positionMs == 250, "numeric strings read as numbers");
    const auto badNum = sessionParse(R"({"tracks":[{"uri":"a"},{"uri":"b"}],"index":"x","positionMs":true})");
    check(badNum && badNum->index == 0 && badNum->positionMs == 0, "what is no number takes the default");
}

void fields() {
    const auto s = sessionParse(R"({"tracks":[{"uri":"a"},{"uri":"b","title":"T","artist":"A"},{"uri":7,"title":null}]})");
    check(s && s->tracks.size() == 3, "tracks read");
    check(s && s->tracks[0].title.empty() && s->tracks[0].artist.empty(), "a missing title or artist is empty");
    check(s && s->tracks[1].title == "T" && s->tracks[1].artist == "A", "title and artist");
    check(s && s->tracks[2].uri == "7" && s->tracks[2].title == "null", "org.json's spelling of a number and a null");
    const auto blank = sessionParse(R"({"tracks":[{"uri":"a"},{"uri":"   "}]})");
    check(blank && blank->tracks.size() == 1, "a blank uri is skipped");
}

void gate() {
    check(sessionParse(R"({"version":1,"tracks":[{"uri":"a"}]})").has_value(), "this version reads");
    check(sessionParse(R"({"tracks":[{"uri":"a"}]})").has_value(), "no version reads as 1");
    check(sessionParse(R"({"version":0,"tracks":[{"uri":"a"}]})").has_value(), "an older version reads");
    check(!sessionParse(R"({"version":2,"tracks":[{"uri":"a"}]})"), "a newer app's file is refused");
    check(!sessionParse(R"({"version":"2","tracks":[{"uri":"a"}]})"), "so is a version written as text");
    check(sessionParse(R"({"version":"x","tracks":[{"uri":"a"}]})").has_value(), "a version that is no number reads as 1");
    check(sessionParse(R"({"version":1.9,"tracks":[{"uri":"a"}]})").has_value(), "1.9 is 1 as an Int");
    check(!sessionParse(R"({"version":2.5,"tracks":[{"uri":"a"}]})"), "2.5 is 2");
    check(!sessionParse(R"({"version":99,"tracks":[]})"), "the gate comes before the rest");
}

void refused() {
    check(!sessionParse(""), "empty text is no session");
    check(!sessionParse("garbage"), "garbage is no session");
    check(!sessionParse("[]"), "an array is no session");
    check(!sessionParse("{}"), "no tracks array is no session");
    check(!sessionParse(R"({"tracks":5})"), "tracks that is no array is no session");
    check(!sessionParse(R"({"tracks":[]})"), "no tracks is no session");
    check(!sessionParse(R"({"tracks":[{"uri":""},{"title":"x"},3]})"), "no track with a uri is no session");
    check(sessionParse(R"({"tracks":[{"uri":"a"}]} trailing)").has_value(), "text after the object is ignored, as JSONObject(String) did");
}
}  // namespace

int main() {
    writing();
    roundTrip();
    clamps();
    fields();
    gate();
    refused();
    if (failures == 0) std::printf("library_session_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
