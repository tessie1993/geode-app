// Host-side checks for the shell's prefs rules: core/shell/Behaviour (the media notification's buttons, the
// last-screen rule), core/shell/AutoVisuals (reading the visual playlist's JSON and the saved Auto settings)
// and core/shell/ExportFps (the frame rates an export offers and accepts, in terms of the encoder ladder), and
// the C ABI the app calls them through (core/api/geode_shell_api.h).
// Every case of the Kotlin NotificationButtonTest and ExportPresetFillTest is here with the same expected
// result; BehaviourRepositoryTest's "a stored LAST_USED reads as the default" is the last-screen case. The
// Auto visuals reading and the export fps rejection had no Kotlin unit test: those cases pin the Kotlin as read.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/shell_prefs_test.cpp core/shell/*.cpp
//     core/api/geode_shell_api.cpp core/util/Json.cpp -o shell_prefs_test && ./shell_prefs_test
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "api/geode_shell_api.h"
#include "output/EncoderLadder.hpp"
#include "shell/AutoVisuals.hpp"
#include "shell/Behaviour.hpp"
#include "shell/ExportFps.hpp"
#include "util/Json.hpp"

using namespace geode::shell;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

constexpr NotificationButton kShuffle = NotificationButton::Shuffle;
constexpr NotificationButton kRepeat = NotificationButton::Repeat;
constexpr NotificationButton kFavourite = NotificationButton::Favourite;

unsigned set(std::initializer_list<NotificationButton> b) {
    unsigned m = 0;
    for (NotificationButton x : b) m |= notificationBit(x);
    return m;
}

void favouriteHasRoom() {
    const auto ordered = notificationButtonsOrdered(set({kShuffle, kRepeat, kFavourite}));
    bool in = false;
    for (size_t i = 0; i < ordered.size() && static_cast<int>(i) < kNotificationRoom; ++i) in = in || ordered[i] == kFavourite;
    check(in, "a chosen favourite is among the buttons there is room for");
}

void defaultsKeepTheirOrder() {
    check(notificationButtonsOrdered(set({kRepeat, kShuffle})) == std::vector<NotificationButton>({kShuffle, kRepeat}), "shuffle, repeat");
    check(notificationButtonsOrdered(set({kRepeat, kFavourite, kShuffle})) ==
              std::vector<NotificationButton>({kFavourite, kShuffle, kRepeat}),
          "favourite, shuffle, repeat");
    check(notificationButtonsOrdered(0).empty(), "none");
}

void switchingAThirdOn() {
    check(notificationButtonsToggled(set({kShuffle, kRepeat}), kFavourite, true) == set({kFavourite, kShuffle}), "favourite on drops repeat");
    check(notificationButtonsToggled(set({kFavourite, kShuffle}), kRepeat, true) == set({kRepeat, kFavourite}), "repeat on drops shuffle");
    check(notificationButtonsToggled(set({kShuffle}), kRepeat, true) == set({kShuffle, kRepeat}), "room for two");
    check(notificationButtonsToggled(set({kShuffle, kRepeat}), kShuffle, true) == set({kShuffle, kRepeat}), "already on");
}

void switchingOff() {
    check(notificationButtonsToggled(set({kShuffle, kRepeat}), kShuffle, false) == set({kRepeat}), "shuffle off");
    check(notificationButtonsToggled(set({kFavourite}), kFavourite, false) == 0u, "favourite off");
    check(notificationButtonsToggled(set({kRepeat}), kShuffle, false) == set({kRepeat}), "off when not on");
}

void lastScreenIsNeverLastUsed() {
    check(behaviourLastScreen(StartScreen::LastUsed) == StartScreen::Player, "LAST_USED reads as the default");
    check(behaviourLastScreen(StartScreen::Visuals) == StartScreen::Visuals, "a screen stays");
    check(behaviourOpensOn(StartScreen::LastUsed, StartScreen::Studio) == StartScreen::Studio, "last used reopens the last screen");
    check(behaviourOpensOn(StartScreen::Library, StartScreen::Studio) == StartScreen::Library, "a chosen screen");
}

// Auto visuals

std::string resolve(const std::string& id) { return id == "old_silk" ? std::string("silk_a") : id; }

AutoVisualsState base() {
    AutoVisualsState s;
    s.randomEnabled = true;
    s.randomIntervalSec = 40;
    s.randomIncludeStyles = true;
    s.randomIncludePresets = false;
    s.randomIncludeMilk = false;
    s.playlistEnabled = true;
    s.playlistIntervalSec = 25;
    s.playlist = {{"fluid_a", std::nullopt, std::nullopt, "Fluid"}};
    return s;
}

void nothingStoredKeepsTheStateButTheTimerStartsOff() {
    const AutoVisualsState s = autoVisualsApply(base(), AutoVisualsStored{}, resolve);
    check(!s.randomEnabled, "the timer always starts off");
    check(s.randomIntervalSec == 40 && s.playlistIntervalSec == 25 && s.randomIncludeStyles && s.playlistEnabled, "state kept");
    check(s.playlist.size() == 1 && s.playlist[0].sceneId == "fluid_a", "playlist kept");
}

void storedValuesWinAndIntervalsClamp() {
    AutoVisualsStored st;
    st.randomIntervalSec = 2;
    st.playlistIntervalSec = 9999;
    st.randomIncludeStyles = false;
    st.randomIncludePresets = true;
    st.randomIncludeMilk = true;
    const AutoVisualsState s = autoVisualsApply(base(), st, resolve);
    check(s.randomIntervalSec == 5 && s.playlistIntervalSec == 300, "clamped to native's 5..300");
    check(!s.randomIncludeStyles && s.randomIncludePresets && s.randomIncludeMilk, "sources");
    st.randomIntervalSec = 5;
    st.playlistIntervalSec = 300;
    const AutoVisualsState edge = autoVisualsApply(base(), st, resolve);
    check(edge.randomIntervalSec == 5 && edge.playlistIntervalSec == 300, "edges stay");
}

void playlistReadFromJson() {
    AutoVisualsStored st;
    st.entriesJson = std::string(
        R"([{"sceneId":"old_silk","label":"Silk","presetName":"Calm","milkPath":"/m/a.milk"},)"
        R"({"sceneId":"","label":"gone"},{"label":"no scene"},"text",null,7,)"
        R"({"sceneId":"fluid_a"},{"sceneId":"x","label":"","presetName":"","milkPath":""},{"sceneId":42,"label":null}])");
    const AutoVisualsState s = autoVisualsApply(base(), st, resolve);
    check(s.playlist.size() == 4, "entries without a scene or not an object are skipped");
    if (s.playlist.size() != 4) return;
    check(s.playlist[0].sceneId == "silk_a" && s.playlist[0].label == "Silk", "scene resolved, label as written");
    check(s.playlist[0].presetName == std::string("Calm") && s.playlist[0].milkPath == std::string("/m/a.milk"), "preset and milk");
    check(s.playlist[1].sceneId == "fluid_a" && s.playlist[1].label == "fluid_a", "an empty label is the raw scene id");
    check(!s.playlist[1].presetName && !s.playlist[1].milkPath, "absent preset and milk");
    check(s.playlist[2].sceneId == "x" && s.playlist[2].label == "x" && !s.playlist[2].presetName && !s.playlist[2].milkPath,
          "empty strings are none");
    // org.json's optString spells a number or a null as text.
    check(s.playlist[3].sceneId == "42" && s.playlist[3].label == "null", "numbers and nulls read as text");
}

void resolveUsesTheRawIdForTheLabel() {
    AutoVisualsStored st;
    st.entriesJson = std::string(R"([{"sceneId":"old_silk"}])");
    const AutoVisualsState s = autoVisualsApply(base(), st, resolve);
    check(s.playlist.size() == 1 && s.playlist[0].sceneId == "silk_a" && s.playlist[0].label == "old_silk", "label is the raw id");
}

void badJsonIsAnEmptyListAndDisablesThePlaylist() {
    for (const char* bad : {"", "not json", "{\"sceneId\":\"a\"}", "[", "42"}) {
        AutoVisualsStored st;
        st.entriesJson = std::string(bad);
        st.playlistEnabled = true;
        const AutoVisualsState s = autoVisualsApply(base(), st, resolve);
        check(s.playlist.empty() && !s.playlistEnabled, std::string("bad json: ") + bad);
    }
    AutoVisualsStored st;
    st.entriesJson = std::string(R"([{"sceneId":"a","label":"A"}] and a sentence after)");
    check(autoVisualsApply(base(), st, resolve).playlist.size() == 1, "text after the array is ignored, as org.json reads it");
}

void playlistNeedsEntriesToBeEnabled() {
    AutoVisualsStored st;
    st.entriesJson = std::string("[]");
    st.playlistEnabled = true;
    check(!autoVisualsApply(base(), st, resolve).playlistEnabled, "stored on, but no entries");
    AutoVisualsState empty = base();
    empty.playlist.clear();
    check(!autoVisualsApply(empty, AutoVisualsStored{}, resolve).playlistEnabled, "state on, but no entries");
    st.entriesJson = std::string(R"([{"sceneId":"a"}])");
    st.playlistEnabled = false;
    check(!autoVisualsApply(base(), st, resolve).playlistEnabled, "stored off wins");
}

void playlistWritesAsTheKotlinDid() {
    std::vector<PlaylistEntry> e{{"silk_a", std::string("Calm"), std::nullopt, "Silk \"A\""}, {"fluid_a", std::nullopt, std::string("/m/a.milk"), "F"}};
    const std::string json = autoVisualsEntriesToJson(e);
    check(json == R"([{"sceneId":"silk_a","label":"Silk \"A\"","presetName":"Calm"},{"sceneId":"fluid_a","label":"F","milkPath":"\/m\/a.milk"}])",
          "key order sceneId, label, presetName, milkPath; org.json escapes: " + json);
    AutoVisualsStored st;
    st.entriesJson = json;
    const AutoVisualsState back = autoVisualsApply(base(), st, resolve);
    check(back.playlist.size() == 2 && back.playlist[0].label == "Silk \"A\"" && back.playlist[1].milkPath == std::string("/m/a.milk"), "round trip");
    check(autoVisualsEntriesToJson({}) == "[]", "empty");
}

const char* resolveC(const char* id) { return std::string(id) == "old_silk" ? "silk_a" : id; }

void autoVisualsThroughTheAbi() {
    geode_auto_playlist* p = geode_auto_playlist_read(
        R"([{"sceneId":"old_silk","label":"Silk","presetName":"Calm"},{"sceneId":"fluid_a","milkPath":"/m/a.milk"},"x"])", resolveC);
    check(geode_auto_playlist_count(p) == 2, "abi: two entries");
    auto field = [&](int i, int f) {
        const char* v = geode_auto_playlist_field(p, i, f);
        return v ? std::string(v) : std::string("<none>");
    };
    check(field(0, 0) == "silk_a" && field(0, 1) == "Calm" && field(0, 2) == "<none>" && field(0, 3) == "Silk", "abi: entry 0");
    check(field(1, 0) == "fluid_a" && field(1, 1) == "<none>" && field(1, 2) == "/m/a.milk" && field(1, 3) == "fluid_a", "abi: entry 1");
    check(field(2, 0) == "<none>" && field(0, 4) == "<none>" && field(-1, 0) == "<none>", "abi: out of range");
    geode_auto_playlist_destroy(p);
    geode_auto_playlist* none = geode_auto_playlist_read(nullptr, resolveC);
    check(geode_auto_playlist_count(none) == 0, "abi: no text, no entries");
    geode_auto_playlist_destroy(none);
    geode_auto_playlist* kept = geode_auto_playlist_read(R"([{"sceneId":"old_silk"}])", nullptr);
    check(geode_auto_playlist_count(kept) == 1 && std::string(geode_auto_playlist_field(kept, 0, 0)) == "old_silk", "abi: no resolver");
    geode_auto_playlist_destroy(kept);

    const char* fields[] = {"silk_a", "Calm", nullptr, "Silk \"A\"", "fluid_a", nullptr, "/m/a.milk", "F"};
    char json[256];
    const int n = geode_auto_playlist_to_json(fields, 2, json, static_cast<int>(sizeof json));
    check(n < static_cast<int>(sizeof json) &&
              std::string(json) ==
                  R"([{"sceneId":"silk_a","label":"Silk \"A\"","presetName":"Calm"},{"sceneId":"fluid_a","label":"F","milkPath":"\/m\/a.milk"}])",
          "abi: written as the Kotlin did");
    check(geode_auto_playlist_to_json(nullptr, 0, json, static_cast<int>(sizeof json)) == 2 && std::string(json) == "[]", "abi: empty");

    const int state[GEODE_AUTO_SAVED_FIELDS] = {1, 40, 1, 0, 0, 1, 25};
    int out[GEODE_AUTO_SAVED_FIELDS] = {};
    const unsigned char none_stored[GEODE_AUTO_SAVED_FIELDS] = {};
    geode_auto_visuals_apply(state, nullptr, none_stored, 1, out);
    check(out[GEODE_AUTO_SAVED_RANDOM_ON] == 0, "abi: the timer always starts off");
    check(out[1] == 40 && out[2] == 1 && out[3] == 0 && out[4] == 0 && out[5] == 1 && out[6] == 25, "abi: the state kept");
    geode_auto_visuals_apply(state, nullptr, none_stored, 0, out);
    check(out[GEODE_AUTO_SAVED_PLAYLIST_ON] == 0, "abi: no entries, no playlist");
    const int stored[GEODE_AUTO_SAVED_FIELDS] = {1, 2, 0, 1, 1, 0, 9999};
    const unsigned char all[GEODE_AUTO_SAVED_FIELDS] = {1, 1, 1, 1, 1, 1, 1};
    geode_auto_visuals_apply(state, stored, all, 3, out);
    check(out[0] == 0 && out[1] == 5 && out[2] == 0 && out[3] == 1 && out[4] == 1 && out[5] == 0 && out[6] == 300,
          "abi: stored values win, the timer stays off, intervals clamp");
}

// Export fps

void offeredRates() {
    check(exportFpsOptions() == std::vector<int>({24, 25, 30, 50, 60}), "24, 25, 30, 50, 60");
    for (int f : exportFpsOptions()) {
        check(f >= geode::output::kLadderMinFps && f <= geode::output::kLadderMaxFps, "inside the ladder's limits");
        check(exportFpsAllowed(geode::output::fallbackFps(f)), "the ladder's retry of an offered rate is offered");
        check(geode::output::fallbackFps(f) == f || f > geode::output::kLadderFloorFps, "only rates above the floor are halved");
    }
    for (int f : {-1, 0, 23, 26, 29, 31, 40, 59, 61, 120}) check(!exportFpsAllowed(f), "refused " + std::to_string(f));
    check(kExportDefaultFps == 30 && exportFpsAllowed(kExportDefaultFps), "the default is offered");
}

void everyPlatformPresetFpsIsOffered() {
    for (int f : {30, 60, 30, 30, 60, 60}) check(exportFpsAllowed(f), "preset fps " + std::to_string(f));
}

void readRejectsWhatIsNotOffered() {
    check(exportFpsRead(50, 30) == 50, "stored 50");
    check(exportFpsRead(45, 30) == 30, "stored 45 falls back");
    check(exportFpsRead(std::nullopt, 60) == 60, "nothing stored is the fallback");
    check(exportFpsRead(std::nullopt, 45) == 45, "fallback taken as is");
}

void behaviourThroughTheAbi() {
    // The ordinals are the shell's enum entries (data/BehaviourPrefs.kt): shuffle 0, repeat 1, favourite 2;
    // last used 0, player 1, library 2, visuals 3, studio 4.
    check(static_cast<int>(kShuffle) == 0 && static_cast<int>(kRepeat) == 1 && static_cast<int>(kFavourite) == 2, "button ordinals");
    check(static_cast<int>(StartScreen::LastUsed) == 0 && static_cast<int>(StartScreen::Studio) == 4, "screen ordinals");
    int out[4] = {-1, -1, -1, -1};
    check(geode_notification_buttons_ordered(0b111u, out, 4) == 3 && out[0] == 2 && out[1] == 0 && out[2] == 1 && out[3] == -1,
          "abi: favourite, shuffle, repeat");
    int one[1] = {-1};
    check(geode_notification_buttons_ordered(0b011u, one, 1) == 2 && one[0] == 0, "abi: short room");
    check(geode_notification_buttons_ordered(0u, nullptr, 0) == 0, "abi: none chosen");
    check(geode_notification_buttons_toggled(0b011u, 2, 1) == 0b101u, "abi: favourite on drops repeat");
    check(geode_notification_buttons_toggled(0b011u, 0, 0) == 0b010u, "abi: shuffle off");
    check(geode_behaviour_last_screen(0) == 1, "abi: last used reads as the player");
    check(geode_behaviour_last_screen(3) == 3, "abi: a real screen is kept");
    check(geode_behaviour_opens_on(0, 4) == 4, "abi: last used opens the last screen");
    check(geode_behaviour_opens_on(2, 4) == 2, "abi: a chosen screen wins");
}

void exportFpsThroughTheAbi() {
    int out[8] = {};
    check(geode_export_fps_options(out, 8) == 5, "five rates");
    check(std::vector<int>(out, out + 5) == exportFpsOptions(), "the same rates in the same order");
    int two[3] = {0, 0, -1};
    check(geode_export_fps_options(two, 2) == 5 && two[0] == 24 && two[1] == 25 && two[2] == -1, "short room: what fits");
    check(geode_export_fps_options(nullptr, 0) == 5, "a count only");
    check(geode_export_fps_read(50, 30) == 50, "abi: stored 50");
    check(geode_export_fps_read(45, 30) == 30, "abi: stored 45 falls back");
    check(geode_export_fps_read(120, 60) == 60, "abi: stored 120 falls back");
}

int fpsOf(const std::string& t, int fallback) {
    const auto v = geode::json::parse(t);
    return exportFpsFromJson(v ? &*v : nullptr, fallback);
}

void jsonRejectionMatchesOptInt() {
    check(fpsOf(R"({"fps":50})", 30) == 50, "50");
    check(fpsOf(R"({"fps":45})", 30) == 30, "45 refused");
    check(fpsOf(R"({"fps":"60"})", 30) == 60, "a numeric string coerces as optInt does");
    check(fpsOf(R"({"fps":60.7})", 30) == 60, "a fraction truncates");
    check(fpsOf(R"({"fps":null})", 24) == 24, "null is the fallback");
    check(fpsOf(R"({"fps":true})", 24) == 24, "a bool is the fallback");
    check(fpsOf(R"({})", 25) == 25, "missing is the fallback");
    check(exportFpsFromJson(nullptr, 25) == 25, "no object is the fallback");
}

}  // namespace

int main() {
    favouriteHasRoom();
    defaultsKeepTheirOrder();
    switchingAThirdOn();
    switchingOff();
    lastScreenIsNeverLastUsed();
    nothingStoredKeepsTheStateButTheTimerStartsOff();
    storedValuesWinAndIntervalsClamp();
    playlistReadFromJson();
    resolveUsesTheRawIdForTheLabel();
    badJsonIsAnEmptyListAndDisablesThePlaylist();
    playlistNeedsEntriesToBeEnabled();
    playlistWritesAsTheKotlinDid();
    offeredRates();
    everyPlatformPresetFpsIsOffered();
    readRejectsWhatIsNotOffered();
    jsonRejectionMatchesOptInt();
    exportFpsThroughTheAbi();
    behaviourThroughTheAbi();
    autoVisualsThroughTheAbi();
    if (failures == 0) std::printf("shell_prefs_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
