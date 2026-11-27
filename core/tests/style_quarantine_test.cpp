// Host-side checks for core/viz/gl/StyleQuarantine: a style whose build or first draws took the
// process down (a driver crash or a GPU hang) is remembered on disk, per driver, and turned off after
// two such deaths, so a saved style cannot crash the app on every launch. Plain files in a temp dir;
// a "new process" is a fresh StyleQuarantine over a reset CacheSentinels, and a crash is an instance
// that is never destroyed (it leaves its files as a dead process would).
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <unistd.h>

#include "viz/gl/BuildReport.hpp"
#include "viz/gl/CacheSentinels.hpp"
#include "viz/gl/ScenePrep.hpp"
#include "viz/gl/StyleQuarantine.hpp"

using geode::viz::CacheSentinels;
using geode::viz::ScenePrep;
using geode::viz::StyleQuarantine;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

std::string freshDir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / ("style_quarantine_test-" + std::to_string(::getpid()) + "-" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    return dir.string();
}

std::string slurp(const std::string& file) {
    std::ifstream in(file);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool quarantined(const StyleQuarantine& q, const std::string& id) {
    for (const auto& entry : q.quarantined()) {
        if (entry == id) return true;
    }
    return false;
}

// A process that armed `id` and died before its draws proved it.
void crashWhileBuilding(CacheSentinels& s, const std::string& dir, const std::string& id) {
    s.resetForTest();
    auto* dead = new StyleQuarantine(s);
    dead->open(dir, "Adreno (TM) 740", 1000);
    dead->arm(id);
    (void)dead;  // never destroyed: the process died with its sentinel on disk
}

void oneCrashIsAStrikeTwoTurnItOff() {
    CacheSentinels s;
    const std::string dir = freshDir("strikes");
    crashWhileBuilding(s, dir, "flame_veil");
    check(fs::exists(dir + "/building-flame_veil"), "an armed build leaves its sentinel on disk");

    s.resetForTest();
    {
        StyleQuarantine q(s);
        q.open(dir, "Adreno (TM) 740", 2000);
        check(!quarantined(q, "flame_veil"), "one death is a strike, not a quarantine (an OEM kill or a swipe)");
        check(!fs::exists(dir + "/building-flame_veil"), "the leftover sentinel is counted once and removed");
        const std::string crash = slurp(dir + "/last-crash.txt");
        check(crash.find("flame_veil") != std::string::npos && crash.find("Adreno (TM) 740") != std::string::npos,
              "last-crash.txt names the style and the driver");
    }

    crashWhileBuilding(s, dir, "flame_veil");
    s.resetForTest();
    StyleQuarantine q(s);
    q.open(dir, "Adreno (TM) 740", 3000);
    check(quarantined(q, "flame_veil"), "a second death while building it turns the style off for this driver");
    q.arm("flame_veil");
    check(!fs::exists(dir + "/building-flame_veil"), "a quarantined style is never armed");

    s.resetForTest();
    StyleQuarantine again(s);
    again.open(dir, "Adreno (TM) 740", 4000);
    check(quarantined(again, "flame_veil"), "the quarantine outlives the process");
}

void drawsProveAStyle() {
    CacheSentinels s;
    const std::string dir = freshDir("proven");
    StyleQuarantine q(s);
    q.open(dir, "Mali-G715", 0);
    q.arm("opal_drop");
    check(fs::exists(dir + "/building-opal_drop"), "arming writes the sentinel");
    for (uint64_t frame = 1; frame < StyleQuarantine::kProvenDraws; ++frame) {
        q.drew("opal_drop", frame);
        q.drew("opal_drop", frame);
    }
    check(fs::exists(dir + "/building-opal_drop"), "two draws in one frame count as one frame");
    check(!q.proven("opal_drop"), "not proven before its draws");
    q.drew("opal_drop", StyleQuarantine::kProvenDraws);
    check(q.proven("opal_drop"), "proven after kProvenDraws frames");
    check(!fs::exists(dir + "/building-opal_drop"), "proving removes the sentinel");
    check(slurp(dir + "/proven.txt").find("opal_drop") != std::string::npos, "and records it");

    q.arm("opal_drop");
    check(!fs::exists(dir + "/building-opal_drop"), "a proven style never touches the disk again");

    s.resetForTest();
    StyleQuarantine next(s);
    next.open(dir, "Mali-G715", 0);
    next.arm("opal_drop");
    check(next.proven("opal_drop") && !fs::exists(dir + "/building-opal_drop"), "proven in a later process too");
}

void aStyleLeftBeforeItsDrawsStopsGuarding() {
    CacheSentinels s;
    const std::string dir = freshDir("left");
    StyleQuarantine q(s);
    q.open(dir, "Mali-G57", 0);
    q.arm("silk_bloom");
    q.settle(1, "silk_bloom");
    check(fs::exists(dir + "/building-silk_bloom"), "a style still building keeps its sentinel");
    q.drew("silk_bloom", 2);
    q.settle(2, "");
    check(fs::exists(dir + "/building-silk_bloom"), "a style drawn in the frame keeps its sentinel");
    q.settle(3, "silk_weave");
    check(!fs::exists(dir + "/building-silk_bloom"), "a style picked past before its draws proved it stops guarding");
    check(!q.proven("silk_bloom"), "without being proven");


    // The app is swiped away now: the style it left is not to blame.
    s.resetForTest();
    StyleQuarantine next(s);
    next.open(dir, "Mali-G57", 0);
    check(!fs::exists(dir + "/last-crash.txt"), "a style picked past is never struck by a later kill");
}

void twoRenderersShareOneSentinel() {
    CacheSentinels s;
    const std::string dir = freshDir("shared");
    StyleQuarantine live(s);
    StyleQuarantine wallpaper(s);
    live.open(dir, "PowerVR D-Series DXT-48-1536", 0);
    live.arm("hollow_roots");
    wallpaper.open(dir, "PowerVR D-Series DXT-48-1536", 0);
    check(fs::exists(dir + "/building-hollow_roots"), "a second renderer's open does not read a live sentinel as a crash");
    wallpaper.arm("hollow_roots");
    for (uint64_t frame = 1; frame <= StyleQuarantine::kProvenDraws; ++frame) live.drew("hollow_roots", frame);
    check(fs::exists(dir + "/building-hollow_roots"), "one renderer's proof does not unguard the other's build");
    for (uint64_t frame = 1; frame <= StyleQuarantine::kProvenDraws; ++frame) wallpaper.drew("hollow_roots", frame);
    check(!fs::exists(dir + "/building-hollow_roots"), "the last holder removes it");
}

void cleanReleaseIsNoStrike() {
    CacheSentinels s;
    const std::string dir = freshDir("release");
    {
        StyleQuarantine q(s);
        q.open(dir, "Samsung Xclipse 940", 0);
        q.arm("silk_web");
        q.release();
        check(!fs::exists(dir + "/building-silk_web"), "release() (a lost context, a closed view) disarms");
        q.arm("silk_web");
    }
    check(!fs::exists(dir + "/building-silk_web"), "a renderer that ends disarms what it still held");
    for (int i = 0; i < 3; ++i) {
        s.resetForTest();
        StyleQuarantine q(s);
        q.open(dir, "Samsung Xclipse 940", 0);
        check(!quarantined(q, "silk_web"), "a clean end is never a strike");
    }
}

void eachDriverHasItsOwnList() {
    const std::string root = freshDir("drivers");
    const std::string a = StyleQuarantine::dirFor(root, {"Qualcomm", "Adreno (TM) 740", "OpenGL ES 3.2 V@0676.0"}, "1.4.2+142");
    const std::string b = StyleQuarantine::dirFor(root, {"Qualcomm", "Adreno (TM) 740", "OpenGL ES 3.2 V@0700.0"}, "1.4.2+142");
    const std::string c = StyleQuarantine::dirFor(root, {"Qualcomm", "Adreno (TM) 740", "OpenGL ES 3.2 V@0676.0"}, "1.4.3+143");
    check(a == StyleQuarantine::dirFor(root, {"Qualcomm", "Adreno (TM) 740", "OpenGL ES 3.2 V@0676.0"}, "1.4.2+142"),
          "the same driver and app build find the same list");
    check(a != b, "a driver update starts a new list");
    check(a != c, "an app update starts a new list (its shaders may be fixed)");
    check(a.rfind(root + "/gl-quarantine/", 0) == 0, "lists live under <cacheDir>/gl-quarantine");

    CacheSentinels s;
    crashWhileBuilding(s, a, "flame_orb");
    crashWhileBuilding(s, a, "flame_orb");
    s.resetForTest();
    StyleQuarantine old(s);
    old.open(a, "Adreno (TM) 740", 0);
    check(quarantined(old, "flame_orb"), "off on the driver that crashed");
    StyleQuarantine updated(s);
    updated.open(b, "Adreno (TM) 740", 0);
    check(!quarantined(updated, "flame_orb"), "tried again on the updated driver");
}

void forgiveKeepsQuarantinedStyles() {
    ScenePrep prep;
    prep.fail("flame_veil", "flame_veil was turned off", ScenePrep::Lasting::Yes);
    prep.fail("opal_drop", "opal_drop did not load (compile)");
    prep.forgive();
    check(prep.failed("flame_veil"), "forgive() (a new context, an edited shader) keeps a quarantined style off");
    check(prep.report("flame_veil") == "flame_veil was turned off", "and its report");
    check(!prep.failed("opal_drop"), "an ordinary failure is still forgiven");

    namespace report = geode::viz::build_report;
    check(report::text("flame_veil", report::Stage::Crash, "") ==
              "flame_veil did not load (driver crash)\nit crashed this phone's graphics driver twice, so it is off until the driver or "
              "the app is updated",
          "the stage says why the style is off and what brings it back");
}

}  // namespace

int main() {
    oneCrashIsAStrikeTwoTurnItOff();
    drawsProveAStyle();
    aStyleLeftBeforeItsDrawsStopsGuarding();
    twoRenderersShareOneSentinel();
    cleanReleaseIsNoStrike();
    eachDriverHasItsOwnList();
    forgiveKeepsQuarantinedStyles();
    if (failures == 0) std::printf("style_quarantine_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
