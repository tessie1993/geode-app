// Host-side checks for the backup's C ABI (core/api/geode_backup_api.cpp): that each plan crosses as the list
// geode_api.h describes, a whole export, unpack and restore walked through the calls the shell makes, and a
// malformed list answering problem 6. The rules themselves are in backup_format_test, backup_archive_test and
// backup_restore_test.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/backup_api_test.cpp
//     core/api/geode_backup_api.cpp core/backup/*.cpp core/util/Json.cpp -o backup_api_test && ./backup_api_test
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

using List = std::vector<std::string>;

// An answer copied out, since the next call reuses it.
// The answer of a call that wrote its length to `n`; the call is the first argument, so n is read after it ran.
List take(const char* const* items, const int& n) {
    List out;
    for (int i = 0; i < n; ++i) out.push_back(items[i]);
    return out;
}

List call(const char* const* (*fn)(const char* const*, int, int*), const List& in) {
    std::vector<const char*> p;
    for (const auto& s : in) p.push_back(s.c_str());
    int n = 0;
    const char* const* out = fn(p.data(), static_cast<int>(p.size()), &n);
    return take(out, n);
}

const std::string kDigest(64, 'a');

void layoutCrosses() {
    int n = 0;
    const List l = take(geode_backup_layout(&n), n);
    check(l.size() > 5 && l[0] == "manifest.json" && l[1] == "8388608" && l[2] == "603979776" && l[3] == "20000" && l[4] == "3",
          "the manifest's path and the three caps, then three sections");
    check(l[5] == "settings" && l[6] == "6" && l[7] == "geode-prefs" && l[8] == "prefs/geode-prefs.json",
          "settings first, its six prefs files each with its archive path");
    size_t at = 7 + 12;
    check(l[at] == "0", "settings has no roots");
    bool creative = false;
    for (size_t i = 0; i + 1 < l.size(); ++i) creative = creative || (l[i] == "prefs/creative/geode-prefs.json");
    check(creative, "creative's slice of the settings file is listed");
}

void exportToRestore() {
    // An export: two prefs files, one with a float and a set; one file found under Creative.
    const List prefs = {"2",
                        "geode-prefs", "3", "gui_opacity", "f", "0.5", "0", "macro_bank", "s", "bank", "0", "gui_setup_done", "b", "1", "0",
                        "geode-favourites", "1", "favs", "t", "", "2", "b", "a"};
    const List found = {"creative", "presets/a.json", "creative", "presets/a.json.tmp"};
    std::vector<const char*> p, f;
    for (const auto& s : prefs) p.push_back(s.c_str());
    for (const auto& s : found) f.push_back(s.c_str());
    int n = 0;
    const List plan = take(geode_backup_plan_export(p.data(), static_cast<int>(p.size()), f.data(), static_cast<int>(f.size()), &n), n);
    check(plan.size() > 4 && plan[0] == "0" && plan[1].empty() && plan[2] == "536870912", "an export plan with no problem and the byte cap");
    check(std::stoi(plan[3]) == static_cast<int>((plan.size() - 4) / 3), "the source count matches the list");
    bool sawFile = false, sawTmp = false, sawFav = false;
    for (size_t i = 4; i + 2 < plan.size(); i += 3) {
        if (plan[i] == "presets/a.json") sawFile = plan[i + 1] == "0";
        if (plan[i] == "presets/a.json.tmp") sawTmp = true;
        if (plan[i] == "prefs/geode-favourites.json") sawFav = plan[i + 1] == "1" && plan[i + 2].find("\"favs\"") != std::string::npos;
    }
    check(sawFile && !sawTmp && sawFav, "the file is opened by the shell, the temp is left out, the prefs travel inline");
    check(plan[4] == "prefs/geode-prefs.json", "prefs files lead their section");

    // The manifest, from what the shell wrote.
    const List written = {"presets/a.json", "10", kDigest, "prefs/geode-prefs.json", "20", kDigest};
    std::vector<const char*> w;
    for (const auto& s : written) w.push_back(s.c_str());
    const List done = take(geode_backup_finish_export(w.data(), static_cast<int>(w.size()), 33, 1000, &n), n);
    check(done.size() == 3 && done[0] == "0" && done[2].find("\"appVersionCode\": 33") != std::string::npos, "the manifest text");
    const List bad = call([](const char* const* a, int c, int* n2) { return geode_backup_finish_export(a, c, 1, 1, n2); },
                          List{"presets/a.json", "10", "not hex"});
    check(bad[0] == "6", "a digest that is not hex is problem 6");
    const List tooBig = call([](const char* const* a, int c, int* n2) { return geode_backup_finish_export(a, c, 1, 1, n2); },
                             List{"presets/a.json", "600000000", kDigest});
    check(tooBig[0] == "5", "over the byte cap is problem 5");

    // Unpack: the zip lists the file and the prefs file, then the manifest.
    List req = {"3", "1", "0", done[2], "presets/a.json", "0", "prefs/geode-prefs.json", "0", "manifest.json", "0"};
    const List un = call(geode_backup_plan_unpack, req);
    check(un[0] == "0" && un[2] == "0", "an unpack plan with no problem and nothing to raise after");
    check(un[4] == "1" && un[5] == "prefs/geode-prefs.json", "the staged prefs file is read for the finish");
    check(un[6] == "2" && un[7] == "0" && un[8] == "presets/a.json" && un[9] == "10" && un[10] == kDigest && un[11] == "536870912",
          "the first item to extract with its promise and the whole budget");
    check(un[12] == "1" && un[16] == "536870902", "the next item's budget is what the first left");

    // One item checked: right, wrong size, cut at the budget.
    const List ok = call(geode_backup_check_extracted, List{"presets/a.json", "10", kDigest, "536870912", "10", kDigest, "0"});
    check(ok[0] == "0", "an extracted item that matches");
    const List wrong = call(geode_backup_check_extracted, List{"presets/a.json", "10", kDigest, "536870912", "11", kDigest, "0"});
    check(wrong[0] == "4", "a size that differs is damage");
    const List cut = call(geode_backup_check_extracted, List{"presets/a.json", "10", kDigest, "536870912", "536870913", kDigest, "1"});
    check(cut[0] == "5", "a read cut at the budget is too large");

    // Finish, on a version 1 backup holding the macros in Settings' file.
    const std::string v1 =
        "{\"format\":\"muzviz-backup\",\"version\":1,\"appVersionCode\":20,\"createdAtMs\":5,\"sections\":[\"settings\",\"creative\"],"
        "\"entries\":[{\"path\":\"presets/a.json\",\"size\":3,\"sha256\":\"aa\"},{\"path\":\"prefs/geode-prefs.json\",\"size\":9,\"sha256\":\"bb\"}]}";
    const std::string prefsText =
        "{\"macro_bank\":{\"type\":\"string\",\"value\":\"bank\"},\"gui_opacity\":{\"type\":\"float\",\"value\":0.5}}";
    const List fin = call(geode_backup_finish_unpack, List{v1, "prefs/geode-prefs.json", prefsText});
    check(fin[0] == "0" && fin[2] == "5" && fin[3] == "20" && fin[4] == "2" && fin[5] == "settings" && fin[6] == "creative",
          "the preview: when, which app, which sections");
    check(fin[7] == "2" && fin[8] == "prefs/geode-prefs.json" && fin[10] == "prefs/creative/geode-prefs.json",
          "migration's rewritten files come back for the shell to write");
    const List broken = call(geode_backup_finish_unpack, List{v1, "prefs/geode-prefs.json", "not json"});
    check(broken[0] == "4", "a prefs file that does not parse is damage");

    // Restore Creative from the migrated slice (the second rewritten file); the phone holds the macros, an opacity and
    // a stray LFO key in Settings' file.
    const List restore = call(geode_backup_plan_restore,
                              List{"1", "2", "settings", "creative", "1", "creative", "1", fin[10], fin[11], "geode-prefs", "3",
                                   "macro_bank", "gui_opacity", "lfos"});
    check(restore[0] == "0" && restore[2] == "1" && restore[3] == "creative", "only the requested section");
    check(restore[4] == "7" && restore[12] == "0" && restore[13] == "7", "its seven directories, no cache to carry, seven to create");
    check(restore[21] == "1" && restore[22] == "geode-prefs" && restore[23] == "prefs/creative/geode-prefs.json",
          "one prefs file: Creative's slice of Settings' file");
    check(restore[24] == "2" && restore[25] == "macro_bank" && restore[26] == "lfos",
          "Creative's keys the phone holds are removed, Settings' opacity stays");
    check(restore[27] == "1" && restore[28] == "macro_bank" && restore[29] == "s" && restore[30] == "bank" && restore[31] == "0",
          "the backup's macro bank is written back as a string");
    const List none = call(geode_backup_plan_restore, List{"0", "0", "0", "0"});
    check(none[0] == "6", "nothing staged is problem 6");
}

void stagingAndName() {
    const List inside = call(geode_backup_check_staging, List{"/data/staging", "/", "presets/a.json", "/data/staging/presets/a.json"});
    check(inside[0] == "0", "a target under staging is fine");
    const List out = call(geode_backup_check_staging, List{"/data/staging", "/", "ok", "/data/staging/ok", "../x", "/data/x", "y", "/data/staging2/y"});
    check(out[0] == "3" && out[1] == "'../x' leaves staging", "the first target outside staging is an unsafe path");
    check(std::string(geode_backup_file_name(2026, 11, 8)) == "beatrealms-backup-20261108.zip", "the file name carries the date");
}

void malformed() {
    const List short1 = call(geode_backup_plan_unpack, List{"1"});
    check(short1[0] == "6", "an unpack request cut short is problem 6");
    const List word = call(geode_backup_check_extracted, List{"p", "ten", "x", "1", "1", "x", "0"});
    check(word[0] == "6", "a count that is not a number is problem 6");
    const List lie = call(geode_backup_plan_restore, List{"1", "999999999"});
    check(lie[0] == "6", "a count larger than the list is problem 6");
    int n = 0;
    const List empty = take(geode_backup_plan_export(nullptr, 0, nullptr, 0, &n), n);
    check(empty[0] == "6", "an empty prefs list has no snapshot count: problem 6");
}

}  // namespace

int main() {
    layoutCrosses();
    exportToRestore();
    stagingAndName();
    malformed();
    if (failures == 0) std::printf("backup_api_test: ok\n");
    return failures == 0 ? 0 : 1;
}
