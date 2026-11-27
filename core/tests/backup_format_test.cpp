// Host-side checks for the backup's format rules (core/backup): the section allowlist, device-local keys, the
// safe-path check, the manifest, the migration chain and the prefs JSON, with the same cases as BackupArchiveTest.kt
// plus hostile inputs for each security check.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/backup_format_test.cpp core/backup/*.cpp
//     core/util/Json.cpp -o backup_format_test && ./backup_format_test
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "backup/Backup.hpp"

using namespace geode::backup;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

Manifest sample() {
    Manifest m;
    m.format = kFormatId;
    m.version = kCurrentVersion;
    m.appVersionCode = 33;
    m.createdAtMs = 1000;
    m.sections = {"settings", "creative", "music"};
    m.entries = {{"presets/a.json", 17, std::string(64, 'a')}};
    return m;
}

PrefValue str(const std::string& s) {
    PrefValue v;
    v.type = PrefType::String;
    v.text = s;
    return v;
}
PrefValue whole(PrefType t, int64_t n) {
    PrefValue v;
    v.type = t;
    v.integer = n;
    return v;
}

void paths() {
    check(isSafePath("presets/a.json"), "plain path is safe");
    for (const char* bad : {"", "/a", "a//b", "a/./b", "a/../b", "..", "a\\b", "C:/a", "presets/", "/tmp/escaped.json",
                            "../escaped.json", "presets/../../escaped.json", ".", "a/..", "./a", "a/", "c:x", "Z:", "a:"}) {
        check(!isSafePath(bad), std::string("unsafe: ") + bad);
    }
    check(!isSafePath(std::string("a\0b", 3)), "NUL is unsafe");
    check(isSafePath("ab:c/d"), "a colon after the first two characters is fine");
    check(isSafePath("..a/b..") && isSafePath("a/.hidden") && isSafePath("a/..."), "dots inside a name are fine");
    // String.length counts UTF-16 units: 512 is the longest, 513 the first refused.
    check(isSafePath(std::string(512, 'x')), "512 characters ok");
    check(!isSafePath(std::string(513, 'x')), "513 characters refused");
    std::string wide;
    for (int i = 0; i < 256; ++i) wide += "\xC3\xA9";  // 256 two-byte characters: 256 units, 512 bytes
    check(isSafePath(wide + "x"), "257 units of accented text ok though 513 bytes");
    std::string astral;
    for (int i = 0; i < 257; ++i) astral += "\xF0\x9F\x8E\xB5";  // 257 supplementary characters: 514 units
    check(!isSafePath(astral), "surrogate pairs count twice");
    check(isSafePath("\xC3\xA9:/x") == false, "an accented first character then a colon is still the second unit");
}

void sections() {
    check(sectionOfPath("presets/a.json") == "creative", "presets creative");
    check(sectionOfPath("prefs/geode-mod.json") == "creative", "mod prefs creative");
    check(sectionOfPath("history.json") == "music", "history music");
    check(sectionOfPath("library.json") == "music", "library music");
    check(sectionOfPath("prefs/geode-favourites.json") == "music", "favourites music");
    check(sectionOfPath("prefs/geode-prefs.json") == "settings", "prefs settings");
    check(sectionOfPath("prefs/geode-wallpaper.json").empty(), "the retired wallpaper file is out");
    check(sectionOfPath("prefs/creative/geode-prefs.json") == "creative", "creative slice");
    check(sectionOfPath("presets/a.json.tmp").empty() && sectionOfPath("presets/a.corrupt").empty(), "tmp and corrupt out");
    check(sectionOfPath("prefs/geode-widget.json").empty(), "unknown prefs file out");
    check(sectionOfPath("session.json").empty(), "session out");
    for (const char* p : {"takes/t.json", "analysis/a.bin", "crash-latest.txt", "presets",
                          "library.json/x", "prefs/geode-prefs", "prefs/.json", "prefs/a/b/c.json",
                          "prefs/settings/geode-prefs.json", "prefs/music/geode-prefs.json", "prefs/nosuch/geode-prefs.json",
                          "prefs/creative/geode-mod.json"}) {
        check(sectionOfPath(p).empty(), std::string("outside the allowlist: ") + p);
    }
    for (const char* p : {"../presets/a.json", "presets/../x", "/presets/a.json", "presets\\a.json"}) {
        check(sectionOfPath(p).empty(), std::string("unsafe never maps: ") + p);
    }
    // Every old directory still restores.
    for (const char* d : {"templates", "editor", "projects", "project-takes", "project-templates", "palettes"}) {
        check(sectionOfPath(std::string(d) + "/x.json") == "creative", std::string("old dir ") + d);
    }
    check(isPrefsPath("prefs/a.json") && !isPrefsPath("prefs/a.txt") && !isPrefsPath("xprefs/a.json"), "isPrefsPath");
}

void keys() {
    check(!isPortableKey("geode-prefs", "gui_tutorial_seen"), "tutorial seen is device-local");
    check(!isPortableKey("geode-library", "roots"), "roots are device-local");
    check(isPortableKey("geode-prefs", "gui_reduced_motion"), "ordinary key portable");
    check(sectionOfKey("geode-prefs", "macro_bank") == "creative", "macro bank creative");
    for (const char* k : {"macro_knobs", "macro_locks", "macro_presets"}) {
        check(sectionOfKey("geode-prefs", k).empty(), std::string(k) + " is neither backed up nor restored");
    }
    for (const char* k : {"lfos", "adsrs", "reactions", "reactions_hand", "reactions_unread", "palette_stack", "customize_layer"}) {
        check(sectionOfKey("geode-prefs", k) == "creative", std::string("borrowed ") + k);
    }
    for (const char* k : {"perf_render_scale", "perf_fps_cap", "perf_march_scale", "perf_post_fx", "perf_adapt_frame_time", "perf_adaptive",
                          "perf_liquid_hot", "customize_open_groups", "customize_tab", "gui_setup_done",
                          "gui_tutorial_resume", "gui_tutorial_tips", "gui_tutorial_off", "gui_tutorial_done",
                          "gui_safety_acknowledged", "preset_mirror_uri"}) {
        check(sectionOfKey("geode-prefs", k).empty(), std::string("device-local ") + k);
    }
    check(sectionOfKey("geode-player", "picture_delay_ms").empty(), "picture delay");
    check(sectionOfKey("geode-background", "background_uri").empty(), "background uri");
    check(sectionOfKey("geode-player", "shuffle") == "settings", "player settings");
    check(sectionOfKey("geode-mod", "slot") == "creative", "mod file creative");
    check(sectionOfKey("geode-favourites", "x") == "music", "favourites music");
    check(sectionOfKey("geode-nothing", "x").empty(), "unknown file");
    check(sectionOfKey("geode-viz", "gui_tutorial_seen") == "settings", "same name in another file is portable");
}

void names() {
    check(defaultFileName(2026, 10, 30) == "beatrealms-backup-20261030.zip", "default file name");
    check(defaultFileName(2026, 1, 5) == "beatrealms-backup-20260105.zip", "zero padded");
    const auto l = layout();
    check(l.size() == 3 && l[0].id == "settings" && l[1].id == "creative" && l[2].id == "music",
          "layout order");
    check(l[1].prefsEntries.size() == 2 && l[1].prefsEntries[0].path == "prefs/geode-mod.json" &&
              l[1].prefsEntries[1].path == "prefs/creative/geode-prefs.json" && l[1].prefsEntries[1].file == "geode-prefs",
          "creative prefs entries: own file then slice");
    check(l[0].prefsEntries.size() == 6, "settings has six files");
    check(l[2].roots == std::vector<std::string>({"music-playlists", "smart-playlists", "library.json", "history.json"}),
          "roots");
    check(kMaxTotalBytes == 536870912 && kMaxEntries == 20000 && kMaxManifestBytes == 8388608 && kCurrentVersion == 2 &&
              kMaxPathLength == 512 && kArchiveSlackBytes == 67108864,
          "caps keep their values");
    check(isInsideStaging("/s", "/s/a", '/') && !isInsideStaging("/s", "/sx/a", '/') && !isInsideStaging("/s", "/s", '/') &&
              !isInsideStaging("/s/a", "/s/b", '/'),
          "inside staging");
    check(remainingBudget(100) == kMaxTotalBytes - 100, "remaining budget");
}

void manifest() {
    const Manifest m = sample();
    Manifest back;
    check(decodeManifest(encodeManifest(m), back).ok() && back == m, "manifest survives encode then decode");
    const std::string text = encodeManifest(m);
    check(text.find("\"format\": \"muzviz-backup\"") != std::string::npos, "org.json layout: key: value");
    check(text.find("\"path\": \"presets\\/a.json\"") != std::string::npos, "org.json escapes the slash");
    check(text.rfind("{\n  \"format\"", 0) == 0, "two-space indent, format first");

    Manifest out;
    check(decodeManifest("nope", out).problem == Problem::NotABackup, "not json");
    check(decodeManifest("", out).problem == Problem::NotABackup, "empty");
    check(decodeManifest("[1]", out).problem == Problem::NotABackup, "array root");
    Manifest other = m;
    other.format = "some-other-app";
    check(decodeManifest(encodeManifest(other), out).problem == Problem::NotABackup, "wrong format id");
    check(decodeManifest("{\"format\": 5}", out).problem == Problem::NotABackup, "numeric format");
    // Version gates: newer first, however different the rest looks.
    check(decodeManifest("{\"format\":\"muzviz-backup\",\"version\":3}", out).problem == Problem::NewerVersion,
          "newer, rest missing");
    Manifest newer = m;
    newer.version = kCurrentVersion + 1;
    newer.entries = {{"visuals-v2/x.json", 3, "ab"}};
    check(decodeManifest(encodeManifest(newer), out).problem == Problem::NewerVersion, "newer before paths looked at");
    check(decodeManifest("{\"format\":\"muzviz-backup\",\"version\":99999}", out).problem == Problem::NewerVersion, "far future");
    for (const char* v : {"0", "-1", "\"x\"", "null", "true"}) {
        check(decodeManifest(std::string("{\"format\":\"muzviz-backup\",\"version\":") + v + "}", out).problem ==
                  Problem::Damaged,
              std::string("version ") + v + " is damage");
    }
    check(decodeManifest("{\"format\":\"muzviz-backup\"}", out).problem == Problem::Damaged, "no version");
    check(decodeManifest("{\"format\":\"muzviz-backup\",\"version\":\"1\",\"appVersionCode\":1,\"createdAtMs\":1,"
                         "\"sections\":[],\"entries\":[]}",
                         out)
                  .ok() &&
              out.version == 1,
          "a version written as text still reads, as org.json does");
    const std::string head = "{\"format\":\"muzviz-backup\",\"version\":2,";
    check(decodeManifest(head + "\"appVersionCode\":1,\"createdAtMs\":1,\"sections\":[]}", out).problem == Problem::Damaged,
          "no entries");
    check(decodeManifest(head + "\"appVersionCode\":1,\"createdAtMs\":1,\"entries\":[]}", out).problem == Problem::Damaged,
          "no sections");
    check(decodeManifest(head + "\"createdAtMs\":1,\"sections\":[],\"entries\":[]}", out).problem == Problem::Damaged,
          "no app version");
    check(decodeManifest(head + "\"appVersionCode\":1,\"sections\":[],\"entries\":[]}", out).problem == Problem::Damaged,
          "no created at");
    check(decodeManifest(head + "\"appVersionCode\":1,\"createdAtMs\":1,\"sections\":{},\"entries\":[]}", out).problem ==
              Problem::Damaged,
          "sections not an array");
    check(decodeManifest(head + "\"appVersionCode\":1,\"createdAtMs\":1,\"sections\":[],\"entries\":[5]}", out).problem ==
              Problem::Damaged,
          "entry not an object");
    check(decodeManifest(head + "\"appVersionCode\":1,\"createdAtMs\":1,\"sections\":[],\"entries\":[{\"path\":\"a\","
                                "\"size\":1}]}",
                         out)
                  .problem == Problem::Damaged,
          "entry without sha");
    check(decodeManifest(head + "\"appVersionCode\":1,\"createdAtMs\":1,\"sections\":[],\"entries\":[{\"path\":\"a\","
                                "\"size\":\"x\",\"sha256\":\"b\"}]}",
                         out)
                  .problem == Problem::Damaged,
          "size not a number");
    check(decodeManifest(head + "\"appVersionCode\":1,\"createdAtMs\":8000000000,\"sections\":[\"settings\"],"
                                "\"entries\":[]}",
                         out)
                  .ok() &&
              out.createdAtMs == 8000000000LL,
          "created at is a long");
}

void migrations() {
    std::vector<int> path;
    check(migrationPath(1, 3, {2, 1}, path).ok() && path == std::vector<int>({1, 2}), "steps run in order up to the target");
    check(migrationPath(kCurrentVersion, kCurrentVersion, {1}, path).ok() && path.empty(), "current version runs nothing");
    check(migrationPath(1, kCurrentVersion, {1}, path).ok() && path == std::vector<int>({1}), "1 -> 2 is one step");
    check(migrationPath(1, 3, {1}, path).problem == Problem::Damaged, "a gap is damage");
    check(migrationPath(1, 3, {}, path).problem == Problem::Damaged, "no steps is damage");
}

void prefs() {
    PrefEntry flag{"b", {}};
    flag.value.type = PrefType::Boolean;
    flag.value.flag = true;
    PrefEntry real{"f", {}};
    real.value.type = PrefType::Float;
    real.value.real = static_cast<double>(0.1f);
    PrefEntry set{"set", {}};
    set.value.type = PrefType::StringSet;
    set.value.items = {"content://a", "content://b"};
    const std::vector<PrefEntry> values = {{"s", str("text")},
                                           {"i", whole(PrefType::Int, 7)},
                                           {"l", whole(PrefType::Long, 8000000000LL)},
                                           {"small_long", whole(PrefType::Long, 3)},
                                           flag,
                                           real,
                                           set};
    std::string text;
    check(encodePrefs(values, text).ok(), "encode");
    check(text.find("\"type\": \"float\"") != std::string::npos && text.find("0.10000000149011612") != std::string::npos,
          "a float is written as its double");
    std::vector<PrefEntry> back;
    check(decodePrefs(text, back).ok(), "decode");
    check(back.size() == values.size(), "same count");
    for (const auto& want : values) {
        bool found = false;
        for (const auto& got : back) {
            if (got.key == want.key) found = got.value == want.value;
        }
        check(found, "prefs keep their type and value: " + want.key);
    }
    for (const auto& got : back) {
        if (got.key == "small_long") check(got.value.type == PrefType::Long, "a small long stays a long");
        if (got.key == "f") check(got.value.type == PrefType::Float, "a float stays a float");
    }
    // Keys come out sorted, as toSortedMap wrote them.
    check(text.find("\"b\"") < text.find("\"f\"") && text.find("\"f\"") < text.find("\"i\"") &&
              text.find("\"i\"") < text.find("\"l\"") && text.find("\"l\"") < text.find("\"s\":"),
          "keys sorted");
    std::string empty;
    const std::vector<PrefEntry> none;
    check(encodePrefs(none, empty).ok() && empty == "{}", "no values is {}");
    const std::vector<PrefEntry> skipped = {PrefEntry{"x", {}}};
    check(encodePrefs(skipped, empty).ok() && empty == "{}", "an unsupported value is left out");
    PrefEntry nan{"n", {}};
    nan.value.type = PrefType::Float;
    nan.value.real = std::nan("");
    const std::vector<PrefEntry> nans = {nan};
    check(encodePrefs(nans, empty).problem == Problem::IoFailed, "a non-finite float cannot be written");
    PrefEntry unordered{"z", {}};
    unordered.value.type = PrefType::StringSet;
    unordered.value.items = {"b", "a", "b"};
    const std::vector<PrefEntry> unorderedList = {unordered};
    check(encodePrefs(unorderedList, text).ok() && text.find("\"a\"") < text.find("\"b\"") &&
              text.find("\"b\"") == text.rfind("\"b\""),
          "set written sorted and once");
    PrefEntry three{"w", {}};
    three.value.type = PrefType::Float;
    three.value.real = 3.0;
    const std::vector<PrefEntry> threes = {three};
    check(encodePrefs(threes, text).ok() && decodePrefs(text, back).ok() && back[0].value.real == 3.0, "whole float round trip");

    // Damaged input.
    for (const char* bad :
         {"not json", "", "[]", "{\"a\": 1}", "{\"a\": {\"value\": 1}}", "{\"a\": {\"type\": \"weird\", \"value\": 1}}",
          "{\"a\": {\"type\": \"int\"}}", "{\"a\": {\"type\": \"int\", \"value\": \"x\"}}",
          "{\"a\": {\"type\": \"boolean\", \"value\": 1}}", "{\"a\": {\"type\": \"stringSet\", \"value\": \"x\"}}",
          "{\"a\": {\"type\": \"float\", \"value\": true}}", "{\"a\": {\"type\": \"long\"}}"}) {
        check(decodePrefs(bad, back).problem == Problem::Damaged, std::string("damaged prefs: ") + bad);
    }
    check(decodePrefs("{\"a\": {\"type\": \"int\", \"value\": \"12\"}}", back).ok() && back[0].value.integer == 12,
          "org.json reads a text int");
    check(decodePrefs("{\"a\": {\"type\": \"float\", \"value\": 1e300}}", back).ok() && std::isinf(back[0].value.real),
          "toFloat overflows to infinity");
    check(decodePrefs("{\"a\": {\"type\": \"string\", \"value\": 5}}", back).ok() && back[0].value.text == "5",
          "getString coerces a number");
    check(decodePrefs("{}", back).ok() && back.empty(), "empty object");
    check(decodePrefs("{\"gui_safety_acknowledged\\u0000x\": {\"type\": \"boolean\", \"value\": true}}", back).problem ==
              Problem::Damaged,
          "a key holding a NUL is refused, so it cannot pass the filters as one key and be written as another");
}

}  // namespace

int main() {
    paths();
    sections();
    keys();
    names();
    manifest();
    migrations();
    prefs();
    if (failures == 0) std::printf("backup_format_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
