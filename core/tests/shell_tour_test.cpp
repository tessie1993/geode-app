// Host-side checks for core/shell/Tour: the onboarding tour's rules (which step shows, what a missing control
// does, what is remembered, what holds it back) and where its card goes. Every case of the Kotlin TourTest
// (app/src/test/java/dev/geode/model/TourTest.kt) is here with the same expected result. No device.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/shell_tour_test.cpp core/shell/*.cpp
//     core/util/Json.cpp -o shell_tour_test && ./shell_tour_test
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "shell/Tour.hpp"

using namespace geode::shell;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

std::uint32_t chapterMask(TourChapter ch) {
    std::uint32_t m = 0;
    for (TourSpot s : tourChapterSpots(ch)) m |= tourSpotBit(s);
    return m;
}

TourFacts visualsShown() {
    TourFacts f;
    f.shown = chapterMask(TourChapter::Visuals);
    f.place = TourPlace::Visuals;
    return f;
}

TourRun run1(TourChapter c) {
    TourRun r;
    r.chapters = {c};
    return r;
}

std::set<std::string> splitSet(const std::string& joined) {
    std::set<std::string> out;
    std::string cur;
    for (char c : joined) {
        if (c == ',') {
            out.insert(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.insert(cur);
    return out;
}

TourProgress progressDone(std::vector<TourChapter> done) {
    TourProgress p;
    p.done = std::move(done);
    return p;
}

void everyChapterHasSteps() {
    for (int c = 0; c < kTourChapterCount; ++c) check(!tourChapterSpots(static_cast<TourChapter>(c)).empty(), "chapter has steps");
    check(tourChapterSpots(TourChapter::Start).front() == TourSpot::NavLibrary, "START leads with the Library tab");
}

void missingControlIsPassedOver() {
    TourFacts facts = visualsShown();
    facts.shown = tourSpotBit(TourSpot::VisualsTabs) | tourSpotBit(TourSpot::StageRecord);
    const TourRun run = run1(TourChapter::Visuals);
    const auto first = tourStep(run, facts);
    check(first && first->spot == TourSpot::VisualsTabs && first->index == 1 && first->count == 2, "first of two pointers");
    if (!first) return;
    const auto second = tourStep(tourPass(run, first->spot), facts);
    check(second && second->spot == TourSpot::StageRecord && second->last(), "second is last");
}

void chapterWithNoControlHasNoStep() {
    check(!tourStep(run1(TourChapter::Studio), visualsShown()), "studio has no step on visuals");
    check(!tourStep(TourRun{}, visualsShown()), "no chapter, no step");
}

void firstRunSkipsOpenLibraryOnLibrary() {
    TourFacts onLibrary;
    onLibrary.shown = tourSpotBit(TourSpot::NavLibrary) | tourSpotBit(TourSpot::NavVisuals);
    onLibrary.place = TourPlace::Library;
    const auto step = tourStep(run1(TourChapter::Start), onLibrary);
    check(step && step->spot == TourSpot::PlaySong && tourSpotKind(step->spot) == TourStepKind::Wait, "play song waits");
}

void firstRunAsksForLibraryElsewhere() {
    TourFacts onPlayer;
    onPlayer.shown = tourSpotBit(TourSpot::NavLibrary) | tourSpotBit(TourSpot::NavVisuals);
    const auto step = tourStep(run1(TourChapter::Start), onPlayer);
    check(step && step->spot == TourSpot::NavLibrary && step->index == 1 && step->count == 2, "library first, 1 of 2");
}

void playingEndsTheWaitAndVisualsEndsTheRun() {
    const TourRun run = run1(TourChapter::Start);
    TourFacts playing;
    playing.shown = tourSpotBit(TourSpot::NavLibrary) | tourSpotBit(TourSpot::NavVisuals);
    playing.place = TourPlace::Library;
    playing.playing = true;
    check(tourReached(TourSpot::PlaySong, playing), "playing reaches play song");
    const auto step = tourStep(run, playing);
    check(step && step->spot == TourSpot::NavVisuals, "then Visuals");
    playing.place = TourPlace::Visuals;
    check(!tourStep(run, playing), "visuals ends the first run");
}

void aPointerIsNeverReachedByItself() {
    TourFacts f = visualsShown();
    f.playing = true;
    for (int s = 0; s < kTourSpotCount; ++s) {
        const auto spot = static_cast<TourSpot>(s);
        if (tourSpotKind(spot) == TourStepKind::Point) check(!tourReached(spot, f), "pointer never reached");
    }
}

void stepCountStaysPut() {
    TourRun run = run1(TourChapter::Visuals);
    const TourFacts f = visualsShown();
    std::vector<std::pair<int, int>> counts;
    for (size_t i = 0; i < tourChapterSpots(TourChapter::Visuals).size(); ++i) {
        const auto step = tourStep(run, f);
        if (!step) {
            check(false, "step expected");
            return;
        }
        run = tourPass(run, step->spot);
        counts.emplace_back(step->index, step->count);
    }
    check(counts == std::vector<std::pair<int, int>>{{1, 4}, {2, 4}, {3, 4}, {4, 4}}, "1/4 .. 4/4");
    check(!tourStep(run, f), "nothing left");
}

void dueOrder() {
    check(tourDue(TourProgress{}, visualsShown()) == TourChapter::Start, "START is due first");
    const TourProgress started = progressDone({TourChapter::Start});
    check(tourDue(started, visualsShown()) == TourChapter::Visuals, "then visuals");
    check(!tourDue(started, TourFacts{}), "nothing shown, nothing due");
    check(!tourDue(progressDone({TourChapter::Start, TourChapter::Visuals}), visualsShown()), "visuals done");
    check(!tourDue(tourFinished(), visualsShown()), "finished");
}

void resumePicksUp() {
    TourProgress left = progressDone({TourChapter::Start});
    left.resume = TourSpot::StageTools;
    check(tourResume(TourChapter::Visuals, left).at == 2, "visuals resumes at 2");
    check(tourResume(TourChapter::Studio, left).at == 0, "studio from the top");
}

void finishingAndSkipping() {
    const TourRun run = run1(TourChapter::Visuals);
    TourProgress before = progressDone({TourChapter::Start});
    before.resume = TourSpot::StageTools;
    const TourProgress want = progressDone({TourChapter::Start, TourChapter::Visuals});
    check(tourEnded(before, run, false) == want, "finish ticks");
    check(tourEnded(before, run, true) == want, "skip of a later chapter ticks it");
    check(tourEnded(TourProgress{}, run1(TourChapter::Start), true) == tourFinished(), "skipping START finishes all");
    const TourProgress ticked = tourEnded(TourProgress{}, run1(TourChapter::Start), false);
    check(ticked.done == std::vector<TourChapter>{TourChapter::Start}, "START ticked alone");
}

void replayRules() {
    const auto run = tourReplay({TourChapter::Studio, TourChapter::Visuals});
    check(run && run->chapters == std::vector<TourChapter>{TourChapter::Visuals, TourChapter::Studio}, "tour order");
    check(run && run->replay, "is a replay");
    if (!run) return;
    check(tourEnded(TourProgress{}, *run, true) == TourProgress{}, "replay changes nothing");
    const auto next = tourNextChapter(*run);
    check(next && next->chapters == std::vector<TourChapter>{TourChapter::Studio}, "next chapter");
    check(next && next->replay, "next keeps replay");
    check(next && !tourNextChapter(*next), "no chapter after the last");
    check(!tourReplay({}), "empty replay is none");
}

void proOffer() {
    check(tourOffersPro(run1(TourChapter::Visuals), false), "after visuals");
    check(tourOffersPro(run1(TourChapter::Start), true), "after skipped start");
    check(!tourOffersPro(run1(TourChapter::Start), false), "not after start");
    check(!tourOffersPro(run1(TourChapter::Customize), false), "not after customize");
    TourRun replay = run1(TourChapter::Visuals);
    replay.replay = true;
    check(!tourOffersPro(replay, false), "never after a replay");
}

void roundTripAndUnknownNames() {
    TourProgress progress;
    progress.done = {TourChapter::Start, TourChapter::Studio};
    progress.resume = TourSpot::CustomizeList;
    progress.tips = {GestureTip::SwipeStyles};
    const std::string done = tourEncodeDone(progress);
    const auto resume = tourEncodeResume(progress);
    const std::string tips = tourEncodeTips(progress);
    check(tourDecode(done, resume, tips) == progress, "round trip");
    check(tourDecode(std::nullopt, std::nullopt, std::nullopt) == TourProgress{}, "decode of nothing");
    TourProgress want;
    want.done = {TourChapter::Visuals};
    check(tourDecode(std::string("PLAYER,VISUALS,ANYWHERE,"), std::string("PLAYER:2"), std::string("GONE,,SWIPE")) == want,
          "unknown names ignored");
}

void oldTourMapsOntoNewChapters() {
    const TourFacts vs = visualsShown();
    const TourProgress old =
        tourDecode(std::string("WELCOME,PLAYER,LIBRARY,CUSTOMIZE,LIVE_VIEW,PRO"), std::string("CUSTOMIZE:3"), std::nullopt);
    check(old == progressDone({TourChapter::Start, TourChapter::Customize}), "WELCOME is START, CUSTOMIZE stays");
    check(!old.resume, "old resume dropped");
    check(tourDue(old, vs) == TourChapter::Visuals, "visuals is due");
    check(tourResume(TourChapter::Visuals, old).at == 0, "from the top");
    check(tourDue(tourDecode(std::string(""), std::string(""), std::string("")), vs) == TourChapter::Start, "reset leads with START");
}

void migratedProgressSavesUnderNewNames() {
    const TourProgress old =
        tourDecode(std::string("WELCOME,SETTINGS,STUDIO"), std::nullopt, std::string("SWIPE_STYLES,HOLD_TO_RECORD"));
    check(splitSet(tourEncodeDone(old)) == std::set<std::string>{"START", "STUDIO"}, "new names only");
    check(!tourEncodeResume(old), "no resume");
    check(tourDecode(tourEncodeDone(old), tourEncodeResume(old), tourEncodeTips(old)) == old, "round trip");
}

void legacySeenFinishesEverything() {
    const TourProgress old = tourDecode(std::string("WELCOME"), std::nullopt, std::nullopt, true);
    check(old == tourFinished(), "all chapters done");
    check(!tourDue(old, visualsShown()), "nothing by itself");
}

void coversHoldBack() {
    std::vector<TourCover> covers(5);
    covers[0].firstRun = true;
    covers[1].paywall = true;
    covers[2].pip = true;
    covers[3].permissionRationale = true;
    covers[4].away = true;
    const auto replay = tourReplay({TourChapter::Visuals});
    for (const TourCover& cover : covers) {
        check(tourHeld(cover), "held");
        check(!tourShows(run1(TourChapter::Start), TourProgress{}, cover), "no step under a cover");
        check(!tourShows(replay, TourProgress{}, cover), "no replay step under a cover");
        check(!tourMayTip(GestureTip::DoubleTapSheet, tourFinished(), cover, std::nullopt), "no tip under a cover");
    }
    check(!tourHeld(TourCover{}), "not held");
    check(tourShows(run1(TourChapter::Start), TourProgress{}, TourCover{}), "shows");
    check(tourMayTip(GestureTip::DoubleTapSheet, tourFinished(), TourCover{}, std::nullopt), "may tip");
}

void tipsOff() {
    TourProgress off;
    off.off = true;
    check(!tourDue(off, visualsShown()), "off starts no chapter");
    check(!tourMayTip(GestureTip::SwipeStyles, off, TourCover{}, std::nullopt), "off says no tip");
    check(!tourShows(run1(TourChapter::Visuals), off, TourCover{}), "off holds a run back");
    check(tourShows(tourReplay({TourChapter::Visuals}), off, TourCover{}), "a replay still plays");
    check(tourDecode(tourEncodeDone(off), tourEncodeResume(off), tourEncodeTips(off), false, true) == off, "off round trip");
}

void tipSaidOnce() {
    TourProgress said = tourFinished();
    said.tips = {GestureTip::SwipeStyles};
    check(!tourMayTip(GestureTip::SwipeStyles, said, TourCover{}, std::nullopt), "said once");
    check(!tourMayTip(GestureTip::HoldToRecord, said, TourCover{}, run1(TourChapter::Visuals)), "never over a run");
}

// placeCard

const TourRect kSafe{16, 60, 344, 700};

bool placed(std::optional<TourRect> hole, int w, int h, int x, int y) {
    const TourPoint p = tourPlaceCard(hole, w, h, kSafe, 16);
    if (p.x != x || p.y != y) std::printf("  got %d,%d want %d,%d\n", p.x, p.y, x, y);
    return p.x == x && p.y == y;
}

void cardOnTheSideWithRoom() {
    check(placed(TourRect{200, 640, 260, 700}, 200, 140, 130, 484), "above a tab at the bottom");
    check(placed(TourRect{240, 60, 344, 108}, 200, 140, 144, 124), "below icons at the top right");
    check(placed(TourRect{240, 60, 344, 700}, 200, 140, 24, 310), "beside a tall panel");
}

void noRoomAndNoControl() {
    check(placed(TourRect{16, 60, 344, 700}, 200, 140, 80, 560), "over the bottom when nothing fits");
    check(placed(std::nullopt, 200, 140, 80, 310), "no control centres it");
}

void cardLargerThanSafeArea() { check(placed(std::nullopt, 400, 900, 16, 60), "corner"); }

}  // namespace

int main() {
    everyChapterHasSteps();
    missingControlIsPassedOver();
    chapterWithNoControlHasNoStep();
    firstRunSkipsOpenLibraryOnLibrary();
    firstRunAsksForLibraryElsewhere();
    playingEndsTheWaitAndVisualsEndsTheRun();
    aPointerIsNeverReachedByItself();
    stepCountStaysPut();
    dueOrder();
    resumePicksUp();
    finishingAndSkipping();
    replayRules();
    proOffer();
    roundTripAndUnknownNames();
    oldTourMapsOntoNewChapters();
    migratedProgressSavesUnderNewNames();
    legacySeenFinishesEverything();
    coversHoldBack();
    tipsOff();
    tipSaidOnce();
    cardOnTheSideWithRoom();
    noRoomAndNoControl();
    cardLargerThanSafeArea();
    if (failures == 0) std::printf("shell_tour_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
