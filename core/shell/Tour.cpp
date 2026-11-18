#include "shell/Tour.hpp"

#include <algorithm>
#include <array>

namespace geode::shell {

namespace {

struct SpotInfo {
    TourChapter chapter;
    TourStepKind kind;
    const char* name;
};

// Declaration order of TourSpot.
constexpr std::array<SpotInfo, kTourSpotCount> kSpots{{
    {TourChapter::Start, TourStepKind::Go, "NAV_LIBRARY"},
    {TourChapter::Start, TourStepKind::Wait, "PLAY_SONG"},
    {TourChapter::Start, TourStepKind::Go, "NAV_VISUALS"},
    {TourChapter::Visuals, TourStepKind::Point, "VISUALS_TABS"},
    {TourChapter::Visuals, TourStepKind::Point, "STAGE_ARROWS"},
    {TourChapter::Visuals, TourStepKind::Point, "STAGE_TOOLS"},
    {TourChapter::Visuals, TourStepKind::Point, "STAGE_RECORD"},
    {TourChapter::Customize, TourStepKind::Point, "CUSTOMIZE_KNOBS"},
    {TourChapter::Customize, TourStepKind::Point, "CUSTOMIZE_LIST"},
    {TourChapter::Customize, TourStepKind::Point, "CUSTOMIZE_RANDOMIZE"},
    {TourChapter::Studio, TourStepKind::Point, "STUDIO_RENDER"},
    {TourChapter::Studio, TourStepKind::Point, "STUDIO_MY_VIDEOS"},
}};

constexpr std::array<const char*, kTourChapterCount> kChapterNames{"START", "VISUALS", "CUSTOMIZE", "STUDIO"};
constexpr std::array<const char*, kGestureTipCount> kTipNames{"SWIPE_STYLES",     "DOUBLE_TAP_SHEET",  "HOLD_TO_RECORD",
                                                              "SWIPE_UP_PLAYER", "SWIPE_DOWN_KNOBS", "SWIRL_RANDOMIZE"};

constexpr const char* kLegacyWelcome = "WELCOME";

const SpotInfo& info(TourSpot s) { return kSpots[static_cast<size_t>(s)]; }

template <class T>
bool has(const std::vector<T>& v, T x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

// Set add: keeps the first place of a value already there (Kotlin's `set + x`).
template <class T>
void addOnce(std::vector<T>& v, T x) {
    if (!has(v, x)) v.push_back(x);
}

template <class T>
bool sameSet(const std::vector<T>& a, const std::vector<T>& b) {
    for (const T& x : a)
        if (!has(b, x)) return false;
    for (const T& x : b)
        if (!has(a, x)) return false;
    return true;
}

std::vector<TourChapter> allChapters() {
    std::vector<TourChapter> all;
    for (int c = 0; c < kTourChapterCount; ++c) all.push_back(static_cast<TourChapter>(c));
    return all;
}

// Kotlin's String.split(',') then lookup by name; an empty piece or unknown name maps to nothing.
std::vector<std::string> names(const std::optional<std::string>& joined) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : joined.value_or(std::string())) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::optional<TourChapter> chapterNamed(const std::string& name) {
    if (name == kLegacyWelcome) return TourChapter::Start;
    for (int c = 0; c < kTourChapterCount; ++c)
        if (name == kChapterNames[static_cast<size_t>(c)]) return static_cast<TourChapter>(c);
    return std::nullopt;
}

int indexIn(const std::vector<TourSpot>& spots, TourSpot s) {
    const auto it = std::find(spots.begin(), spots.end(), s);
    return it == spots.end() ? -1 : static_cast<int>(it - spots.begin());
}

}  // namespace

TourChapter tourSpotChapter(TourSpot s) { return info(s).chapter; }
TourStepKind tourSpotKind(TourSpot s) { return info(s).kind; }

std::vector<TourSpot> tourChapterSpots(TourChapter c) {
    std::vector<TourSpot> out;
    for (int s = 0; s < kTourSpotCount; ++s)
        if (kSpots[static_cast<size_t>(s)].chapter == c) out.push_back(static_cast<TourSpot>(s));
    return out;
}

bool operator==(const TourProgress& a, const TourProgress& b) {
    return a.off == b.off && a.resume == b.resume && sameSet(a.done, b.done) && sameSet(a.tips, b.tips);
}

TourProgress tourFinished() {
    TourProgress p;
    p.done = allChapters();
    return p;
}

bool tourReached(TourSpot spot, const TourFacts& facts) {
    switch (spot) {
        case TourSpot::NavLibrary: return facts.place != TourPlace::Elsewhere || facts.playing;
        case TourSpot::PlaySong: return facts.playing || facts.place == TourPlace::Visuals;
        case TourSpot::NavVisuals: return facts.place == TourPlace::Visuals;
        default: return false;
    }
}

bool tourAvailable(TourSpot spot, const TourFacts& facts) {
    const bool shown = (facts.shown & tourSpotBit(spot)) != 0;
    switch (tourSpotKind(spot)) {
        case TourStepKind::Point: return shown;
        case TourStepKind::Go: return shown && !tourReached(spot, facts);
        case TourStepKind::Wait: return !tourReached(spot, facts);
    }
    return false;
}

std::optional<TourStep> tourStep(const TourRun& run, const TourFacts& facts) {
    if (run.chapters.empty()) return std::nullopt;
    const std::vector<TourSpot> spots = tourChapterSpots(run.chapters.front());
    // Kotlin's drop(at) throws below zero; no caller makes one, so it reads as 0 here.
    const size_t from = static_cast<size_t>(std::max(run.at, 0));
    std::optional<TourSpot> first;
    int pointers = 0;
    for (size_t i = from; i < spots.size(); ++i) {
        if (!tourAvailable(spots[i], facts)) continue;
        if (!first) first = spots[i];
        if (tourSpotKind(spots[i]) != TourStepKind::Wait) ++pointers;
    }
    if (!first) return std::nullopt;
    TourStep step;
    step.spot = *first;
    step.index = run.seen + 1;
    step.count = run.seen + pointers;
    return step;
}

TourRun tourPass(const TourRun& run, TourSpot spot) {
    TourRun out = run;
    out.at = indexIn(tourChapterSpots(tourSpotChapter(spot)), spot) + 1;
    out.seen = run.seen + (tourSpotKind(spot) == TourStepKind::Wait ? 0 : 1);
    return out;
}

std::optional<TourRun> tourNextChapter(const TourRun& run) {
    if (run.chapters.size() <= 1) return std::nullopt;
    TourRun next;
    next.chapters.assign(run.chapters.begin() + 1, run.chapters.end());
    next.replay = run.replay;
    return next;
}

bool tourHeld(const TourCover& c) { return c.firstRun || c.paywall || c.pip || c.permissionRationale || c.away; }

bool tourShows(const std::optional<TourRun>& run, const TourProgress& progress, const TourCover& cover) {
    return !tourHeld(cover) && ((run && run->replay) || !progress.off);
}

bool tourMayTip(GestureTip tip, const TourProgress& progress, const TourCover& cover, const std::optional<TourRun>& run) {
    return !has(progress.tips, tip) && !progress.off && !run && !tourHeld(cover);
}

std::optional<TourChapter> tourDue(const TourProgress& progress, const TourFacts& facts) {
    if (progress.off) return std::nullopt;
    if (!has(progress.done, TourChapter::Start)) return TourChapter::Start;
    for (int c = 0; c < kTourChapterCount; ++c) {
        const auto ch = static_cast<TourChapter>(c);
        if (has(progress.done, ch)) continue;
        for (TourSpot s : tourChapterSpots(ch))
            if (facts.shown & tourSpotBit(s)) return ch;
    }
    return std::nullopt;
}

TourRun tourResume(TourChapter chapter, const TourProgress& progress) {
    TourRun run;
    run.chapters = {chapter};
    if (progress.resume && tourSpotChapter(*progress.resume) == chapter)
        run.at = indexIn(tourChapterSpots(chapter), *progress.resume);
    return run;
}

std::optional<TourRun> tourReplay(const std::vector<TourChapter>& chapters) {
    TourRun run;
    for (TourChapter c : allChapters())
        if (has(chapters, c)) run.chapters.push_back(c);
    if (run.chapters.empty()) return std::nullopt;
    run.replay = true;
    return run;
}

TourProgress tourEnded(const TourProgress& progress, const TourRun& run, bool skipped) {
    if (run.replay || run.chapters.empty()) return progress;
    const TourChapter chapter = run.chapters.front();
    TourProgress out = progress;
    out.resume.reset();
    if (skipped && chapter == TourChapter::Start)
        out.done = allChapters();
    else
        addOnce(out.done, chapter);
    return out;
}

bool tourOffersPro(const TourRun& run, bool skipped) {
    if (run.replay || run.chapters.empty()) return false;
    const TourChapter c = run.chapters.front();
    return c == TourChapter::Visuals || (skipped && c == TourChapter::Start);
}

TourProgress tourDecode(const std::optional<std::string>& done, const std::optional<std::string>& resume,
                        const std::optional<std::string>& tips, bool legacySeen, bool off) {
    TourProgress p;
    if (legacySeen) {
        p.done = allChapters();
    } else {
        for (const std::string& n : names(done))
            if (const auto c = chapterNamed(n)) addOnce(p.done, *c);
    }
    if (resume) {
        for (int s = 0; s < kTourSpotCount; ++s)
            if (*resume == kSpots[static_cast<size_t>(s)].name) p.resume = static_cast<TourSpot>(s);
    }
    for (const std::string& n : names(tips)) {
        for (int t = 0; t < kGestureTipCount; ++t)
            if (n == kTipNames[static_cast<size_t>(t)]) addOnce(p.tips, static_cast<GestureTip>(t));
    }
    p.off = off;
    return p;
}

std::string tourEncodeDone(const TourProgress& progress) {
    std::string out;
    for (TourChapter c : progress.done) {
        if (!out.empty()) out += ',';
        out += kChapterNames[static_cast<size_t>(c)];
    }
    return out;
}

std::optional<std::string> tourEncodeResume(const TourProgress& progress) {
    if (!progress.resume) return std::nullopt;
    return std::string(info(*progress.resume).name);
}

std::string tourEncodeTips(const TourProgress& progress) {
    std::string out;
    for (GestureTip t : progress.tips) {
        if (!out.empty()) out += ',';
        out += kTipNames[static_cast<size_t>(t)];
    }
    return out;
}

TourPoint tourPlaceCard(const std::optional<TourRect>& hole, int cardWidth, int cardHeight, const TourRect& safe, int gap) {
    const int maxX = std::max(safe.right - cardWidth, safe.left);
    const int maxY = std::max(safe.bottom - cardHeight, safe.top);
    if (!hole) return {(safe.left + maxX) / 2, (safe.top + maxY) / 2};
    // coerceIn(lo, hi): lo <= hi holds here (max above).
    const int centredX = std::clamp((hole->left + hole->right - cardWidth) / 2, safe.left, maxX);
    const int centredY = std::clamp((hole->top + hole->bottom - cardHeight) / 2, safe.top, maxY);
    struct Side {
        int room;
        int x;
        int y;
    };
    const Side sides[4] = {
        {safe.bottom - hole->bottom - gap - cardHeight, centredX, hole->bottom + gap},
        {hole->top - safe.top - gap - cardHeight, centredX, hole->top - gap - cardHeight},
        {safe.right - hole->right - gap - cardWidth, hole->right + gap, centredY},
        {hole->left - safe.left - gap - cardWidth, hole->left - gap - cardWidth, centredY},
    };
    // maxBy: the first of equal rooms wins.
    const Side* best = &sides[0];
    for (const Side& s : sides)
        if (s.room > best->room) best = &s;
    if (best->room >= 0) return {best->x, best->y};
    return {centredX, maxY};
}

}  // namespace geode::shell
