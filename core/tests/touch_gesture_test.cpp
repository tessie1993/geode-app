// Host-side checks for core/viz/input/TouchGesture (was VisualGestures.kt): touch play's fingers into y-up NDC, the
// two-finger reading the pinch and twist come from, and touch play's stir. Then the gesture reader
// (core/viz/input/GestureRead: the strips, tap, double tap, hold, swipe, swirl, two fingers) and the gesture map
// (core/viz/input/GestureMap: the owner's defaults, what Settings offers each gesture, the remote's keys), the
// assertions the JVM's VisualGesturesTest and StageKeysTest held, through the C ABI where the shell calls it.
// Built and run by CI (.github/workflows/native-host-tests.yml) with the system compiler; no GL.
#include <cmath>
#include <cstdio>
#include <string>

#include "api/geode_api.h"
#include "viz/input/GestureMap.hpp"
#include "viz/input/GestureRead.hpp"
#include "viz/input/TouchGesture.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

using touch::Action;
using touch::Gesture;

constexpr int G(Gesture g) { return static_cast<int>(g); }
constexpr int A(Action a) { return static_cast<int>(a); }

// A phone's values: 8 px touch slop, 100 px double-tap slop, 2.5 px per dp (a swipe is 180 px, a swirl step 15).
constexpr float kSlop = 8.0f;
constexpr long long kLongPress = 400;
constexpr long long kDoubleMin = 40;
constexpr long long kDoubleTimeout = 300;

// A reader on a 1080 x 2400 window with no strips, the stage at the window's origin, the owner's map.
struct Reader {
    geode_gesture* g = geode_gesture_create();
    int actions[GEODE_GESTURE_COUNT] = {};
    float xy[4] = {};
    float pinch[2] = {1.0f, 0.0f};
    int out[3] = {};
    Reader() {
        for (int i = 0; i < GEODE_GESTURE_COUNT; ++i) actions[i] = geode_gesture_default_action(i);
        geode_gesture_configure(g, kSlop, 100.0f, 2.5f, kLongPress, kDoubleMin, kDoubleTimeout);
        geode_gesture_area(g, 1080.0f, 2400.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1);
    }
    ~Reader() { geode_gesture_destroy(g); }
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    void down(float x, float y, long long t, bool armed = true) { geode_gesture_down(g, x, y, t, actions, armed ? 1 : 0); }
    unsigned one(float x, float y, long long t) {
        xy[0] = x;
        xy[1] = y;
        return geode_gesture_event(g, t, xy, 1, 1, pinch);
    }
    unsigned two(float ax, float ay, float bx, float by, long long t) {
        xy[0] = ax;
        xy[1] = ay;
        xy[2] = bx;
        xy[3] = by;
        return geode_gesture_event(g, t, xy, 2, 1, pinch);
    }
    unsigned lift(long long t) { return geode_gesture_event(g, t, xy, 0, 0, pinch); }
    int end(float split = 1200.0f) { return geode_gesture_end(g, split, out); }
};

void strips() {
    // The safe area is the window less each strip (was VisualGesturesTest).
    const touch::SafeRect safe = touch::safeArea({1080.0f, 2400.0f, 40.0f, 100.0f, 40.0f, 50.0f, 0.0f, 0.0f, true});
    check(safe.left == 40.0f && safe.top == 100.0f && safe.right == 1040.0f && safe.bottom == 2350.0f,
          "the safe area is the window less each strip");
    // The expanded shell puts the stage beside an 80 px rail: stage-local x 10 is window x 90, outside a 40 px strip.
    touch::Area sides{1080.0f, 2400.0f, 40.0f, 0.0f, 40.0f, 0.0f, 80.0f, 0.0f, true};
    check(touch::ours(sides, 10.0f, 1200.0f), "beside the rail, a down at local x 10 is ours");
    sides.originX = 0.0f;
    check(!touch::ours(sides, 10.0f, 1200.0f) && !touch::ours(sides, 1060.0f, 1200.0f),
          "at the window's edge, the side strips are the system's");
    check(touch::ours(sides, 540.0f, 1200.0f), "the middle is ours");
    const touch::Area unplaced{1080.0f, 2400.0f, 40.0f, 100.0f, 40.0f, 60.0f, 0.0f, 0.0f, false};
    check(touch::ours(unplaced, 10.0f, 10.0f) && touch::ours(unplaced, 540.0f, 2380.0f), "before the first placement every down is ours");
    const touch::SafeRect bottom = touch::safeArea({1080.0f, 2400.0f, 0.0f, 0.0f, 0.0f, 60.0f, 0.0f, 0.0f, true});
    check(bottom.contains(540.0f, 2300.0f) && !bottom.contains(540.0f, 2350.0f), "a down in the bottom strip is the system's");
    check(!bottom.contains(540.0f, 2340.0f) && bottom.contains(0.0f, 0.0f), "right and bottom exclusive, left and top inclusive");

    // The double tap's slop and the swirl attempt.
    check(touch::isSecondTap(30.0f, 40.0f, 50.0f) && touch::isSecondTap(0.0f, 0.0f, 50.0f), "a second tap within the slop counts");
    check(!touch::isSecondTap(30.0f, 41.0f, 50.0f) && !touch::isSecondTap(-400.0f, 0.0f, 50.0f), "past the slop it is a new touch");
    const float pi = touch::kPi;
    check(!touch::swirlAttempted(0.0f) && !touch::swirlAttempted(pi / 2.0f) && !touch::swirlAttempted(pi * 0.95f),
          "under half a circle: no attempt");
    check(touch::swirlAttempted(pi * 1.05f) && touch::swirlAttempted(-pi * 1.05f) && touch::swirlAttempted(3.6f * pi),
          "past half a circle, either way: a swirl attempt");
    check(touch::kSwipeDp == 72.0f && touch::kHoldFillMs == 800 && touch::kSwirlStepDp == 6.0f, "the shell's old distances and fill");
    check(touch::kPinchZoom == 0.2f && touch::kPinchTwistRad == 0.5f && touch::kSwirlTurnRad == 3.6f * pi, "the shell's old thresholds");
    check(touch::wrapTurn(3.0f * pi / 2.0f) < 0.0f && touch::wrapTurn(-3.0f * pi / 2.0f) > 0.0f, "a heading step wraps to the short way round");
}

void taps() {
    Reader r;
    r.down(500.0f, 300.0f, 1000);
    check(geode_gesture_wait_ms(r.g, 1000) == kLongPress, "a still finger waits on the hold");
    r.one(502.0f, 301.0f, 1050);
    check(r.lift(1100) & GEODE_GESTURE_OVER, "a lift ends the gesture");
    check(r.end(1200.0f) == G(Gesture::TapTop) && r.out[1] == 1, "above the split: the top tap");
    check(r.out[2] == kDoubleTimeout, "the double tap has an action: wait the double-tap timeout");
    // The second down: a bounce is skipped, then one within the double-tap slop is followed until it lifts.
    check(geode_gesture_second_down(r.g, 510.0f, 310.0f, 1100 + kDoubleMin - 1) == GEODE_GESTURE_SECOND_BOUNCE, "too soon: a bounce");
    check(geode_gesture_second_down(r.g, 560.0f, 300.0f, 1100 + kDoubleMin) == GEODE_GESTURE_SECOND_FOLLOW, "within the slop: followed");
    check(geode_gesture_second_move(r.g, 1, 565.0f, 300.0f, 1) == GEODE_GESTURE_SECOND_FOLLOW, "still down within the touch slop");
    check(geode_gesture_second_move(r.g, 1, 566.0f, 302.0f, 0) == GEODE_GESTURE_SECOND_DOUBLE, "it lifts: a double tap");
    check(geode_gesture_second_move(r.g, 0, 0.0f, 0.0f, 0) == GEODE_GESTURE_SECOND_DOUBLE, "no change for it: a double tap");
    check(geode_gesture_second_move(r.g, 1, 569.0f, 300.0f, 1) == GEODE_GESTURE_SECOND_NEW_TOUCH, "past the touch slop: a new touch");
    check(geode_gesture_second_down(r.g, 500.0f, 401.0f, 1200) == GEODE_GESTURE_SECOND_NEW_TOUCH, "past the double-tap slop: a new touch");

    r.down(500.0f, 1500.0f, 5000);
    r.lift(5100);
    check(r.end(1200.0f) == G(Gesture::TapBottom), "below the split: the bottom tap");
    r.actions[G(Gesture::DoubleTap)] = A(Action::Off);
    r.down(500.0f, 1500.0f, 6000);
    r.lift(6100);
    check(r.end(1200.0f) == G(Gesture::TapBottom) && r.out[2] == 0, "no double-tap action: no wait");
}

void hold() {
    Reader r;
    r.down(500.0f, 1000.0f, 1000);
    check(geode_gesture_tick(r.g) == GEODE_GESTURE_RING, "the timeout: the ring shows");
    check(geode_gesture_wait_ms(r.g, 1000 + kLongPress) == GEODE_GESTURE_HOLD_FILL_MS, "then the fill");
    check(r.one(503.0f, 1000.0f, 1500) == GEODE_GESTURE_RING, "a finger at rest keeps the ring");
    check(geode_gesture_wait_ms(r.g, 1500) == 700, "the deadline counts from the down, not the event");
    check(geode_gesture_tick(r.g) == GEODE_GESTURE_HOLD, "the ring closes: the hold fires");
    check(geode_gesture_wait_ms(r.g, 2200) == -1, "nothing left to wait for");
    r.lift(2300);
    check(r.end() == -1, "the lift after a hold means nothing");

    r.down(500.0f, 1000.0f, 3000);
    geode_gesture_tick(r.g);
    r.lift(3500);
    check(r.end() == -1, "a lift once the ring shows does nothing");

    r.down(500.0f, 1000.0f, 4000);
    geode_gesture_tick(r.g);
    check(r.one(520.0f, 1000.0f, 4450) == 0 && geode_gesture_wait_ms(r.g, 4450) == -1, "a move past the slop drops the ring and the timer");

    r.down(500.0f, 1000.0f, 5000, false);
    check(geode_gesture_wait_ms(r.g, 5000) == -1, "the hold not armed: no timer");
    r.lift(5000 + kLongPress);
    check(r.end() == G(Gesture::TapTop), "a press of exactly the timeout is still a tap");
    r.down(500.0f, 1000.0f, 6000, false);
    r.lift(6000 + kLongPress + 1);
    check(r.end() == -1, "a press past the timeout is no tap, ring or not");
    r.actions[G(Gesture::LongPress)] = A(Action::Off);
    r.down(500.0f, 1000.0f, 7000);
    check(geode_gesture_wait_ms(r.g, 7000) == -1, "the hold with no action: no timer");
}

// A slow main thread: the timeouts come late, or not at all before the next event. Each event's own time decides.
void lateHold() {
    Reader r;
    const long long closes = kLongPress + GEODE_GESTURE_HOLD_FILL_MS;
    r.down(500.0f, 1000.0f, 0);
    check(r.lift(closes + 50) == (GEODE_GESTURE_HOLD | GEODE_GESTURE_OVER), "a lift past the ring's close, no tick: the hold fires");
    check(r.end() == -1, "and the lift means nothing more");

    r.down(500.0f, 1000.0f, 0);
    check(r.one(502.0f, 1000.0f, closes + 50) == GEODE_GESTURE_HOLD, "a finger at rest past the close, no tick: the hold fires");
    check(r.lift(closes + 100) == GEODE_GESTURE_OVER, "the hold fires once");

    r.down(500.0f, 1000.0f, 0);
    check(r.one(502.0f, 1000.0f, kLongPress + 100) == GEODE_GESTURE_RING, "past the timeout, no tick: the ring shows");
    check(r.lift(kLongPress + 200) == GEODE_GESTURE_OVER && r.end() == -1, "a lift before the close does nothing");

    r.down(500.0f, 1000.0f, 0);
    check(r.lift(kLongPress + 200) == GEODE_GESTURE_OVER && r.end() == -1, "a lift past the timeout, no tick: no tap, no hold");
    r.down(500.0f, 1000.0f, 0);
    check(r.lift(kLongPress) == GEODE_GESTURE_OVER && r.end() == G(Gesture::TapTop), "a lift at exactly the timeout is still a tap");
    r.down(500.0f, 1000.0f, 0);
    check(r.lift(300) == GEODE_GESTURE_OVER && r.end() == G(Gesture::TapTop), "a quick lift is a tap");

    r.down(500.0f, 1000.0f, 0);
    check(r.two(500.0f, 1000.0f, 700.0f, 1000.0f, closes + 50) & GEODE_GESTURE_HOLD, "a second finger past the close: the hold had fired");

    // The wait counts from the caller's now: a follow loop that starts late does not wait its delay again.
    r.down(500.0f, 1000.0f, 0);
    check(geode_gesture_wait_ms(r.g, 0) == kLongPress && geode_gesture_wait_ms(r.g, 300) == kLongPress - 300, "the wait shrinks as now grows");
    check(geode_gesture_wait_ms(r.g, kLongPress + 50) == 1, "overdue: tick at once");
    check(geode_gesture_tick(r.g) == GEODE_GESTURE_RING, "a late timeout: the ring shows");
    check(geode_gesture_wait_ms(r.g, kLongPress + 300) == GEODE_GESTURE_HOLD_FILL_MS - 300, "the fill counts from the down's deadline, not the late tick");
    check(geode_gesture_tick(r.g) == GEODE_GESTURE_HOLD && geode_gesture_wait_ms(r.g, closes) == -1, "the ring closes: the hold fires");
    r.down(500.0f, 1000.0f, 0);
    check(r.one(502.0f, 1000.0f, kLongPress + 100) == GEODE_GESTURE_RING && geode_gesture_wait_ms(r.g, kLongPress + 100) == GEODE_GESTURE_HOLD_FILL_MS - 100,
          "a ring caught up by an event keeps the event's time");
}

void swipes() {
    Reader r;
    const auto swipe = [&](float dx, float dy) {
        r.down(540.0f, 1200.0f, 1000);
        r.one(540.0f + dx * 0.5f, 1200.0f + dy * 0.5f, 1020);
        r.one(540.0f + dx, 1200.0f + dy, 1040);
        r.lift(1060);
        return r.end();
    };
    check(swipe(-200.0f, 20.0f) == G(Gesture::SwipeSideways) && r.out[0] == 1, "leftwards: sideways, next");
    check(swipe(200.0f, -20.0f) == G(Gesture::SwipeSideways) && r.out[0] == -1, "rightwards: sideways, previous");
    check(swipe(10.0f, -200.0f) == G(Gesture::SwipeUp) && r.out[0] == 0, "upwards");
    check(swipe(-10.0f, 200.0f) == G(Gesture::SwipeDown), "downwards");
    check(swipe(-170.0f, 0.0f) == -1 && r.out[1] == 0, "short of 72 dp: nothing, and no tap");
    check(swipe(150.0f, 150.0f) == -1, "a diagonal reads as sideways, here short");
    geode_gesture_area(r.g, 1080.0f, 2400.0f, 0.0f, 0.0f, 0.0f, 300.0f, 0.0f, 0.0f, 1);
    r.down(540.0f, 2200.0f, 1000);
    r.one(540.0f, 1900.0f, 1040);
    r.lift(1060);
    check(r.end() == -1, "a swipe from the bottom strip is the system's");
}

void circleAt(int deg, float& x, float& y) {
    const float a = static_cast<float>(deg) * touch::kPi / 180.0f;
    x = 540.0f + 200.0f * std::cos(a);
    y = 1200.0f + 200.0f * std::sin(a);
}

void swirls() {
    Reader r;
    float x = 0.0f, y = 0.0f;
    circleAt(0, x, y);
    r.down(x, y, 1000);
    int firedAt = -1;
    for (int deg = 5; deg <= 800 && firedAt < 0; deg += 5) {
        circleAt(deg, x, y);
        if (r.one(x, y, 1000 + deg) & GEODE_GESTURE_SWIRL) firedAt = deg;
    }
    check(firedAt > 360 && firedAt <= 660, "the swirl fires near two circles, not at one");
    check(!(r.one(x + 1.0f, y, 2000) & GEODE_GESTURE_SWIRL), "it fires once");
    r.lift(2100);
    check(r.end() == -1, "the lift after a swirl means nothing");

    // Most of a circle ends well left of its start: a swipe's length, but a swirl attempt.
    const auto threeQuarters = [&](long long t0) {
        circleAt(0, x, y);
        r.down(x, y, t0);
        for (int deg = 5; deg <= 260; deg += 5) {
            circleAt(deg, x, y);
            r.one(x, y, t0 + deg);
        }
        r.lift(t0 + 300);
        return r.end();
    };
    check(threeQuarters(3000) == -1, "a swirl given up short of two circles never swipes");
    r.actions[G(Gesture::Swirl)] = A(Action::Off);
    check(threeQuarters(4000) == G(Gesture::SwipeSideways), "the swirl with no action: the same path is a swipe");
}

void pairs() {
    Reader r;
    r.down(300.0f, 1000.0f, 1000);
    check(r.two(300.0f, 1000.0f, 500.0f, 1000.0f, 1010) == 0, "the second finger: nothing yet, the ring gone");
    check(geode_gesture_wait_ms(r.g, 1010) == -1, "two fingers never hold");
    check(r.two(305.0f, 1000.0f, 505.0f, 1000.0f, 1020) == 0, "inside the slop: not yet a drag");
    unsigned a = r.two(320.0f, 1000.0f, 520.0f, 1000.0f, 1030);
    check(a == GEODE_GESTURE_PUBLISH, "a drag mapped to touch play hands the fingers down");
    a = r.two(320.0f, 1000.0f, 580.0f, 1000.0f, 1040);
    check((a & GEODE_GESTURE_PINCH) && near(r.pinch[0], 1.3f) && (a & GEODE_GESTURE_RELEASE),
          "spread past 0.2: a pinch from this event's step, and touch play lets go");
    a = r.two(320.0f, 1000.0f, 520.0f, 1000.0f, 1050);
    check((a & GEODE_GESTURE_PINCH) && !(a & (GEODE_GESTURE_PUBLISH | GEODE_GESTURE_RELEASE)), "a pinch stays a pinch");
    check(r.two(320.0f, 1000.0f, 520.0f, 1000.0f, 1060) == 0, "no change: no pinch step");
    check(r.one(320.0f, 1000.0f, 1070) == 0, "one finger left: still the pair's");
    check(r.lift(1080) & GEODE_GESTURE_OVER, "every finger off: over");
    check(r.end() == G(Gesture::PinchTwist) && r.out[0] == 0, "a pinch fires");
    check(geode_gesture_release(r.g) == 0, "nothing published left to lift");

    r.down(300.0f, 1000.0f, 2000);
    r.two(300.0f, 1000.0f, 500.0f, 1000.0f, 2010);
    r.two(300.0f, 1000.0f, 300.0f + 200.0f * std::cos(0.6f), 1000.0f + 200.0f * std::sin(0.6f), 2030);
    r.lift(2040);
    check(r.end() == G(Gesture::PinchTwist), "a twist past 0.5 rad is a pinch / twist");

    r.down(300.0f, 1000.0f, 3000);
    r.two(300.0f, 1000.0f, 500.0f, 1000.0f, 3010);
    check(r.two(400.0f, 1000.0f, 600.0f, 1000.0f, 3020) == GEODE_GESTURE_PUBLISH, "touch play while dragging");
    r.lift(3030);
    check(r.end() == -1, "a drag given touch play fires nothing at the lift");
    check(geode_gesture_release(r.g) == 1 && geode_gesture_release(r.g) == 0, "its fingers lift, once");

    r.actions[G(Gesture::TwoFingerDrag)] = A(Action::Macros);
    r.down(300.0f, 1000.0f, 4000);
    r.two(300.0f, 1000.0f, 500.0f, 1000.0f, 4010);
    check(r.two(400.0f, 1000.0f, 600.0f, 1000.0f, 4020) == 0, "a drag not given touch play hands nothing down");
    r.two(490.0f, 1000.0f, 690.0f, 1000.0f, 4030);
    r.lift(4040);
    check(r.end() == G(Gesture::TwoFingerDrag), "a drag past 72 dp fires");
    r.down(300.0f, 1000.0f, 5000);
    r.two(300.0f, 1000.0f, 500.0f, 1000.0f, 5010);
    r.two(470.0f, 1000.0f, 670.0f, 1000.0f, 5020);
    r.lift(5030);
    check(r.end() == -1, "a drag short of 72 dp fires nothing");

    geode_gesture_area(r.g, 1080.0f, 2400.0f, 40.0f, 0.0f, 40.0f, 0.0f, 0.0f, 0.0f, 1);
    r.down(300.0f, 1000.0f, 6000);
    r.two(300.0f, 1000.0f, 20.0f, 1000.0f, 6010);
    r.two(600.0f, 1000.0f, 320.0f, 1000.0f, 6020);
    r.lift(6030);
    check(r.end() == -1, "a pair with a finger in a strip is the system's");
}

// The owner's map and what Settings offers each gesture.
void gestureMap() {
    check(geode_gesture_default_action(G(Gesture::TapBottom)) == A(Action::ShowMiniPlayer)
              && geode_gesture_default_action(G(Gesture::TapTop)) == A(Action::ShowMiniPlayer)
              && geode_gesture_default_action(G(Gesture::DoubleTap)) == A(Action::ToggleSheet)
              && geode_gesture_default_action(G(Gesture::SwipeSideways)) == A(Action::StepStyle)
              && geode_gesture_default_action(G(Gesture::SwipeUp)) == A(Action::ShowMiniPlayer)
              && geode_gesture_default_action(G(Gesture::SwipeDown)) == A(Action::Macros)
              && geode_gesture_default_action(G(Gesture::LongPress)) == A(Action::Record)
              && geode_gesture_default_action(G(Gesture::Swirl)) == A(Action::RandomizeAll)
              && geode_gesture_default_action(G(Gesture::TwoFingerDrag)) == A(Action::TouchPlay)
              && geode_gesture_default_action(G(Gesture::PinchTwist)) == A(Action::Off),
          "the owner's gesture set");
    check(geode_gesture_default_action(-1) == -1 && geode_gesture_default_action(GEODE_GESTURE_COUNT) == -1, "no such gesture");
    int list[GEODE_GESTURE_MAX_CHOICES] = {};
    check(geode_gesture_choices(G(Gesture::SwipeSideways), A(Action::StepStyle), list, GEODE_GESTURE_MAX_CHOICES) == 4
              && list[0] == A(Action::StepStyle) && list[1] == A(Action::StepPreset) && list[2] == A(Action::StepPlaylist)
              && list[3] == A(Action::Off),
          "a sideways swipe steps through something");
    check(geode_gesture_choices(G(Gesture::PinchTwist), A(Action::Off), list, GEODE_GESTURE_MAX_CHOICES) == 2
              && list[0] == A(Action::TouchPlay) && list[1] == A(Action::Off),
          "a pinch moves over the visual");
    check(geode_gesture_choices(G(Gesture::TapTop), A(Action::QuickSettings), list, GEODE_GESTURE_MAX_CHOICES) == 8
              && list[0] == A(Action::QuickSettings) && list[1] == A(Action::ShowMiniPlayer) && list[2] == A(Action::ToggleSheet)
              && list[3] == A(Action::Macros) && list[4] == A(Action::Record) && list[5] == A(Action::RandomizeAll)
              && list[6] == A(Action::PlayPause) && list[7] == A(Action::Off),
          "a one-shot gesture: the current unsuited action stays, first");
    check(geode_gesture_choices(G(Gesture::TwoFingerDrag), A(Action::StepStyle), list, 1) == 3 && list[0] == A(Action::StepStyle),
          "a short buffer gets the head, the count is whole");
    check(geode_gesture_choices(GEODE_GESTURE_COUNT, 0, list, GEODE_GESTURE_MAX_CHOICES) == 0, "no such gesture, no choices");
}

// The remote's twins of the gestures (was StageKeysTest).
void keys() {
    namespace key = touch::key;
    int step = 9;
    const auto keyIs = [&](int code, bool shown, Gesture g, int s) {
        return geode_gesture_for_key(code, shown ? 1 : 0, &step) == G(g) && step == s;
    };
    check(keyIs(key::kDpadRight, false, Gesture::SwipeSideways, 1) && keyIs(key::kDpadLeft, false, Gesture::SwipeSideways, -1)
              && keyIs(key::kDpadUp, false, Gesture::SwipeUp, 0) && keyIs(key::kDpadDown, false, Gesture::SwipeDown, 0),
          "full screen the D-pad steps like a swipe, right being next");
    bool arrowsFree = true;
    for (int code : {key::kDpadLeft, key::kDpadRight, key::kDpadUp, key::kDpadDown}) {
        arrowsFree = arrowsFree && geode_gesture_for_key(code, 1, &step) == -1;
    }
    check(arrowsFree, "with the options up the arrows are left to focus");
    bool centre = true;
    for (int code : {key::kDpadCenter, key::kEnter, key::kNumpadEnter, key::kButtonA}) {
        centre = centre && keyIs(code, false, Gesture::DoubleTap, 0) && keyIs(code, true, Gesture::DoubleTap, 0);
    }
    check(centre, "the centre is the double tap either way");
    check(keyIs(key::kChannelUp, true, Gesture::SwipeSideways, 1) && keyIs(key::kChannelDown, true, Gesture::SwipeSideways, -1),
          "channel keys step even with the options up");
    // Android's Back, Home, play/pause, next and previous.
    bool others = true;
    for (int code : {4, 3, 85, 87, 88}) {
        others = others && geode_gesture_for_key(code, 0, &step) == -1 && geode_gesture_for_key(code, 1, &step) == -1;
    }
    check(others, "back and the media keys are no gesture");
    bool reached[GEODE_GESTURE_ACTION_COUNT] = {};
    for (int code = 0; code <= 1024; ++code) {
        const int g = geode_gesture_for_key(code, 0, &step);
        if (g >= 0) reached[geode_gesture_default_action(g)] = true;
    }
    bool everyAction = true;
    for (int g = 0; g < GEODE_GESTURE_COUNT; ++g) {
        const int a = geode_gesture_default_action(g);
        everyAction = everyAction && (a == A(Action::Off) || reached[a]);
    }
    check(everyAction, "every action the owner's map gives a gesture has a key");
}

}  // namespace

int main() {
    // Pixels into y-up NDC: the top-left corner is (-1, 1), the bottom-right (1, -1), the centre (0, 0).
    float xy[6] = {0.0f, 0.0f, 400.0f, 800.0f, 200.0f, 400.0f};
    touch::toNdc(xy, 3, 400.0f, 800.0f);
    check(near(xy[0], -1.0f) && near(xy[1], 1.0f), "top-left is (-1, 1)");
    check(near(xy[2], 1.0f) && near(xy[3], -1.0f), "bottom-right is (1, -1)");
    check(near(xy[4], 0.0f) && near(xy[5], 0.0f), "the centre is (0, 0)");
    float tiny[2] = {0.5f, 0.5f};
    touch::toNdc(tiny, 1, 0.0f, 0.0f);
    check(near(tiny[0], 0.0f) && near(tiny[1], 0.0f), "an unlaid view reads as 1 px");
    float left[4] = {10.0f, 10.0f, 99.0f, 99.0f};
    touch::toNdc(left, 1, 20.0f, 20.0f);
    check(left[2] == 99.0f && left[3] == 99.0f, "only the live points are rewritten");

    // Two fingers: a pan moves the midpoint, a spread is the zoom, a turn the twist.
    const float start[4] = {100.0f, 100.0f, 200.0f, 100.0f};
    const float panned[4] = {130.0f, 60.0f, 230.0f, 60.0f};
    auto r = touch::readPair(start, start, panned);
    check(near(r.panX, 30.0f) && near(r.panY, -40.0f), "a drag pans");
    check(near(r.zoom, 1.0f) && near(r.twist, 0.0f), "a drag neither zooms nor twists");
    check(near(r.stepFactor, 1.0f) && near(r.stepDegrees, 0.0f), "a drag has no pinch step");

    const float spread[4] = {50.0f, 100.0f, 250.0f, 100.0f};
    r = touch::readPair(start, start, spread);
    check(near(r.zoom, 2.0f) && near(r.stepFactor, 2.0f), "spreading to twice is zoom 2");
    check(near(r.panX, 0.0f) && near(r.panY, 0.0f), "a centred spread does not pan");

    const float quarter[4] = {150.0f, 50.0f, 150.0f, 150.0f};
    r = touch::readPair(start, start, quarter);
    check(near(r.twist, touch::kPi / 2.0f) && near(r.stepDegrees, 90.0f), "a quarter turn is 90 degrees");
    check(near(r.zoom, 1.0f), "a turn alone does not zoom");

    // The step is from the last event, not the start: a pinch recognised late does not jump.
    r = touch::readPair(start, spread, spread);
    check(near(r.zoom, 2.0f) && near(r.stepFactor, 1.0f) && near(r.stepDegrees, 0.0f), "no change since last, no step");
    const float wider[4] = {0.0f, 100.0f, 300.0f, 100.0f};
    r = touch::readPair(start, spread, wider);
    check(near(r.zoom, 3.0f) && near(r.stepFactor, 1.5f), "the step is the spread since the last event");

    // A twist across the half turn wraps: from +170 to -170 degrees is 20 degrees on, not 340 back.
    const float d = touch::kPi / 180.0f;
    const float at170[4] = {0.0f, 0.0f, 100.0f * std::cos(170.0f * d), 100.0f * std::sin(170.0f * d)};
    const float atMinus170[4] = {0.0f, 0.0f, 100.0f * std::cos(-170.0f * d), 100.0f * std::sin(-170.0f * d)};
    const float flat[4] = {0.0f, 0.0f, 100.0f, 0.0f};
    r = touch::readPair(flat, at170, atMinus170);
    check(near(r.stepDegrees, 20.0f, 1e-2f), "a twist wraps across the half turn");
    check(near(r.twist, -170.0f * d, 1e-4f), "the twist since start is wrapped");

    // Fingers that met last event give no factor; fingers that came down together read as 1 px apart.
    const float met[4] = {100.0f, 100.0f, 100.0f, 100.0f};
    r = touch::readPair(start, met, spread);
    check(r.stepFactor == 1.0f, "no factor from a zero spread");
    r = touch::readPair(met, met, spread);
    check(near(r.zoom, 200.0f) && std::isfinite(r.stepFactor), "a zero start spread reads as 1 px");
    check(touch::readPair(nullptr, start, start).zoom == 1.0f, "a missing pair reads as nothing");

    // The stir: two opposite points circling the centre at the radius, a step a frame, then over.
    float p[4] = {9.0f, 9.0f, 9.0f, 9.0f};
    check(touch::pulse(0, p) == touch::kPulseFrameMs, "the stir's first step holds a frame");
    check(near(p[0], touch::kPulseRadius) && near(p[1], 0.0f) && near(p[2], -touch::kPulseRadius) && near(p[3], 0.0f),
          "the stir starts on the x axis, the points opposite");
    check(touch::pulse(touch::kPulseSteps / 4, p) > 0, "a quarter of the way");
    check(near(p[0], 0.0f) && near(p[1], touch::kPulseRadius) && near(p[3], -touch::kPulseRadius), "a quarter turn round");
    int steps = 0;
    for (int i = 0; touch::pulse(i, p) > 0; ++i) ++steps;
    check(steps == 36, "the stir is 36 steps, one turn");
    float untouched[4] = {7.0f, 7.0f, 7.0f, 7.0f};
    check(touch::pulse(touch::kPulseSteps, untouched) == 0 && untouched[0] == 7.0f, "past the end: over, xy untouched");
    check(touch::pulse(-1, untouched) == 0, "a negative step is over");
    check(touch::pulse(0, nullptr) == 0, "no buffer, no stir");
    check(near(touch::kPulseRadius, 0.35f) && touch::kPulseFrameMs == 33, "the stir is the shell's old one");

    strips();
    taps();
    hold();
    lateHold();
    swipes();
    swirls();
    pairs();
    gestureMap();
    keys();

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("touch_gesture_test: OK\n");
    return 0;
}
