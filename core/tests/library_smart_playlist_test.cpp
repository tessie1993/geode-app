// Host-side checks for the smart-playlist rules (core/library/SmartPlaylist): how a playlist's JSON reads (an
// unknown token drops only its rule), how it is written, and which tracks a rule set picks: text rules ignore
// case and trim the value, number rules compare as Kotlin's Float arithmetic did (IS and IS_NOT on the whole
// part), a blank value is ignored except for Favourite, match-all against match-any, and the limit. The Kotlin
// tree has no unit test for these; the cases are the rules of SmartPlaylistStore.kt written out. No device.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/library_smart_playlist_test.cpp
//     core/library/SmartPlaylist.cpp core/library/History.cpp core/util/Json.cpp -o library_smart_playlist_test
#include <cstdio>
#include <string>
#include <vector>

#include "library/History.hpp"
#include "library/SmartPlaylist.hpp"

using namespace geode::library;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

using Rows = std::vector<int>;

constexpr long long kDay = 86400;
constexpr long long kNowMs = 100 * kDay * 1000;

std::vector<SmartTrack> library() {
    SmartTrack a{"u/a", "Blue Monday", "New Order", "Power", "/music/rock", 90000, 100 * kDay - 2 * kDay - kDay / 2};
    SmartTrack b{"u/b", "Heroes", "David Bowie", "Heroes", "/music/pop", 360000, 100 * kDay - 10 * kDay};
    SmartTrack c{"u/c", "blue", "BLUE Artist", "", "/Music/Rock", 125000, 100 * kDay};
    return {a, b, c};
}

SmartRule rule(RuleField f, RuleOp op, const std::string& v) { return {f, op, v}; }

SmartPlaylist list(std::vector<SmartRule> rules, bool all = true, int limit = 0) {
    SmartPlaylist p;
    p.name = "L";
    p.rules = std::move(rules);
    p.matchAll = all;
    p.limit = limit;
    return p;
}

Rows resolve(const SmartPlaylist& p, const std::vector<std::string>& favs = {},
             const std::function<int(const std::string&)>& counts = [](const std::string&) { return 0; }) {
    return smartResolve(p, library(), counts, favs, kNowMs);
}

void text() {
    check(resolve(list({rule(RuleField::Title, RuleOp::Contains, "BLUE")})) == Rows({0, 2}), "contains ignores case");
    check(resolve(list({rule(RuleField::Title, RuleOp::Contains, "  blue  ")})) == Rows({0, 2}), "the value is trimmed");
    check(resolve(list({rule(RuleField::Title, RuleOp::Is, "blue")})) == Rows({2}), "is is the whole text, ignoring case");
    check(resolve(list({rule(RuleField::Title, RuleOp::IsNot, "blue")})) == Rows({0, 1}), "is-not is the rest");
    check(resolve(list({rule(RuleField::Artist, RuleOp::Contains, "bowie")})) == Rows({1}), "artist");
    check(resolve(list({rule(RuleField::Album, RuleOp::Is, "power")})) == Rows({0}), "album");
    check(resolve(list({rule(RuleField::Folder, RuleOp::Contains, "/MUSIC/rock")})) == Rows({0, 2}), "folder");
    check(resolve(list({rule(RuleField::Title, RuleOp::AtLeast, "1")})).empty(), "at-least means nothing for text");
    check(resolve(list({rule(RuleField::Title, RuleOp::AtMost, "1")})).empty(), "nor at-most");
    check(resolve(list({rule(RuleField::Album, RuleOp::IsNot, "power")})) == Rows({1, 2}), "an empty album is not 'power'");
    check(resolve(list({rule(RuleField::Title, RuleOp::Contains, "\xC3\x89")}, true)).empty(), "an accent is not matched by its plain letter");
    SmartTrack t{"u", "\xC3\x89t\xC3\xA9", "", "", "", 0, 0};
    check(smartResolve(list({rule(RuleField::Title, RuleOp::Is, "\xC3\xA9T\xC3\x89")}), {t}, [](const std::string&) { return 0; }, {}, 0) == Rows({0}),
          "Latin-1 letters fold their case");
}

void numbers() {
    check(resolve(list({rule(RuleField::DurationMinutes, RuleOp::AtLeast, "1.5")})) == Rows({0, 1, 2}), "duration at-least, 90 s is 1.5 min");
    check(resolve(list({rule(RuleField::DurationMinutes, RuleOp::AtMost, "2")})) == Rows({0}), "duration at-most (125 s is 2.08 min)");
    check(resolve(list({rule(RuleField::DurationMinutes, RuleOp::Is, "1")})) == Rows({0}), "is compares the whole part: 1.5 is 1");
    check(resolve(list({rule(RuleField::DurationMinutes, RuleOp::Is, "2.9")})) == Rows({2}), "and so does the wanted value: 2.9 is 2");
    check(resolve(list({rule(RuleField::DurationMinutes, RuleOp::IsNot, "2")})) == Rows({0, 1}), "is-not on the whole part");
    check(resolve(list({rule(RuleField::DurationMinutes, RuleOp::Contains, "1")})).empty(), "contains means nothing for a number");
    check(resolve(list({rule(RuleField::DurationMinutes, RuleOp::AtLeast, "abc")})).empty(), "a value that is no number matches nothing");
    check(resolve(list({rule(RuleField::DurationMinutes, RuleOp::AtLeast, " 6 ")})) == Rows({1}), "the number is trimmed");
    check(resolve(list({rule(RuleField::DurationMinutes, RuleOp::AtLeast, "6f")})) == Rows({1}), "Java's float suffix reads");
    check(resolve(list({rule(RuleField::DurationMinutes, RuleOp::AtLeast, "1e0")})) == Rows({0, 1, 2}), "an exponent reads");
    check(resolve(list({rule(RuleField::AddedDaysAgo, RuleOp::AtMost, "3")})) == Rows({0, 2}), "added 2.5 and 0 days ago");
    check(resolve(list({rule(RuleField::AddedDaysAgo, RuleOp::Is, "2")})) == Rows({0}), "2.5 days is day 2");
    check(resolve(list({rule(RuleField::AddedDaysAgo, RuleOp::AtLeast, "10")})) == Rows({1}), "added ten days ago");
    const auto counts = [](const std::string& uri) { return uri == "u/a" ? 5 : uri == "u/b" ? 1 : 0; };
    check(resolve(list({rule(RuleField::PlayCount, RuleOp::AtLeast, "1")}), {}, counts) == Rows({0, 1}), "play count at-least");
    check(resolve(list({rule(RuleField::PlayCount, RuleOp::Is, "0")}), {}, counts) == Rows({2}), "never played");
    check(resolve(list({rule(RuleField::PlayCount, RuleOp::Is, "NaN")}), {}, counts) == Rows({2}), "NaN reads, and its whole part is 0");
    check(resolve(list({rule(RuleField::PlayCount, RuleOp::AtLeast, "NaN")}), {}, counts).empty(), "nothing is at least NaN");
    check(resolve(list({rule(RuleField::PlayCount, RuleOp::AtMost, "Infinity")}), {}, counts) == Rows({0, 1, 2}), "anything is at most Infinity");
    check(resolve(list({rule(RuleField::PlayCount, RuleOp::Is, "99999999999")}), {}, counts).empty(), "a whole part past Int saturates, as toInt does");
}

void favourites() {
    const std::vector<std::string> favs = {"u/b"};
    check(resolve(list({rule(RuleField::Favourite, RuleOp::Is, "")}), favs) == Rows({1}), "a favourite rule needs no value");
    check(resolve(list({rule(RuleField::Favourite, RuleOp::IsNot, "")}), favs) == Rows({0, 2}), "is-not takes the rest");
    check(resolve(list({rule(RuleField::Favourite, RuleOp::Contains, "")}), favs) == Rows({1}), "any other op reads as is");
    check(resolve(list({rule(RuleField::Favourite, RuleOp::AtLeast, "zzz")}), favs) == Rows({1}), "and the value is not read");
}

void combining() {
    const auto blue = rule(RuleField::Title, RuleOp::Contains, "blue");
    const auto bowie = rule(RuleField::Artist, RuleOp::Contains, "bowie");
    check(resolve(list({blue, bowie}, true)).empty(), "match all");
    check(resolve(list({blue, bowie}, false)) == Rows({0, 1, 2}), "match any");
    check(resolve(list({}, true)) == Rows({0, 1, 2}), "no rules picks everything");
    check(resolve(list({rule(RuleField::Title, RuleOp::Contains, "   ")}, true)) == Rows({0, 1, 2}), "a blank value is ignored");
    check(resolve(list({rule(RuleField::Title, RuleOp::Contains, "")}, false)) == Rows({0, 1, 2}), "even when matching any");
    check(resolve(list({rule(RuleField::Title, RuleOp::Contains, " "), bowie}, true)) == Rows({1}), "the others still apply");
    check(resolve(list({}, true, 2)) == Rows({0, 1}), "limit takes the first");
    check(resolve(list({}, true, 99)) == Rows({0, 1, 2}), "a limit past the count");
    check(resolve(list({}, true, 0)) == Rows({0, 1, 2}), "0 is no limit");
    check(resolve(list({}, true, -1)) == Rows({0, 1, 2}), "nor is a negative one");
}

void files() {
    const auto p = smartParse(R"({"name":"Fast","matchAll":false,"limit":5,"rules":[
        {"field":"TITLE","op":"CONTAINS","value":"x"},
        {"field":"BPM","op":"IS","value":"1"},
        {"field":"ALBUM","op":"SOMEDAY","value":"1"},
        {"field":"FAVOURITE","op":"IS_NOT"},
        7,
        {"field":"DURATION_MINUTES","op":"AT_LEAST","value":"3"}]})");
    check(p && p->name == "Fast" && !p->matchAll && p->limit == 5, "the header reads");
    check(p && p->rules.size() == 3, "unknown field, unknown op and a non-object are dropped, the rest kept");
    check(p && p->rules[0].field == RuleField::Title && p->rules[0].op == RuleOp::Contains && p->rules[0].value == "x", "first rule");
    check(p && p->rules[1].field == RuleField::Favourite && p->rules[1].op == RuleOp::IsNot && p->rules[1].value.empty(), "a missing value is empty");
    check(p && p->rules[2].field == RuleField::DurationMinutes && p->rules[2].op == RuleOp::AtLeast, "last rule");
    const auto bare = smartParse(R"({"name":"N"})");
    check(bare && bare->matchAll && bare->limit == 0 && bare->rules.empty(), "defaults: match all, no limit, no rules");
    const auto odd = smartParse(R"({"name":"N","matchAll":"false","limit":"7","rules":5})");
    check(odd && !odd->matchAll && odd->limit == 7 && odd->rules.empty(), "org.json coerces a string flag and number; rules that is no array is none");
    const auto bad = smartParse(R"({"name":"N","matchAll":"maybe","limit":"x"})");
    check(bad && bad->matchAll && bad->limit == 0, "what cannot be coerced takes the default");
    check(!smartParse(R"({"matchAll":true})"), "no name, no playlist");
    check(!smartParse("[]") && !smartParse("{"), "not an object, no playlist");
    const auto named = smartParse(R"({"name":12})");
    check(named && named->name == "12", "a number name reads as text");

    SmartPlaylist w;
    w.name = "W/1";
    w.matchAll = false;
    w.limit = 3;
    w.rules = {rule(RuleField::AddedDaysAgo, RuleOp::IsNot, "a\"b"), rule(RuleField::PlayCount, RuleOp::AtMost, "2")};
    const std::string text = smartToJson(w);
    check(text == R"({"name":"W\/1","matchAll":false,"limit":3,"rules":[{"field":"ADDED_DAYS_AGO","op":"IS_NOT","value":"a\"b"},)"
                  R"({"field":"PLAY_COUNT","op":"AT_MOST","value":"2"}]})",
          "written in the store's order, tokens by enum name");
    const auto back = smartParse(text);
    check(back && back->name == w.name && back->rules.size() == 2 && back->rules[0].value == "a\"b", "what is written reads back");
    check(smartToJson(list({})) == R"({"name":"L","matchAll":true,"limit":0,"rules":[]})", "an empty rule list is an empty array");

    SmartPlaylist blank;
    blank.name = " \t";
    check(!smartSaveText(blank), "a blank name saves nothing");
    check(smartSaveText(list({})).has_value(), "a named playlist has text to write");

    const auto sorted = smartList({smartToJson([] { SmartPlaylist s; s.name = "b"; return s; }()), "junk",
                                   smartToJson([] { SmartPlaylist s; s.name = "A"; return s; }()),
                                   smartToJson([] { SmartPlaylist s; s.name = "a"; return s; }())});
    check(sorted.size() == 3 && sorted[0].name == "A" && sorted[1].name == "a" && sorted[2].name == "b", "listed by lower-case name, stable");
}

void fromText() {
    const std::string history = historyRecordPlay(historyRecordPlay("", "u/a", "A", "", 10).state, "u/a", "A", "", 20).state;
    SmartPlaylist p = list({rule(RuleField::PlayCount, RuleOp::AtLeast, "2")});
    const auto out = smartResolveText(smartToJson(p), library(), history, {}, kNowMs);
    check(out && *out == Rows({0}), "play counts come from the history document");
    check(!smartResolveText("junk", library(), history, {}, kNowMs), "an unreadable playlist resolves to nothing");
    const auto none = smartResolveText(smartToJson(p), library(), "", {}, kNowMs);
    check(none && none->empty(), "no history means no plays");
}
}  // namespace

int main() {
    text();
    numbers();
    favourites();
    combining();
    files();
    fromText();
    if (failures == 0) std::printf("library_smart_playlist_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
