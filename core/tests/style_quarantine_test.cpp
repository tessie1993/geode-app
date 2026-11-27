// Host-side checks for core/viz/gl/StyleQuarantine: a style that was the only one armed (built or first
// drawn) when its process died in the graphics driver is remembered on disk, per driver, and turned off
// after two such deaths, so a saved style cannot crash the app on every launch. Plain files in a temp dir.
// A "process" is a fresh CacheSentinels and StyleDeaths with its own pid; a death is abandon(), which
// leaves the sentinels on disk; the app's word on how earlier processes died is StyleDeaths::tell.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include "viz/gl/BuildReport.hpp"
#include "viz/gl/CacheSentinels.hpp"
#include "viz/gl/ScenePrep.hpp"
#include "viz/gl/StyleQuarantine.hpp"

using geode::viz::CacheSentinels;
using geode::viz::ScenePrep;
using geode::viz::StyleDeaths;
using geode::viz::StyleQuarantine;
using Death = StyleDeaths::Death;
using Verdict = StyleDeaths::Verdict;
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

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Exit records, written after the sentinels they explain.
Death crashOf(int pid) { return {pid, nowMs() + 60000, Verdict::Crash}; }
Death killOf(int pid) { return {pid, nowMs() + 60000, Verdict::Other}; }
Death unknownOf(int pid) { return {pid, nowMs() + 60000, Verdict::Unknown}; }

int countNamed(const std::string& dir, const std::string& prefix) {
    int n = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) n += entry.path().filename().string().rfind(prefix, 0) == 0 ? 1 : 0;
    return n;
}

struct Process {
    explicit Process(int pid) : deaths(pid) {}
    CacheSentinels sentinels;
    StyleDeaths deaths;
};

struct Run {
    explicit Run(int pid) : process(std::make_unique<Process>(pid)), q(std::make_unique<StyleQuarantine>(process->sentinels, process->deaths)) {}
    std::unique_ptr<Process> process;
    std::unique_ptr<StyleQuarantine> q;
    void open(const std::string& dir) { q->open(dir, "Adreno (TM) 740", 2000); }
    void tell(std::vector<Death> deaths) { process->deaths.tell(std::move(deaths)); }
};

// Process `pid` armed `id` and died before its draws proved it; nobody told it how its predecessor died.
void dieWhileBuilding(const std::string& dir, const std::string& id, int pid) {
    Run run(pid);
    run.open(dir);
    run.q->arm(id);
    run.q->abandon();
}

// Draws `id` for kProvenDraws frames over kProvenSeconds, from `t`.
void prove(StyleQuarantine& q, const std::string& id, double t = 0.0) {
    for (uint64_t i = 0; i <= StyleQuarantine::kProvenDraws; ++i) {
        q.frame("", t + StyleQuarantine::kProvenSeconds * static_cast<double>(i) / StyleQuarantine::kProvenDraws);
        q.drew(id);
    }
}

void oneCrashIsAStrikeTwoTurnItOff() {
    const std::string dir = freshDir("strikes");
    dieWhileBuilding(dir, "flame_veil", 101);
    check(has(slurp(dir + "/building-flame_veil"), "101 "), "the sentinel names the process that armed it");
    {
        Run run(102);
        run.open(dir);
        run.tell({crashOf(101)});
        check(!run.q->isQuarantined("flame_veil"), "one driver crash is a strike, not a quarantine");
        check(countNamed(dir, "building-") == 0 && countNamed(dir, "left-") == 0, "the leftover is counted once and removed");
        const std::string crash = slurp(dir + "/last-crash.txt");
        check(has(crash, "flame_veil") && has(crash, "Adreno (TM) 740"), "last-crash.txt names the style and the driver");
        check(has(slurp(dir + "/strikes.txt"), "flame_veil 1"), "one strike on disk");
        run.q->arm("flame_veil");
        run.q->abandon();
    }
    Run run(103);
    run.tell({crashOf(102), killOf(101)});
    run.open(dir);
    check(run.q->isQuarantined("flame_veil"), "a second driver crash turns it off, at once when the app told first");
    run.q->arm("flame_veil");
    check(!fs::exists(dir + "/building-flame_veil"), "a quarantined style is never armed");

    Run again(104);
    again.open(dir);
    check(again.q->isQuarantined("flame_veil"), "the quarantine outlives the process");
}

// A swipe from Recents, an OEM or low-memory kill, a JVM crash, a native crash outside the driver.
void aDeathThatIsNoDriverCrashIsNoStrike() {
    const std::string dir = freshDir("kills");
    for (int pid = 110; pid < 113; ++pid) {
        dieWhileBuilding(dir, "silk_web", pid);
        Run run(pid + 100);
        run.open(dir);
        run.tell({killOf(pid)});
        check(countNamed(dir, "left-") == 0 && countNamed(dir, "building-") == 0, "a kill's leftover is cleared");
        check(!run.q->isQuarantined("silk_web"), "a kill is never a strike");
    }
    check(!has(slurp(dir + "/strikes.txt"), "silk_web"), "nothing is struck");
}

// Unknown is "not yet": an export renderer or the live view may open before the app has read the records.
void evidenceWaitsUntilTold() {
    const std::string dir = freshDir("unknown");
    dieWhileBuilding(dir, "opal_drop", 201);
    {
        Run run(202);
        run.open(dir);
        check(!fs::exists(dir + "/building-opal_drop") && countNamed(dir, "left-201-") == 1,
              "an open before the app told sets the leftover aside and keeps it");
        run.q->arm("opal_drop");
        check(fs::exists(dir + "/building-opal_drop") && countNamed(dir, "left-201-") == 1, "arming the same style again keeps the evidence");
        run.q->release();
        check(countNamed(dir, "left-201-") == 1, "and so does a release");
    }
    {
        Run run(203);
        run.open(dir);
        run.tell({unknownOf(202), unknownOf(201)});
        check(countNamed(dir, "left-201-") == 1, "a record still unread (no tombstone yet) keeps it for a later process");
        check(!fs::exists(dir + "/strikes.txt"), "without a strike");
    }
    Run run(204);
    run.open(dir);
    run.tell({killOf(203), crashOf(201)});
    check(has(slurp(dir + "/strikes.txt"), "opal_drop 1"), "the crash, once known, strikes the style its own process armed");
    check(countNamed(dir, "left-") == 0, "and the evidence goes");
}

void theRecordMustBeTheSentinelsOwn() {
    const std::string dir = freshDir("own");
    dieWhileBuilding(dir, "hollow_roots", 301);
    {
        // P2 (started by a media button) crashed in audio code; P1 was swiped and its record is gone.
        Run run(303);
        run.open(dir);
        run.tell({crashOf(302)});
        check(!fs::exists(dir + "/strikes.txt"), "another process's crash is never this style's strike");
        check(countNamed(dir, "left-") == 0, "a leftover with no record of its own is dropped");
    }
    dieWhileBuilding(dir, "hollow_roots", 401);
    Run run(402);
    run.open(dir);
    run.tell({{401, 1000, Verdict::Crash}});
    check(!fs::exists(dir + "/strikes.txt"), "a record older than the sentinel (pid reused, power lost) is not its death");
}

void drawsProveAStyle() {
    const std::string dir = freshDir("proven");
    Run run(501);
    StyleQuarantine& q = *run.q;
    run.open(dir);
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
    q.arm("opal_drop");
    check(!fs::exists(dir + "/building-opal_drop"), "a proven style never touches the disk again");

    Run next(502);
    next.open(dir);
    next.q->arm("opal_drop");
    check(next.q->proven("opal_drop") && !fs::exists(dir + "/building-opal_drop"), "proven in a later process too");
}

void aProvenStyleIsNeverStruck() {
    const std::string dir = freshDir("never");
    {
        Run run(601);
        run.open(dir);
        run.q->arm("opal_drop");
        prove(*run.q, "opal_drop");
    }
    // A sentinel some holder left behind for a style already proven.
    std::ofstream(dir + "/building-opal_drop") << "602 " << nowMs() - 1000 << "\n";
    Run run(603);
    run.open(dir);
    run.tell({crashOf(602)});
    check(countNamed(dir, "left-") == 0 && !fs::exists(dir + "/building-opal_drop"), "its sentinel is cleared");
    check(!has(slurp(dir + "/strikes.txt"), "opal_drop"), "a proven style is never struck");
}

void provingClearsTheStrikes() {
    const std::string dir = freshDir("clear");
    dieWhileBuilding(dir, "flame_orb", 701);
    Run run(702);
    run.open(dir);
    run.tell({crashOf(701)});
    check(has(slurp(dir + "/strikes.txt"), "flame_orb 1"), "struck once");
    run.q->arm("flame_orb");
    prove(*run.q, "flame_orb");
    check(!has(slurp(dir + "/strikes.txt"), "flame_orb"), "a style that proves itself loses its strike");
}

void aStyleLeftBeforeItsDrawsStopsGuarding() {
    const std::string dir = freshDir("left");
    {
        Run run(801);
        StyleQuarantine& q = *run.q;
        run.open(dir);
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
    }
    Run next(802);
    next.open(dir);
    next.tell({crashOf(801)});
    check(!fs::exists(dir + "/last-crash.txt"), "a style picked past is never struck by a later crash");
}

// The surface stops drawing (home, the lock screen, a hidden wallpaper) and the context is kept.
void aPauseDisarmsAndDrawingOnRearms() {
    const std::string dir = freshDir("pause");
    {
        Run run(901);
        StyleQuarantine& q = *run.q;
        run.open(dir);
        q.arm("silk_bloom");
        q.frame("silk_bloom", 0.0);
        q.drew("silk_bloom");
        q.pause();
        check(!fs::exists(dir + "/building-silk_bloom"), "a pause takes the sentinel down");
        q.frame("", 60.0);
        q.drew("silk_bloom");
        check(!fs::exists(dir + "/building-silk_bloom"), "one draw while paused (a still frame) does not put it back");
        q.frame("", 60.1);
        check(fs::exists(dir + "/building-silk_bloom"), "drawing on puts it back for a style still unproven");
        q.drew("silk_bloom");
        q.frame("", 60.2);
        q.drew("silk_bloom");
        q.frame("", 60.3);
        q.drew("silk_bloom");
        check(!q.proven("silk_bloom"), "time spent paused is not time drawing");

        q.arm("opal_drop");
        q.frame("opal_drop", 61.0);
        q.pause();
        check(!fs::exists(dir + "/building-opal_drop"), "a style still building is taken down too");
        q.frame("opal_drop", 70.0);
        check(fs::exists(dir + "/building-opal_drop"), "and put back on the next frame: it never drew");
        q.pause();
        q.abandon();
    }
    Run run(902);
    run.open(dir);
    run.tell({crashOf(901)});
    check(!fs::exists(dir + "/strikes.txt"), "a crash while paused is never a style's strike");
}

void twoArmedStylesAreNoStrike() {
    const std::string dir = freshDir("ambiguous");
    {
        Process p(1001);
        StyleQuarantine live(p.sentinels, p.deaths);
        StyleQuarantine wallpaper(p.sentinels, p.deaths);
        live.open(dir, "Mali-G57", 0);
        wallpaper.open(dir, "Mali-G57", 0);
        live.arm("silk_bloom");
        wallpaper.arm("hollow_roots");
        live.abandon();
        wallpaper.abandon();
    }
    Run run(1002);
    run.open(dir);
    run.tell({crashOf(1001)});
    check(!fs::exists(dir + "/strikes.txt"), "two styles armed at the crash: neither is struck");
    const std::string crash = slurp(dir + "/last-crash.txt");
    check(has(crash, "ambiguous") && has(crash, "silk_bloom") && has(crash, "hollow_roots"), "last-crash.txt says it could not tell");
    check(countNamed(dir, "left-") == 0, "both leftovers are cleared");
}

void malformedStrikesAreSkipped() {
    const std::string dir = freshDir("malformed");
    fs::create_directories(dir);
    std::ofstream(dir + "/strikes.txt") << "garbage\nopal_drop x\n../etc 1\nflame_veil 1\n 7\nsilk_web 1 extra\nhollow_roots 1";
    std::ofstream(dir + "/building-junk") << "not a pid\n";
    dieWhileBuilding(dir, "flame_veil", 1101);
    Run run(1102);
    run.open(dir);
    run.tell({crashOf(1101)});
    check(!run.q->isQuarantined("flame_veil"), "a renderer open before the app told has not looked yet");
    check(run.q->frame("", 0.0) && run.q->isQuarantined("flame_veil"), "its next frame sees the style turned off");
    check(!run.q->frame("", 0.1), "once");
    check(has(slurp(dir + "/quarantined.txt"), "flame_veil"), "a good line after bad ones still counts");
    const std::string strikes = slurp(dir + "/strikes.txt");
    check(has(strikes, "hollow_roots 1"), "a last line with no newline is kept");
    check(!has(strikes, "garbage") && !has(strikes, "../etc") && !has(strikes, "opal_drop"), "bad lines are dropped");
    check(!has(strikes, "silk_web"), "a line with more than an id and a count is bad");
    check(!fs::exists(dir + "/building-junk") && countNamed(dir, "left-") == 0, "a sentinel with no pid is dropped");
    check(countNamed(dir, "strikes.txt.") == 0 && countNamed(dir, "quarantined.txt.") == 0, "no temp file is left behind");
}

void twoRenderersShareOneSentinel() {
    const std::string dir = freshDir("shared");
    Process p(1201);
    StyleQuarantine live(p.sentinels, p.deaths);
    StyleQuarantine wallpaper(p.sentinels, p.deaths);
    live.open(dir, "PowerVR D-Series DXT-48-1536", 0);
    live.arm("hollow_roots");
    wallpaper.open(dir, "PowerVR D-Series DXT-48-1536", 0);
    check(fs::exists(dir + "/building-hollow_roots"), "a second renderer's open does not set a live sentinel aside");
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
    const std::string dir = freshDir("release");
    {
        Run run(1301);
        run.open(dir);
        run.q->arm("silk_web");
        run.q->release();
        check(!fs::exists(dir + "/building-silk_web"), "release() (a lost context, a closed view) disarms");
        run.q->arm("silk_web");
    }
    check(!fs::exists(dir + "/building-silk_web"), "a renderer that ends disarms what it still held");
    Run run(1302);
    run.open(dir);
    run.tell({crashOf(1301)});
    check(!fs::exists(dir + "/strikes.txt"), "a clean end is never a strike");
}

void aStyleLeftAloneIsNotGuarded() {
    const std::string dir = freshDir("leave");
    Run run(1401);
    run.open(dir);
    run.q->arm("silk_web");
    run.q->leave("silk_web");
    check(!fs::exists(dir + "/building-silk_web"), "leave() (a shader override now draws it) disarms");
    run.q->frame("", 0.0);
    check(!fs::exists(dir + "/building-silk_web"), "and the next frame does not arm it again");
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

    dieWhileBuilding(a, "flame_orb", 1501);
    {
        Run run(1502);
        run.open(a);
        run.tell({crashOf(1501)});
        run.q->arm("flame_orb");
        run.q->abandon();
    }
    Run old(1503);
    old.open(a);
    old.tell({crashOf(1502)});
    old.q->frame("", 0.0);
    check(old.q->isQuarantined("flame_orb"), "off on the driver that crashed");
    StyleQuarantine updated(old.process->sentinels, old.process->deaths);
    updated.open(b, "Adreno (TM) 740", 0);
    check(!updated.isQuarantined("flame_orb"), "tried again on the updated driver");
    check(fs::exists(a), "a list in use in this process is kept");

    Run next(1504);
    next.open(b);
    check(fs::exists(b) && !fs::exists(a), "the first open in a process prunes the lists of older drivers and builds");
    check(fs::exists(root), "and nothing outside gl-quarantine");
}

void theProcessIsOne() {
    check(&StyleDeaths::process() == &StyleDeaths::process(), "every renderer in a process shares one StyleDeaths");
    check(StyleDeaths::process().pid() == static_cast<int>(::getpid()), "and it carries this process's pid");
}

void forgiveKeepsQuarantinedStyles() {
    ScenePrep prep;
    prep.fail("flame_veil", "flame_veil was turned off", ScenePrep::Lasting::Yes);
    prep.fail("opal_drop", "opal_drop did not load (compile)");
    prep.forgive();
    check(prep.failed("flame_veil") && prep.quarantined("flame_veil"), "forgive() (a new context, an edited shader) keeps a quarantined style off");
    check(prep.report("flame_veil") == "flame_veil was turned off", "and its report");
    check(!prep.failed("opal_drop"), "an ordinary failure is still forgiven");

    namespace report = geode::viz::build_report;
    check(report::text("flame_veil", report::Stage::Crash, "") ==
              "flame_veil did not load (driver crash)\nit crashed this phone's graphics driver twice, so it is off until the driver or "
              "the app is updated",
          "the stage says why the style is off and what brings it back");
}

void anOverrideMayTryAQuarantinedStyle() {
    ScenePrep prep;
    prep.fail("flame_veil", "flame_veil was turned off", ScenePrep::Lasting::Yes);
    prep.setOverridden({"flame_veil"});
    check(!prep.failed("flame_veil") && !prep.quarantined("flame_veil"), "a Studio override of a quarantined style may build");
    check(prep.report("flame_veil").empty(), "and the stage does not call it off");
    prep.fail("flame_veil", "flame_veil did not load (compile)");
    check(prep.failed("flame_veil") && prep.report("flame_veil") == "flame_veil did not load (compile)",
          "an override that does not build fails as any build does");
    prep.forgive();
    prep.setOverridden({});
    check(prep.quarantined("flame_veil") && prep.report("flame_veil") == "flame_veil was turned off",
          "without the override it is off again");
}

// Renderer::resolveActiveScene's choice, frame by frame: the id it stages and hands readyScene.
void aQuarantinedStyleGetsAStandInEveryFrame() {
    ScenePrep prep;
    prep.fail("flame_veil", "off", ScenePrep::Lasting::Yes);
    const std::string requested = "flame_veil";
    const std::string fallback = "neon_chrome_sea";
    std::string onScreen;
    for (int frame = 0; frame < 5; ++frame) {
        const std::string& staged = prep.stageFor(requested, onScreen, fallback);
        check(staged == fallback, "with nothing on screen, the free first style stands in, every frame");
        onScreen = staged;  // built: the next frame draws it, and nothing stays pending
    }
    check(prep.stageFor(requested, "glass_orb", fallback) == "glass_orb", "a style already on screen stays");
    check(prep.stageFor("opal_drop", "glass_orb", fallback) == "opal_drop", "a style that is not off draws itself");
    prep.fail("glass_orb", "did not load");
    check(prep.stageFor(requested, "glass_orb", fallback) == fallback, "a stand-in that failed to build falls through to the next");
    prep.fail(fallback, "did not load");
    check(prep.stageFor(requested, "glass_orb", fallback) == requested, "with none left the request stays, as any failed style");
    prep.setOverridden({"flame_veil"});
    check(prep.stageFor(requested, "", fallback) == requested, "an overridden style is not stood in for");
}

}  // namespace

int main() {
    oneCrashIsAStrikeTwoTurnItOff();
    aDeathThatIsNoDriverCrashIsNoStrike();
    evidenceWaitsUntilTold();
    theRecordMustBeTheSentinelsOwn();
    drawsProveAStyle();
    aProvenStyleIsNeverStruck();
    provingClearsTheStrikes();
    aStyleLeftBeforeItsDrawsStopsGuarding();
    aPauseDisarmsAndDrawingOnRearms();
    twoArmedStylesAreNoStrike();
    malformedStrikesAreSkipped();
    twoRenderersShareOneSentinel();
    cleanReleaseIsNoStrike();
    aStyleLeftAloneIsNotGuarded();
    eachDriverHasItsOwnList();
    theProcessIsOne();
    forgiveKeepsQuarantinedStyles();
    anOverrideMayTryAQuarantinedStyle();
    aQuarantinedStyleGetsAStandInEveryFrame();
    if (failures == 0) std::printf("style_quarantine_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
