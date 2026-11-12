// Host-side checks for Auto's style timer (core/viz/AutoTimer, geode_auto_timer_*) and for the duration a
// .milk preset's `progress` runs over (core/viz/MilkProgress). No GL, no device.
//
// What is pinned:
// - the wait is the user's interval plus a spread drawn per look: never shorter than the interval, never
//   longer than a quarter more (10 s at most), and not the same from look to look (MilkDrop: 16 s + rand 10 s);
// - a timer that is not running (off, paused, nothing to step to) is never due and holds nothing;
// - the interval is read on every poll, and a restart's new wait is the hold at once, so a preset loaded
//   right after the step runs its progress over the wait that will really pass;
// - a preset's progress runs over Auto's hold; held (Auto off) and in an export it stays at 0, as MilkDrop's
//   does on a locked preset;
// - Auto's random draw (core/viz/AutoDraw) never brings back the entry on screen while there is another, so the
//   same .milk (or style, or look) cannot come up twice running, and it still reaches every other entry.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <set>

#include "api/geode_api.h"
#include "viz/AutoDraw.hpp"
#include "viz/AutoTimer.hpp"
#include "viz/MilkProgress.hpp"

namespace auto_timer = geode::viz::auto_timer;
namespace milk = geode::viz::milk;
namespace auto_draw = geode::viz::auto_draw;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

void spreadIsModest() {
    check(near(auto_timer::spreadSeconds(5.0), 1.25), "5 s: a quarter");
    check(near(auto_timer::spreadSeconds(20.0), 5.0), "20 s (the default): 5 s, MilkDrop's F3 spread");
    check(near(auto_timer::spreadSeconds(40.0), 10.0), "40 s: 10 s, MilkDrop's default spread");
    check(near(auto_timer::spreadSeconds(300.0), 10.0), "300 s: capped at 10 s");
    check(near(auto_timer::spreadSeconds(0.0), 0.0) && near(auto_timer::spreadSeconds(-3.0), 0.0), "no interval, no spread");
}

// Steps one timer through `looks` looks at a 100 ms poll and returns each distinct wait in ms.
std::set<std::int64_t> waitsOf(auto_timer::Timer& timer, double intervalS, int looks, std::int64_t* minMs, std::int64_t* maxMs) {
    std::set<std::int64_t> seen;
    std::int64_t now = 1'000'000;
    timer.restart(now);
    std::int64_t start = now;
    *minMs = std::numeric_limits<std::int64_t>::max();
    *maxMs = 0;
    for (int look = 0; look < looks; ++look) {
        do {
            now += 100;
        } while (!timer.due(now, intervalS, true));
        const std::int64_t waited = now - start;
        *minMs = std::min(*minMs, waited);
        *maxMs = std::max(*maxMs, waited);
        seen.insert(waited);
        start = now;
    }
    return seen;
}

void waitIsIntervalPlusSpread() {
    for (const double interval : {5.0, 20.0, 60.0, 300.0}) {
        auto_timer::Timer timer(42);
        std::int64_t minMs = 0;
        std::int64_t maxMs = 0;
        const auto seen = waitsOf(timer, interval, 60, &minMs, &maxMs);
        const double spread = auto_timer::spreadSeconds(interval);
        check(static_cast<double>(minMs) >= interval * 1000.0, "never sooner than the interval");
        check(static_cast<double>(maxMs) <= (interval + spread) * 1000.0 + 100.0, "never later than the interval plus its spread");
        check(seen.size() >= 8, "the wait differs from look to look");
        check(static_cast<double>(maxMs - minMs) >= spread * 500.0, "the draws use at least half the spread");
    }
}

void sameSeedSameWaits() {
    auto_timer::Timer a(7);
    auto_timer::Timer b(7);
    auto_timer::Timer c(8);
    bool same = true;
    bool differs = false;
    for (int i = 0; i < 16; ++i) {
        a.restart(i);
        b.restart(i);
        c.restart(i);
        same = same && a.waitSeconds(20.0) == b.waitSeconds(20.0);
        differs = differs || a.waitSeconds(20.0) != c.waitSeconds(20.0);
    }
    check(same, "one seed, one sequence of waits");
    check(differs, "another seed, another sequence");
}

void notRunningIsNeverDue() {
    auto_timer::Timer timer(3);
    timer.restart(0);
    check(!timer.due(10'000'000, 20.0, false), "off or paused: never due, however long it has been");
    check(timer.holdSeconds() == 0.0, "and it holds nothing");
    check(timer.due(10'000'000, 20.0, true), "running again past the wait: due");
    check(!timer.due(10'000'100, 20.0, true), "which restarted the wait");
    check(!timer.due(10'019'900, 20.0, true), "so nothing before another interval");
    check(timer.due(10'025'100, 20.0, true), "and due again once interval and spread have passed");
}

void holdIsTheWaitOfThisLook() {
    auto_timer::Timer timer(11);
    timer.restart(0);
    check(timer.holdSeconds() == 0.0, "never polled: nothing held");
    timer.due(100, 20.0, true);
    check(timer.holdSeconds() >= 20.0 && timer.holdSeconds() <= 25.0, "running: the hold is the interval plus the spread");
    check(timer.holdSeconds() == timer.waitSeconds(20.0), "exactly the wait due() measures");
    const double before = timer.holdSeconds();
    timer.restart(200);
    check(timer.holdSeconds() == timer.waitSeconds(20.0), "a restart's new wait is the hold at once, before the next poll");
    check(timer.holdSeconds() != before, "and it is a new draw");
    timer.due(300, 60.0, true);
    check(timer.holdSeconds() >= 60.0 && timer.holdSeconds() <= 70.0, "the interval slider reaches the wait in progress");
    check(!timer.due(59'000, 60.0, true), "which then runs by the new interval");
    check(timer.due(59'000, 5.0, true), "and a shorter one is due as soon as it has passed");
}

void theAbi() {
    const long long t0 = 5'000'000;
    geode_auto_timer_restart(GEODE_AUTO_TIMER_PLAYLIST, t0);
    geode_auto_timer_restart(GEODE_AUTO_TIMER_RANDOM, t0);
    check(geode_auto_timer_due(GEODE_AUTO_TIMER_PLAYLIST, t0 + 1, 30, 0) == 0, "playlist off");
    check(geode_auto_timer_due(GEODE_AUTO_TIMER_RANDOM, t0 + 1, 20, 0) == 0, "random off");
    check(geode_auto_timer_hold_s() == 0.0, "no timer running: nothing held");
    check(geode_auto_timer_due(GEODE_AUTO_TIMER_RANDOM, t0 + 500, 20, 1) == 0, "random on, just restarted: not due");
    check(geode_auto_timer_hold_s() >= 20.0 && geode_auto_timer_hold_s() <= 25.0, "the random timer's hold");
    check(geode_auto_timer_due(GEODE_AUTO_TIMER_RANDOM, t0 + 19'900, 20, 1) == 0, "not before the interval");
    check(geode_auto_timer_due(GEODE_AUTO_TIMER_RANDOM, t0 + 25'100, 20, 1) == 1, "due after interval and spread");
    check(geode_auto_timer_due(GEODE_AUTO_TIMER_RANDOM, t0 + 25'200, 20, 1) == 0, "once");
    check(geode_auto_timer_due(GEODE_AUTO_TIMER_RANDOM, t0 + 25'300, 20, 0) == 0 && geode_auto_timer_hold_s() == 0.0,
          "switched off: nothing held");
    check(geode_auto_timer_due(GEODE_AUTO_TIMER_PLAYLIST, t0 + 25'400, 30, 1) == 0, "the playlist's own wait, from its own restart");
    check(geode_auto_timer_hold_s() >= 30.0 && geode_auto_timer_hold_s() <= 37.5, "the playlist timer's hold");
    check(geode_auto_timer_due(GEODE_AUTO_TIMER_PLAYLIST, t0 + 37'600, 30, 1) == 1, "due on its own clock");
    geode_auto_timer_due(GEODE_AUTO_TIMER_PLAYLIST, t0 + 37'700, 30, 0);
    geode_auto_timer_restart(7, t0);
    check(geode_auto_timer_due(7, t0 + 999'999, 5, 1) == 0 && geode_auto_timer_due(-1, t0 + 999'999, 5, 1) == 0,
          "an unknown timer is never due");
    check(geode_auto_timer_hold_s() == 0.0, "and holds nothing");
}

void milkProgressDuration() {
    check(milk::progressSeconds(23.4, false) == 23.4, "Auto's timer runs: progress goes 0 to 1 over the wait");
    check(milk::progressSeconds(0.0, false) == 30.0, "Auto off: a held preset runs over projectM's 30 s");
    check(milk::progressSeconds(23.4, true) == milk::kHeldSeconds, "an export never reads the live timer");
    check(milk::progressSeconds(-1.0, false) == milk::kHeldSeconds, "a negative hold is no hold");
    check(milk::progressSeconds(std::nan(""), false) == milk::kHeldSeconds, "nor is NaN");
    check(milk::progressSeconds(0.2, false) == milk::kMinSeconds, "projectM's floor of one second");
}

void milkProgressOfAHeldPreset() {
    const double held = milk::progressSeconds(0.0, false);
    check(milk::progressAt(0.0, held) == 0.0, "a held preset starts at 0");
    check(milk::progressAt(15.0, held) == 0.5, "is halfway at 15 s");
    check(milk::progressAt(30.0, held) == 1.0, "reaches 1 at 30 s");
    check(milk::progressAt(3600.0, held) == 1.0, "and holds at 1 for as long as it stays");
    double last = -1.0;
    bool rising = true;
    for (int frame = 0; frame <= 30 * 60; ++frame) {
        const double p = milk::progressAt(frame / 60.0, held);
        rising = rising && p > last;
        last = p;
    }
    check(rising && last == 1.0, "rising every frame on the way");
    check(milk::progressAt(-1.0, held) == 0.0, "never below 0");
    check(milk::progressAt(30.0, milk::progressSeconds(20.0, false)) == 1.0, "a preset that outstays Auto's wait holds at 1");
    check(milk::progressAt(0.5, 0.0) == 0.5, "the floor guards a zero duration");
}

}  // namespace

void drawNeverRepeatsTheScreen() {
    auto_draw::Draw draw(7);
    std::set<int> seen;
    for (int i = 0; i < 2000; ++i) {
        const int idx = draw.pick(5, 2);
        check(idx >= 0 && idx < 5, "a draw lands in the pool");
        check(idx != 2, "never the entry on screen");
        seen.insert(idx);
    }
    check(seen.size() == 4, "every other entry still comes up");
    int on = 0;
    for (int i = 0; i < 2000; ++i) {
        const int next = draw.pick(3, on);
        check(next != on, "a chain of draws never repeats one");
        on = next;
    }
    check(draw.pick(1, 0) == 0, "a pool of one: that one, there is nothing else");
    check(draw.pick(0, -1) == -1, "an empty pool: nothing");
    std::set<int> any;
    for (int i = 0; i < 400; ++i) any.insert(draw.pick(4, -1));
    check(any.size() == 4, "nothing on screen: the whole pool");
    std::set<int> past;
    for (int i = 0; i < 400; ++i) past.insert(draw.pick(4, 9));
    check(past.size() == 4, "an on-screen index past the pool is nothing on screen");
    auto_draw::Draw a(42);
    auto_draw::Draw b(42);
    bool same = true;
    for (int i = 0; i < 50; ++i) same = same && a.pick(9, 1) == b.pick(9, 1);
    check(same, "the same seed draws the same");
    check(geode_auto_pick(3, 1) != 1 && geode_auto_pick(0, -1) == -1, "the C ABI");
}

int main() {
    spreadIsModest();
    waitIsIntervalPlusSpread();
    sameSeedSameWaits();
    notRunningIsNeverDue();
    holdIsTheWaitOfThisLook();
    theAbi();
    milkProgressDuration();
    milkProgressOfAHeldPreset();
    drawNeverRepeatsTheScreen();
    if (failures == 0) std::printf("auto_timer_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
