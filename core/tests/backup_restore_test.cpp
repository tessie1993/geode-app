// Host-side checks for the backup's migration and restore decisions (core/backup): the version 1 -> 2 split of the
// macros and reactions into Creative's slice, old backups still restoring, and the restore plan (which sections, which
// files, which prefs keys are removed and written, what the phone keeps).
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/backup_restore_test.cpp core/backup/*.cpp
//     core/util/Json.cpp -o backup_restore_test && ./backup_restore_test
#include <algorithm>
#include <cstdio>
#include <map>
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

PrefEntry text(const std::string& key, const std::string& v) {
    PrefEntry e{key, {}};
    e.value.type = PrefType::String;
    e.value.text = v;
    return e;
}
PrefEntry flag(const std::string& key, bool v) {
    PrefEntry e{key, {}};
    e.value.type = PrefType::Boolean;
    e.value.flag = v;
    return e;
}
PrefEntry number(const std::string& key, int64_t n) {
    PrefEntry e{key, {}};
    e.value.type = PrefType::Int;
    e.value.integer = n;
    return e;
}

std::string prefsText(const std::vector<PrefEntry>& v) {
    std::string out;
    encodePrefs(v, out);
    return out;
}

bool has(const std::vector<PrefEntry>& v, const std::string& key) {
    return std::any_of(v.begin(), v.end(), [&](const PrefEntry& e) { return e.key == key; });
}

std::string manifestV1(const std::vector<std::string>& sections, const std::vector<EntryRecord>& entries) {
    Manifest m;
    m.format = kFormatId;
    m.version = 1;
    m.appVersionCode = 20;
    m.createdAtMs = 5;
    m.sections = sections;
    m.entries = entries;
    return encodeManifest(m);
}

const std::string kV1Prefs = prefsText({flag("gui_reduced_motion", true), text("macro_bank", "bank"), text("lfos", "l"),
                                        text("adsrs", "a"), text("reactions", "r"), number("reactions_hand", 2),
                                        text("palette_stack", "ps"), text("customize_layer", "cl")});

void migration() {
    const std::vector<EntryRecord> entries = {{"presets/a.json", 3, "aa"}, {"prefs/geode-prefs.json", 10, "bb"}};
    const std::map<std::string, std::string> texts = {{"prefs/geode-prefs.json", kV1Prefs}};
    const FinishResult r = finishUnpack(manifestV1({"settings", "creative"}, entries), texts);
    check(r.failure.ok(), "a version 1 backup restores");
    check(r.manifest.version == 2, "walks up to the current version");
    check(r.writes.size() == 2 && r.writes[0].path == "prefs/geode-prefs.json" && r.writes[1].path == "prefs/creative/geode-prefs.json",
          "owner file then slice rewritten");
    std::vector<PrefEntry> owner, slice;
    check(decodePrefs(r.writes[0].bytes, owner).ok() && decodePrefs(r.writes[1].bytes, slice).ok(), "both rewritten files decode");
    check(owner.size() == 1 && has(owner, "gui_reduced_motion"), "settings keep only their own keys");
    check(slice.size() == 7 && has(slice, "macro_bank") && has(slice, "lfos") && has(slice, "adsrs") && has(slice, "reactions") &&
              has(slice, "reactions_hand") && has(slice, "palette_stack") && has(slice, "customize_layer"),
          "creative's slice holds the macros, LFOs, envelopes, reactions, palette stack and layer");
    check(slice[0].value.type != PrefType::Unsupported, "values keep their types");
    std::vector<std::string> paths;
    for (const auto& e : r.manifest.entries) paths.push_back(e.path);
    check(paths == std::vector<std::string>({"presets/a.json", "prefs/geode-prefs.json", "prefs/creative/geode-prefs.json"}),
          "entries: the others, then the owner file, then the slice");
    check(r.manifest.entries[1].size == static_cast<int64_t>(r.writes[0].bytes.size()) &&
              r.manifest.entries[2].size == static_cast<int64_t>(r.writes[1].bytes.size()),
          "entry sizes describe the rewritten files");
    check(r.sections == std::vector<std::string>({"settings", "creative"}) && r.createdAtMs == 5 && r.appVersionCode == 20,
          "the preview");

    // Only when the backup holds Creative: a Settings-only backup keeps its file as it was.
    const FinishResult settingsOnly = finishUnpack(manifestV1({"settings"}, {entries[1]}), texts);
    check(settingsOnly.failure.ok() && settingsOnly.writes.empty() && settingsOnly.manifest.entries == std::vector<EntryRecord>({entries[1]}) &&
              settingsOnly.manifest.version == 2,
          "a Settings-only backup is not given a Creative section");
    // Nothing to move.
    const std::map<std::string, std::string> plain = {{"prefs/geode-prefs.json", prefsText({flag("gui_reduced_motion", true)})}};
    const FinishResult nothing = finishUnpack(manifestV1({"settings", "creative"}, {entries[1]}), plain);
    check(nothing.failure.ok() && nothing.writes.empty() && nothing.manifest.entries.size() == 1, "no borrowed keys: nothing moves");
    // No owner file.
    const FinishResult noOwner = finishUnpack(manifestV1({"settings", "creative"}, {entries[0]}), {});
    check(noOwner.failure.ok() && noOwner.writes.empty(), "no owner file: nothing moves");
    // An owner file listed but not readable as prefs.
    const std::map<std::string, std::string> broken = {{"prefs/geode-prefs.json", "not json"}};
    check(finishUnpack(manifestV1({"settings", "creative"}, {entries[1]}), broken).failure.problem == Problem::Damaged,
          "a damaged owner file is damage");
    // A slice already present is replaced, not doubled.
    const std::vector<EntryRecord> withSlice = {entries[1], {"prefs/creative/geode-prefs.json", 2, "cc"}};
    const FinishResult replaced = finishUnpack(manifestV1({"settings", "creative"}, withSlice),
                                               {{"prefs/geode-prefs.json", kV1Prefs}, {"prefs/creative/geode-prefs.json", "{}"}});
    int slices = 0;
    for (const auto& e : replaced.manifest.entries) slices += e.path == "prefs/creative/geode-prefs.json";
    check(replaced.failure.ok() && slices == 1, "an existing slice entry is replaced");

    // A version 2 backup passes through untouched.
    Manifest v2;
    v2.format = kFormatId;
    v2.version = 2;
    v2.sections = {"settings", "creative"};
    v2.entries = {entries[1], {"prefs/creative/geode-prefs.json", 2, "cc"}};
    const FinishResult same = finishUnpack(encodeManifest(v2), {{"prefs/geode-prefs.json", kV1Prefs}, {"prefs/creative/geode-prefs.json", "{}"}});
    check(same.failure.ok() && same.writes.empty() && same.manifest == v2, "version 2 is not migrated");
    // Migrated entries must still pass the allowlist (the slice is Creative's).
    check(r.manifest.entries[2].path == "prefs/creative/geode-prefs.json", "the slice lands where the allowlist allows");
    // An older backup may carry a file the current allowlist refuses: refused after migration, as before.
    const FinishResult odd = finishUnpack(manifestV1({"settings"}, {{"visuals-v2/x.json", 1, "dd"}}), {});
    check(odd.failure.problem == Problem::UnsafePath, "version 1 paths are checked against today's allowlist");
    // Version 1 with every old directory still restores.
    const FinishResult old = finishUnpack(manifestV1({"creative"}, {{"editor/a.json", 1, "x"}, {"project-takes/t.json", 1, "x"}, {"templates/t.json", 1, "x"}}), {});
    check(old.failure.ok(), "old creative directories still restore");
    // Version 0 and a manifest the unpack never saw.
    check(finishUnpack("{\"format\":\"muzviz-backup\",\"version\":0}", {}).failure.problem == Problem::Damaged, "version 0");
    check(finishUnpack("nope", {}).failure.problem == Problem::NotABackup, "not a manifest");
}

RestoreRequest base() {
    RestoreRequest r;
    r.hasStaged = true;
    r.stagedSections = {"settings", "creative", "music"};
    r.requested = {"settings", "creative", "music"};
    r.stagedPrefs = {{"prefs/geode-prefs.json", prefsText({flag("gui_reduced_motion", true), flag("gui_tutorial_seen", true), text("macro_bank", "from-settings-file")})},
                     {"prefs/creative/geode-prefs.json", prefsText({text("macro_bank", "mb"), text("reactions", "r"), flag("gui_reduced_motion", false), text("macro_knobs", "old")})},
                     {"prefs/geode-mod.json", prefsText({number("slot", 1)})}};
    r.currentKeys = {{"geode-prefs", {"gui_reduced_motion", "gui_tutorial_seen", "macro_bank", "macro_knobs", "lfos", "perf_fps_cap", "other"}}};
    return r;
}

const SectionRestore* find(const RestorePlan& p, const std::string& id) {
    for (const auto& s : p.sections) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

void restore() {
    check(planRestore(RestoreRequest{}).failure.problem == Problem::IoFailed, "nothing staged");
    RestoreRequest req = base();
    const RestorePlan plan = planRestore(req);
    check(plan.failure.ok() && plan.sections.size() == 3, "three sections in order");
    check(plan.sections[0].id == "settings" && plan.sections[1].id == "creative" && plan.sections[2].id == "music",
          "sections in layout order");
    const SectionRestore* settings = find(plan, "settings");
    const SectionRestore* creative = find(plan, "creative");
    check(settings && creative, "settings and creative planned");
    check(settings->targets.empty() && settings->prefs.size() == 1 && settings->prefs[0].file == "geode-prefs", "settings has no files, one staged prefs file");
    const PrefsRestore& sp = settings->prefs[0];
    check(has(sp.put, "gui_reduced_motion") && !has(sp.put, "gui_tutorial_seen") && !has(sp.put, "macro_bank"),
          "settings writes its keys, never a device-local one nor creative's");
    check(std::find(sp.removeKeys.begin(), sp.removeKeys.end(), "gui_reduced_motion") != sp.removeKeys.end() &&
              std::find(sp.removeKeys.begin(), sp.removeKeys.end(), "other") != sp.removeKeys.end(),
          "settings removes its present keys first");
    for (const char* kept : {"gui_tutorial_seen", "macro_bank", "macro_knobs", "lfos", "perf_fps_cap"}) {
        check(std::find(sp.removeKeys.begin(), sp.removeKeys.end(), kept) == sp.removeKeys.end(), std::string("settings leaves ") + kept);
    }
    check(creative->prefs.size() == 2 && creative->prefs[0].file == "geode-mod" && creative->prefs[1].file == "geode-prefs" &&
              creative->prefs[1].stagedPath == "prefs/creative/geode-prefs.json",
          "creative: its own file, then its slice");
    const PrefsRestore& cp = creative->prefs[1];
    check(has(cp.put, "macro_bank") && has(cp.put, "reactions") && !has(cp.put, "gui_reduced_motion") && !has(cp.put, "macro_knobs"),
          "creative writes only its keys; the old knobs are never restored");
    check(cp.removeKeys == std::vector<std::string>({"macro_bank", "lfos"}), "creative removes only its present keys");
    check(creative->targets.size() == 7 && creative->targets[0] == "presets" && creative->ensureDirs == creative->targets, "creative's directories");
    check(creative->carryOver.empty(), "no cache under creative");
    const SectionRestore* music = find(plan, "music");
    check(music->targets == std::vector<std::string>({"music-playlists", "smart-playlists", "library.json", "history.json"}) &&
              music->ensureDirs.size() == 2 && music->prefs.empty(),
          "music: dirs then files, only the dirs are re-created, favourites file not staged");

    // Only what was asked for and staged.
    req.requested = {"creative", "bogus"};
    req.stagedSections = {"settings", "creative"};
    RestorePlan some = planRestore(req);
    check(some.failure.ok() && some.sections.size() == 1 && some.sections[0].id == "creative", "only requested sections");
    req.requested = {"music", "settings"};
    some = planRestore(req);
    check(some.sections.size() == 1 && some.sections[0].id == "settings", "a section the backup lacks is skipped");
    req.requested = {};
    check(planRestore(req).sections.empty(), "nothing asked, nothing planned");

    // A staged prefs file that is not there leaves the phone's keys alone.
    RestoreRequest absent = base();
    absent.stagedPrefs.erase("prefs/creative/geode-prefs.json");
    const RestorePlan a = planRestore(absent);
    check(find(a, "creative")->prefs.size() == 1, "an absent slice is skipped, so no key is removed");
    // A staged prefs file that no longer decodes stops the whole plan, before any file moves.
    RestoreRequest broken = base();
    broken.stagedPrefs["prefs/geode-mod.json"] = "not json";
    const RestorePlan b = planRestore(broken);
    check(b.failure.problem == Problem::Damaged && b.sections.empty(), "damaged staged prefs: nothing is planned");
    // Every Settings file in order.
    RestoreRequest all = base();
    all.requested = {"settings"};
    const auto sections = layout();  // held: a range-for over a temporary's member dangles
    for (const auto& info : sections[0].prefsEntries) all.stagedPrefs[info.path] = "{}";
    const RestorePlan s = planRestore(all);
    check(s.sections[0].prefs.size() == 6 && s.sections[0].prefs[5].file == "geode-background", "settings' six files in order");
    // An older backup's wallpaper options (retired with the wallpaper) are accepted and restore nothing.
    all.stagedPrefs["prefs/geode-wallpaper.json"] = "{}";
    const RestorePlan w = planRestore(all);
    check(w.failure.ok() && w.sections[0].prefs.size() == 6, "the retired wallpaper file is skipped");
    // A key the phone has in a device-local slot is never removed even when the backup holds a value for it.
    RestoreRequest local = base();
    local.requested = {"settings"};
    local.stagedPrefs["prefs/geode-prefs.json"] = prefsText({flag("gui_tutorial_seen", false), number("perf_fps_cap", 30)});
    const RestorePlan l = planRestore(local);
    check(l.sections[0].prefs[0].put.empty(), "device-local values in a backup are ignored");
}

}  // namespace

int main() {
    migration();
    restore();
    if (failures == 0) std::printf("backup_restore_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
