// Host-side checks for what pause, seek and a track change do to a capture take (core/output/TakeRules, #459).
// Built and run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler.
//
// What is pinned: the default is pause holds, seek cuts, a new track ends and saves; Continuous rolls through
// everything; the third rule holds on pause and ends on any seek. A hold stops the take's clock for picture and
// sound alike, so the file runs on after it without a gap and the two stay in step.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "output/TakeClock.hpp"
#include "output/TakeRules.hpp"

using geode::output::TakeAction;
using geode::output::TakeAudio;
using geode::output::TakeEvent;
using geode::output::TakeFramePicker;
using geode::output::TakeRule;
using geode::output::TakeRules;
using geode::output::takeRuleOf;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int64_t kMs = 1'000'000;  // ns

void theDefaultHoldsCutsAndEnds() {
    check(takeRuleOf(0) == TakeRule::PauseHoldsSeekCuts, "rule 0 is the default");
    check(takeRuleOf(7) == TakeRule::PauseHoldsSeekCuts, "an unknown rule reads as the default");
    TakeRules r;
    check(r.rule() == TakeRule::PauseHoldsSeekCuts, "a take with no setting runs the default");
    check(r.on(TakeEvent::Pause) == TakeAction::Hold, "default: pause holds the capture");
    check(r.holding(), "default: held while paused");
    check(r.on(TakeEvent::Pause) == TakeAction::Roll, "a second pause changes nothing");
    check(r.on(TakeEvent::Seek) == TakeAction::Cut && r.holding(), "a seek while held is a cut and stays held");
    check(r.on(TakeEvent::Resume) == TakeAction::Release && !r.holding(), "default: resume releases the hold");
    check(r.on(TakeEvent::Resume) == TakeAction::Roll, "a resume while running changes nothing");
    check(r.on(TakeEvent::Seek) == TakeAction::Cut, "default: a seek is a hard cut");
    check(r.on(TakeEvent::TrackChange) == TakeAction::End && r.ended(), "default: a track change ends and saves");
    check(r.on(TakeEvent::Pause) == TakeAction::Roll, "an ended take hears nothing more");
}

void continuousRollsThroughEverything() {
    TakeRules r(TakeRule::Continuous);
    const TakeEvent all[] = {TakeEvent::Pause, TakeEvent::Seek, TakeEvent::Resume, TakeEvent::TrackChange, TakeEvent::Seek};
    bool rolled = true;
    for (TakeEvent e : all) rolled = rolled && r.on(e) == TakeAction::Roll;
    check(rolled, "continuous: pause, seek and a new track all keep rolling");
    check(!r.holding() && !r.ended(), "continuous: never held, never ended: a take can span tracks");
}

void pauseHoldsAndAnySeekEnds() {
    TakeRules r(TakeRule::PauseHoldsSeekEnds);
    check(r.on(TakeEvent::Pause) == TakeAction::Hold, "seek-ends: pause holds");
    check(r.on(TakeEvent::Resume) == TakeAction::Release, "seek-ends: resume releases");
    check(r.on(TakeEvent::Seek) == TakeAction::End && r.ended(), "seek-ends: any seek ends the take");

    TakeRules held(TakeRule::PauseHoldsSeekEnds);
    held.on(TakeEvent::Pause);
    check(held.on(TakeEvent::Seek) == TakeAction::End, "seek-ends: a seek while held ends it too");

    TakeRules track(TakeRule::PauseHoldsSeekEnds);
    check(track.on(TakeEvent::TrackChange) == TakeAction::End, "seek-ends: a new track ends the take");
}

void aHoldStopsThePicturesClock() {
    TakeFramePicker picker(0, 60.0f);
    int64_t lastBefore = -1;
    for (int f = 0; f < 60; ++f) {
        if (const auto pts = picker.admit(static_cast<int64_t>(f) * 16'667'000)) lastBefore = *pts;
    }
    picker.hold(1'000 * kMs);
    bool none = true;
    for (int f = 60; f < 240; ++f) none = none && !picker.admit(static_cast<int64_t>(f) * 16'667'000);
    check(none, "no frame is kept while held");
    picker.release(4'000 * kMs);
    const auto first = picker.admit(4'000 * kMs + 16'667'000);
    check(first.has_value(), "frames are kept again after release");
    check(first && *first > lastBefore && *first - lastBefore < 40'000, "the picture runs on after a hold without a gap");
}

void aHoldStopsTheSoundsClockWithIt() {
    const int rate = TakeAudio::kRate;
    TakeAudio audio(0);
    std::vector<int16_t> a(480 * 2, 100);
    for (int i = 0; i < 100; ++i) audio.write(a.data(), 480, 2, rate, i * 10 * kMs);
    std::vector<int16_t> buf(4096 * 2);
    int64_t before = 0;
    while (const int n = audio.read(buf.data(), 4096, 1'000 * kMs)) before += n;
    audio.hold(1'000 * kMs);
    audio.write(a.data(), 480, 2, rate, 2'000 * kMs);
    int64_t during = 0;
    while (const int n = audio.read(buf.data(), 4096, 3'900 * kMs)) during += n;
    check(during == 0, "nothing is written or filled while held");
    audio.release(4'000 * kMs);
    std::vector<int16_t> b(480 * 2, 200);
    audio.write(b.data(), 480, 2, rate, 4'000 * kMs);
    int64_t after = 0;
    int firstNew = -1;
    while (const int n = audio.read(buf.data(), 4096, 4'010 * kMs)) {
        for (int i = 0; i < n && firstNew < 0; ++i) {
            if (buf[static_cast<size_t>(i) * 2] == 200) firstNew = static_cast<int>(after) + i;
        }
        after += n;
    }
    check(before == 48'000, "a second of sound before the hold");
    check(firstNew == 0, "the first block after a hold follows the last one before it, no silence between");
}

}  // namespace

int main() {
    theDefaultHoldsCutsAndEnds();
    continuousRollsThroughEverything();
    pauseHoldsAndAnySeekEnds();
    aHoldStopsThePicturesClock();
    aHoldStopsTheSoundsClockWithIt();
    if (failures == 0) std::printf("take_rules_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
