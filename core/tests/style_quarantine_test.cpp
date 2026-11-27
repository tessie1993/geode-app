// Host-side checks for core/viz/gl/StyleQuarantine: a style whose build or first draws were armed when
// the process died of a native crash or an ANR is remembered on disk, per driver, and turned off after
// two such deaths, so a saved style cannot crash the app on every launch. Plain files in a temp dir; a
// "new process" is a fresh StyleQuarantine over a reset CacheSentinels, and a death is abandon(): the
// instance forgets what it armed and leaves its files as a dead process would.
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
using Death = geode::viz::StyleQuarantine::Death;
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

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// Draws `id` for kProvenDraws frames over kProvenSeconds, from `t`.
void prove(StyleQuarantine& q, const std::string& id, double t = 0.0) {
    for (uint64_t i = 0; i <= StyleQuarantine::kProvenDraws; ++i) {
        q.frame("", t + StyleQuarantine::kProvenSeconds * static_cast<double>(i) / StyleQuarantine::kProvenDraws);
        q.drew(id);
    }
}

// A process that armed `id` and died before its draws proved it; `previous` is how the process before it died.
void dieWhileBuilding(CacheSentinels& s, const std::string& dir, const std::string& id, Death previous = Death::Crash) {
    s.resetForTest();
    StyleQuarantine dead(s);
    dead.open(dir, "Adreno (TM) 740", 1000, previous);
    dead.arm(id);
    dead.abandon();
}

// A new process that learned how the last one died.
void reopen(CacheSentinels& s, StyleQuarantine& q, const std::string& dir, Death death, long long nowS = 2000) {
    s.resetForTest();
    q.open(dir, "Adreno (TM) 740", nowS, death);
}

void oneCrashIsAStrikeTwoTurnItOff() {
    CacheSentinels s;
    const std::string dir = freshDir("strikes");
    dieWhileBuilding(s, dir, "flame_veil");
    check(fs::exists(dir + "/building-flame_veil"), "an armed build leaves its sentinel on disk");
    {
        StyleQuarantine q(s);
        reopen(s, q, dir, Death::Crash);
        check(!q.isQuarantined("flame_veil"), "one crash is a strike, not a quarantine");
        check(!fs::exists(dir + "/building-flame_veil"), "the leftover sentinel is counted once and removed");
        const std::string crash = slurp(dir + "/last-crash.txt");
        check(has(crash, "flame_veil") && has(crash, "Adreno (TM) 740"), "last-crash.txt names the style and the driver");
        check(has(slurp(dir + "/strikes.txt"), "flame_veil 1"), "one strike on disk");
    }
    dieWhileBuilding(s, dir, "flame_veil");
    StyleQuarantine q(s);
    reopen(s, q, dir, Death::Crash, 3000);
    check(q.isQuarantined("flame_veil"), "a second crash while building it turns the style off for this driver");
    q.arm("flame_veil");
    check(!fs::exists(dir + "/building-flame_veil"), "a quarantined style is never armed");

    StyleQuarantine again(s);
    reopen(s, again, dir, Death::Unknown, 4000);
    check(again.isQuarantined("flame_veil"), "the quarantine outlives the process");
}

// A swipe from Recents, an OEM or low-memory kill, a JVM crash: the style is not to blame.
void aDeathThatIsNoCrashIsNoStrike() {
    CacheSentinels s;
    const std::string dir = freshDir("kills");
    for (const Death death : {Death::Other, Death::Unknown, Death::Other}) {
        dieWhileBuilding(s, dir, "silk_web");
        StyleQuarantine q(s);
        reopen(s, q, dir, death);
        check(!fs::exists(dir + "/building-silk_web"), "a kill's leftover sentinel is cleared");
        check(!q.isQuarantined("silk_web"), "a kill or an unknown death is never a strike");
    }
    check(!has(slurp(dir + "/strikes.txt"), "silk_web"), "nothing is struck");

    dieWhileBuilding(s, dir, "silk_web");
    StyleQuarantine q(s);
    reopen(s, q, dir, Death::Crash);
    check(has(slurp(dir + "/strikes.txt"), "silk_web 1"), "the same leftover after a native crash or an ANR is one strike");
}

void theDeathIsProcessWide() {
    CacheSentinels s;
    const std::string dir = freshDir("flag");
    check(StyleQuarantine::lastDeath() == Death::Unknown, "unknown until the app says");
    StyleQuarantine::setLastDeath(Death::Crash);
    check(StyleQuarantine::lastDeath() == Death::Crash, "the app's word is kept for every renderer");
    dieWhileBuilding(s, dir, "opal_drop");
    s.resetForTest();
    StyleQuarantine q(s);
    q.open(dir, "Mali-G715", 0);
    check(has(slurp(dir + "/strikes.txt"), "opal_drop 1"), "open() uses it by default");
    StyleQuarantine::setLastDeath(Death::Unknown);
}

void drawsProveAStyle() {
    CacheSentinels s;
    const std::string dir = freshDir("proven");
    StyleQuarantine q(s);
    q.open(dir, "Mali-G715", 0, Death::Unknown);
    q.arm("opal_drop");
    check(fs::exists(dir + "/building-opal_drop"), "arming writes the sentinel");
    for (uint64_t i = 0; i < StyleQuarantine::kProvenDraws; ++i) {
        q.frame("", 0.01 * static_cast<double>(i));
        q.drew("opal_drop");
        q.drew("opal_drop");
    }
    check(!q.proven("opal_drop") && fs::exists(dir + "/building-opal_drop"), "frames alone, in a blink, do not prove it");
    q.frame("", StyleQuarantine::kProvenSeconds * 0.5);
    q.drew("opal_drop");
    check(!q.proven("opal_drop"), "nor half the time");
    q.frame("", StyleQuarantine::kProvenSeconds + 0.1);
    q.drew("opal_drop");
    check(q.proven("opal_drop"), "proven after kProvenDraws frames and kProvenSeconds of drawing");
    check(!fs::exists(dir + "/building-opal_drop"), "proving removes the sentinel");
    check(has(slurp(dir + "/proven.txt"), "opal_drop"), "and records it");
    check(q.lastProven() == "opal_drop", "the last style proven is known");

    q.arm("opal_drop");
    check(!fs::exists(dir + "/building-opal_drop"), "a proven style never touches the disk again");

    StyleQuarantine next(s);
    reopen(s, next, dir, Death::Unknown);
    next.arm("opal_drop");
    check(next.proven("opal_drop") && !fs::exists(dir + "/building-opal_drop"), "proven in a later process too");
    check(next.lastProven() == "opal_drop", "and still the last proven");
}

void aProvenStyleIsNeverStruck() {
    CacheSentinels s;
    const std::string dir = freshDir("never");
    {
        StyleQuarantine q(s);
        q.open(dir, "Mali-G715", 0, Death::Unknown);
        q.arm("opal_drop");
        prove(q, "opal_drop");
    }
    // A sentinel some holder left behind for a style already proven.
    std::ofstream(dir + "/building-opal_drop").close();
    StyleQuarantine q(s);
    reopen(s, q, dir, Death::Crash);
    check(!fs::exists(dir + "/building-opal_drop"), "its sentinel is cleared");
    check(!has(slurp(dir + "/strikes.txt"), "opal_drop"), "a proven style is never struck");
}

void provingClearsTheStrikes() {
    CacheSentinels s;
    const std::string dir = freshDir("clear");
    dieWhileBuilding(s, dir, "flame_orb");
    {
        StyleQuarantine q(s);
        reopen(s, q, dir, Death::Crash);
        check(has(slurp(dir + "/strikes.txt"), "flame_orb 1"), "struck once");
        q.arm("flame_orb");
        prove(q, "flame_orb");
        check(!has(slurp(dir + "/strikes.txt"), "flame_orb"), "a style that proves itself loses its strike");
    }
}

void aStyleLeftBeforeItsDrawsStopsGuarding() {
    CacheSentinels s;
    const std::string dir = freshDir("left");
    StyleQuarantine q(s);
    q.open(dir, "Mali-G57", 0, Death::Unknown);
    q.arm("silk_bloom");
    q.frame("silk_bloom", 0.0);
    check(fs::exists(dir + "/building-silk_bloom"), "a style still building keeps its sentinel");
    q.drew("silk_bloom");
    q.frame("", 0.1);
    check(fs::exists(dir + "/building-silk_bloom"), "a style drawn in the frame keeps its sentinel");
    q.frame("silk_weave", 0.2);
    check(!fs::exists(dir + "/building-silk_bloom"), "a style picked past before its draws proved it stops guarding");
    check(!q.proven("silk_bloom"), "without being proven");
    q.abandon();

    StyleQuarantine next(s);
    reopen(s, next, dir, Death::Crash);
    check(!fs::exists(dir + "/last-crash.txt"), "a style picked past is never struck by a later crash");
}

// The surface stops drawing (home, the lock screen, a hidden wallpaper) and the context is kept.
void aPauseDisarmsAndTheNextFrameRearms() {
    CacheSentinels s;
    const std::string dir = freshDir("pause");
    {
        StyleQuarantine q(s);
        q.open(dir, "Mali-G57", 0, Death::Unknown);
        q.arm("silk_bloom");
        q.frame("silk_bloom", 0.0);
        q.drew("silk_bloom");
        q.pause();
        check(!fs::exists(dir + "/building-silk_bloom"), "a pause takes the sentinel down");
        q.frame("", 60.0);
        check(fs::exists(dir + "/building-silk_bloom"), "the next frame puts it back for a style still unproven");
        q.drew("silk_bloom");
        q.frame("", 60.1);
        q.drew("silk_bloom");
        q.frame("", 60.2);
        q.drew("silk_bloom");
        q.frame("", 60.3);
        q.drew("silk_bloom");
        check(!q.proven("silk_bloom"), "time spent paused is not time drawing");
        q.pause();
        q.abandon();
    }
    StyleQuarantine q(s);
    reopen(s, q, dir, Death::Crash);
    check(!has(slurp(dir + "/strikes.txt"), "silk_bloom"), "a kill or crash while paused is never this style's strike");
}

void twoArmedStylesAreNoStrike() {
    CacheSentinels s;
    const std::string dir = freshDir("ambiguous");
    s.resetForTest();
    {
        StyleQuarantine live(s);
        StyleQuarantine wallpaper(s);
        live.open(dir, "Mali-G57", 0, Death::Unknown);
        wallpaper.open(dir, "Mali-G57", 0, Death::Unknown);
        live.arm("silk_bloom");
        wallpaper.arm("hollow_roots");
        live.abandon();
        wallpaper.abandon();
    }
    StyleQuarantine q(s);
    reopen(s, q, dir, Death::Crash);
    check(!fs::exists(dir + "/strikes.txt") || slurp(dir + "/strikes.txt").empty(), "two styles armed at the crash: neither is struck");
    const std::string crash = slurp(dir + "/last-crash.txt");
    check(has(crash, "ambiguous") && has(crash, "silk_bloom") && has(crash, "hollow_roots"), "last-crash.txt says it could not tell");
    check(!fs::exists(dir + "/building-silk_bloom") && !fs::exists(dir + "/building-hollow_roots"), "both sentinels are cleared");
}

void malformedStrikesAreSkipped() {
    CacheSentinels s;
    const std::string dir = freshDir("malformed");
    fs::create_directories(dir);
    std::ofstream(dir + "/strikes.txt") << "garbage\nopal_drop x\n../etc 1\nflame_veil 1\n 7\nsilk_web 1 extra\nhollow_roots 1";
    dieWhileBuilding(s, dir, "flame_veil");
    StyleQuarantine q(s);
    reopen(s, q, dir, Death::Crash);
    check(q.isQuarantined("flame_veil"), "a good line after bad ones still counts");
    const std::string strikes = slurp(dir + "/strikes.txt");
    check(has(strikes, "hollow_roots 1"), "a last line with no newline is kept");
    check(!has(strikes, "garbage") && !has(strikes, "../etc") && !has(strikes, "opal_drop"), "bad lines are dropped");
    check(!has(strikes, "silk_web"), "a line with more than an id and a count is bad");
    bool temp = false;
    for (const auto& entry : fs::directory_iterator(dir)) temp = temp || has(entry.path().filename().string(), ".tmp");
    check(!temp, "no temp file is left behind");
}

void twoRenderersShareOneSentinel() {
    CacheSentinels s;
    const std::string dir = freshDir("shared");
    StyleQuarantine live(s);
    StyleQuarantine wallpaper(s);
    live.open(dir, "PowerVR D-Series DXT-48-1536", 0, Death::Unknown);
    live.arm("hollow_roots");
    wallpaper.open(dir, "PowerVR D-Series DXT-48-1536", 0, Death::Unknown);
    check(fs::exists(dir + "/building-hollow_roots"), "a second renderer's open does not read a live sentinel as a crash");
    wallpaper.arm("hollow_roots");
    live.release();
    check(fs::exists(dir + "/building-hollow_roots"), "one renderer's release does not unguard the other's build");
    live.arm("hollow_roots");
    prove(live, "hollow_roots");
    check(!fs::exists(dir + "/building-hollow_roots"), "one renderer's proof takes it down for every holder");
    wallpaper.frame("", 0.0);
    wallpaper.drew("hollow_roots");
    check(wallpaper.proven("hollow_roots"), "the other renderer learns it is proven");
    wallpaper.release();
    wallpaper.arm("hollow_roots");
    check(!fs::exists(dir + "/building-hollow_roots"), "and never arms it again");
}

void cleanReleaseIsNoStrike() {
    CacheSentinels s;
    const std::string dir = freshDir("release");
    {
        StyleQuarantine q(s);
        q.open(dir, "Samsung Xclipse 940", 0, Death::Unknown);
        q.arm("silk_web");
        q.release();
        check(!fs::exists(dir + "/building-silk_web"), "release() (a lost context, a closed view) disarms");
        q.arm("silk_web");
    }
    check(!fs::exists(dir + "/building-silk_web"), "a renderer that ends disarms what it still held");
    for (int i = 0; i < 3; ++i) {
        StyleQuarantine q(s);
        reopen(s, q, dir, Death::Crash);
        check(!q.isQuarantined("silk_web"), "a clean end is never a strike");
    }
}

void aStyleLeftAloneIsNotGuarded() {
    CacheSentinels s;
    const std::string dir = freshDir("leave");
    StyleQuarantine q(s);
    q.open(dir, "Mali-G57", 0, Death::Unknown);
    q.arm("silk_web");
    q.leave("silk_web");
    check(!fs::exists(dir + "/building-silk_web"), "leave() (a shader override now draws it) disarms");
    q.frame("", 0.0);
    check(!fs::exists(dir + "/building-silk_web"), "and the next frame does not arm it again");
}

void aQuarantinedStyleGetsAStandIn() {
    CacheSentinels s;
    const std::string dir = freshDir("standin");
    dieWhileBuilding(s, dir, "flame_veil");
    {
        StyleQuarantine q(s);
        reopen(s, q, dir, Death::Crash);
    }
    dieWhileBuilding(s, dir, "flame_veil");
    StyleQuarantine q(s);
    reopen(s, q, dir, Death::Crash);
    check(q.isQuarantined("flame_veil"), "the saved style is off");
    check(q.standIn("flame_veil", "silk_flow") == "silk_flow", "with nothing proven, the safe default draws");
    check(q.standIn("opal_drop", "silk_flow") == "opal_drop", "a style that is not off draws itself");
    q.arm("opal_drop");
    prove(q, "opal_drop");
    check(q.standIn("flame_veil", "silk_flow") == "opal_drop", "the last proven style stands in");
    check(q.standIn("flame_veil", "flame_veil").empty() || q.standIn("flame_veil", "flame_veil") == "opal_drop",
          "never the quarantined style itself");

    StyleQuarantine later(s);
    reopen(s, later, dir, Death::Unknown);
    check(later.standIn("flame_veil", "silk_flow") == "opal_drop", "in a later process too");
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
    dieWhileBuilding(s, a, "flame_orb");
    dieWhileBuilding(s, a, "flame_orb");
    StyleQuarantine old(s);
    reopen(s, old, a, Death::Crash);
    check(old.isQuarantined("flame_orb"), "off on the driver that crashed");
    StyleQuarantine updated(s);
    updated.open(b, "Adreno (TM) 740", 0, Death::Unknown);
    check(!updated.isQuarantined("flame_orb"), "tried again on the updated driver");
    check(fs::exists(a), "a list in use in this process is kept");

    s.resetForTest();
    StyleQuarantine next(s);
    next.open(b, "Adreno (TM) 740", 0, Death::Unknown);
    check(fs::exists(b) && !fs::exists(a), "the first open in a process prunes the lists of older drivers and builds");
    check(fs::exists(root), "and nothing outside gl-quarantine");
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
    aDeathThatIsNoCrashIsNoStrike();
    theDeathIsProcessWide();
    drawsProveAStyle();
    aProvenStyleIsNeverStruck();
    provingClearsTheStrikes();
    aStyleLeftBeforeItsDrawsStopsGuarding();
    aPauseDisarmsAndTheNextFrameRearms();
    twoArmedStylesAreNoStrike();
    malformedStrikesAreSkipped();
    twoRenderersShareOneSentinel();
    cleanReleaseIsNoStrike();
    aStyleLeftAloneIsNotGuarded();
    aQuarantinedStyleGetsAStandIn();
    eachDriverHasItsOwnList();
    forgiveKeepsQuarantinedStyles();
    if (failures == 0) std::printf("style_quarantine_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
