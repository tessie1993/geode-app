#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The onboarding tour's rules, ported from Kotlin model/Tour.kt: which step a run shows given what the app
// reads now, what a missing control does, what ending a chapter remembers, what holds the tour back, how the
// remembered progress is spelled in the prefs, and where the step's card goes around the lit control.
// Pure: the same run and facts always give the same answer. The shell hands in what it reads (the marked
// controls laid out now as a bit set, the screen, the player, what covers the app) and the sizes of the card,
// the lit control and the safe area as whole numbers (pixels or dp, whichever it measured in: the maths does
// not care, only that all of them are the same unit). Host-tested in core/tests/shell_tour_test.cpp.
namespace geode::shell {

// The chapters, in the order the tour plays them.
enum class TourChapter : int { Start, Visuals, Customize, Studio };
inline constexpr int kTourChapterCount = 4;

// How a step ends: a pointer lights a control and waits for Next; a go lights a control the user is asked to
// use and ends once they have; a wait is a line over a usable app, gone once the user has done the thing.
enum class TourStepKind : int { Point, Go, Wait };

// One step and the real control it is about; each chapter's steps play in this order (also the bit index in
// TourFacts::shown).
enum class TourSpot : int {
    NavLibrary,
    PlaySong,
    NavVisuals,
    VisualsTabs,
    StageArrows,
    StageTools,
    StageRecord,
    CustomizeKnobs,
    CustomizeList,
    CustomizeRandomize,
    StudioRender,
    StudioMyVideos,
};
inline constexpr int kTourSpotCount = 12;

enum class GestureTip : int { SwipeStyles, DoubleTapSheet, HoldToRecord, SwipeUpPlayer, SwipeDownKnobs, SwirlRandomize };
inline constexpr int kGestureTipCount = 6;

enum class TourPlace : int { Library, Visuals, Elsewhere };

constexpr std::uint32_t tourSpotBit(TourSpot s) { return std::uint32_t{1} << static_cast<int>(s); }

TourChapter tourSpotChapter(TourSpot s);
TourStepKind tourSpotKind(TourSpot s);
// The chapter's steps in play order.
std::vector<TourSpot> tourChapterSpots(TourChapter c);

// What the tour reads from the app. shown: the bits (tourSpotBit) of the marked controls laid out now.
struct TourFacts {
    std::uint32_t shown = 0;
    TourPlace place = TourPlace::Elsewhere;
    bool playing = false;
};

// What can be over the app; each one holds every pointer and tip back.
struct TourCover {
    bool firstRun = false;
    bool paywall = false;
    bool pip = false;
    bool permissionRationale = false;
    // The app is not in front: a system window is over it.
    bool away = false;
};

// Everything remembered between launches. done and tips are sets that keep the order they were added in (the
// order they are written in); == compares them as sets, as Kotlin's Set equality does.
struct TourProgress {
    std::vector<TourChapter> done;
    std::optional<TourSpot> resume;
    std::vector<GestureTip> tips;
    // No gesture tip and no chapter of its own accord; a replay still plays.
    bool off = false;
};
bool operator==(const TourProgress& a, const TourProgress& b);
inline bool operator!=(const TourProgress& a, const TourProgress& b) { return !(a == b); }

// Every chapter done (TourProgress.Finished).
TourProgress tourFinished();

// A run: the chapters still to play (the head is on screen), the index into the head's steps of the first one
// not yet passed, and how many pointers it has shown. A replay was asked for in Settings.
struct TourRun {
    std::vector<TourChapter> chapters;
    int at = 0;
    int seen = 0;
    bool replay = false;
};

// The step to draw; index of count counts pointers only (a wait has neither).
struct TourStep {
    TourSpot spot = TourSpot::NavLibrary;
    int index = 0;
    int count = 0;
    bool last() const { return index == count; }
};

// Whether the user has already done what the step asks (always false for a pointer).
bool tourReached(TourSpot spot, const TourFacts& facts);
// A pointer needs its control on screen; a step already done is never asked for.
bool tourAvailable(TourSpot spot, const TourFacts& facts);

// The step the run is on: the first one from run.at that can be shown (a missing control is passed over);
// nullopt when the chapter has nothing left to show. (Tour.step)
std::optional<TourStep> tourStep(const TourRun& run, const TourFacts& facts);
// The run with the spot behind it. (Tour.pass)
TourRun tourPass(const TourRun& run, TourSpot spot);
// The run on its next chapter, or nullopt when that was the last. (Tour.nextChapter)
std::optional<TourRun> tourNextChapter(const TourRun& run);

// Whether anything over the app holds the tour back.
bool tourHeld(const TourCover& cover);
// Whether the run (nullopt: none) may show a step now: nothing covers the app, and tips are on unless it is a
// replay. (Tour.shows)
bool tourShows(const std::optional<TourRun>& run, const TourProgress& progress, const TourCover& cover);
// Whether the tip may be said now: not said before, tips on, no run and nothing over the app.
bool tourMayTip(GestureTip tip, const TourProgress& progress, const TourCover& cover, const std::optional<TourRun>& run);

// The chapter that should start by itself, if any: START once on a new install, then each other chapter the
// first time one of its controls is on screen. None while tips are off. (Tour.due)
std::optional<TourChapter> tourDue(const TourProgress& progress, const TourFacts& facts);
// A run of the chapter picking up where it was left. (Tour.resume)
TourRun tourResume(TourChapter chapter, const TourProgress& progress);
// A replay of the chapters in tour order; nullopt for none. (Tour.replay)
std::optional<TourRun> tourReplay(const std::vector<TourChapter>& chapters);
// Progress once the run's chapter has ended: a replay changes nothing; skipping START turns the whole tour off
// (every chapter done); any other ending ticks that chapter. (Tour.ended)
TourProgress tourEnded(const TourProgress& progress, const TourRun& run, bool skipped);
// Whether the one-time Pro offer follows this ending. Never after a replay. (Tour.offersPro)
bool tourOffersPro(const TourRun& run, bool skipped);

// The prefs' spelling of progress: plain names, read leniently (a name a later build dropped is ignored; the
// old build's WELCOME is START). legacySeen: the flag older builds kept for "the first-run tour ended", which
// counts every chapter as done. (TourProgress.decode / encodeDone / encodeResume / encodeTips)
TourProgress tourDecode(const std::optional<std::string>& done, const std::optional<std::string>& resume,
                        const std::optional<std::string>& tips, bool legacySeen = false, bool off = false);
std::string tourEncodeDone(const TourProgress& progress);
std::optional<std::string> tourEncodeResume(const TourProgress& progress);
std::string tourEncodeTips(const TourProgress& progress);

struct TourRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};
struct TourPoint {
    int x = 0;
    int y = 0;
};

// Where the card goes: the side of the lit control (hole) with the most room that fits it, a gap away and
// centred on it; over the control's bottom only when no side fits. Always inside safe. With no control it is
// centred. Returns the card's left and top. Whole-number maths exactly as Kotlin's Int (division truncates
// toward zero). (Tour.placeCard)
TourPoint tourPlaceCard(const std::optional<TourRect>& hole, int cardWidth, int cardHeight, const TourRect& safe, int gap);

}  // namespace geode::shell
