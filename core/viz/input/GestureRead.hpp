#pragma once
#include <cmath>
#include <cstdint>

#include "viz/input/GestureMap.hpp"
#include "viz/input/TouchGesture.hpp"

namespace geode::viz::touch {

// What a gesture on the full-screen visual is, read one pointer event at a time (was VisualGestures.kt). The
// shell keeps the pointer loop, the platform's slop and timeouts, the insets, haptics and the ring's drawing; it
// feeds each down, event and timeout in, and this answers what happened. One reader per stage, reused for every
// gesture; nothing allocates.
//
// One finger: a tap (top or bottom, split where the shell says), a double tap, a swipe sideways, up or down past
// kSwipeDp, a swirl (two circles, counted as the heading's total turn) and a long press held through the ring.
// Two fingers: a drag while they move together past the touch slop, a pinch / twist from the moment their spread
// or angle changes past its threshold (and a pinch stays a pinch). A swipe, a swirl or a pair counts only when it
// starts outside the system's gesture strips; a pair only when both its downs do.
//
// The ring shows at the platform's long-press timeout and fills over kHoldFillMs; the hold fires when it closes.
// A lift before the ring is a tap, a lift once it shows does nothing. A swirl attempt (the heading turned past half
// a circle) never falls through to a swipe. The double tap is waited for only while it has an action: the second
// down counts between the double-tap min time and timeout after the first lift, within the platform's double-tap
// slop of the first down, and only while it stays within the touch slop until it lifts.

inline constexpr float kSwipeDp = 72.0f;          // how far a swipe, or a two-finger drag, travels to count
inline constexpr int64_t kHoldFillMs = 800;       // the ring's fill; the hold fires when it closes
inline constexpr float kSwirlTurnRad = 3.6f * kPi;  // two circles, a little short: a drawn circle rarely closes
inline constexpr float kSwirlAttemptRad = kPi;    // past half a circle the finger is drawing one, not swiping
inline constexpr float kSwirlStepDp = 6.0f;       // headings over steps this long, so jitter never adds up
inline constexpr float kPinchZoom = 0.2f;
inline constexpr float kPinchTwistRad = 0.5f;

// The platform's values, as the shell's ViewConfiguration gives them.
struct Platform {
    float touchSlop = 0.0f;      // px
    float doubleTapSlop = 0.0f;  // px, the platform's scaled double-tap slop
    float density = 1.0f;        // px per dp
    int64_t longPressMs = 0;
    int64_t doubleTapMinMs = 0;
    int64_t doubleTapTimeoutMs = 0;
};

// Where the stage sits: the window (from 0, 0) in px, the system's gesture strips (distances from the window's
// edges), and the stage's top-left in the window, once its first placement is known.
struct Area {
    float windowW = 0.0f, windowH = 0.0f;
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    float originX = 0.0f, originY = 0.0f;
    bool placed = false;
};

struct SafeRect {
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    // Left and top inclusive, right and bottom exclusive.
    bool contains(float x, float y) const { return x >= left && x < right && y >= top && y < bottom; }
};

// The part of the window outside the strips.
inline SafeRect safeArea(const Area& a) { return {a.left, a.top, a.windowW - a.right, a.windowH - a.bottom}; }

// Whether a down at the stage-local (x, y) is the app's: anywhere until the stage is placed (an unplaced origin
// would put every down in the top-left strips), then only outside the strips. A tap is the app's either way.
inline bool ours(const Area& a, float x, float y) { return !a.placed || safeArea(a).contains(a.originX + x, a.originY + y); }

inline float distance(float dx, float dy) { return std::sqrt(dx * dx + dy * dy); }

// A second down this far from the first tap's down is the second tap of a double tap; further, a new touch.
inline bool isSecondTap(float dx, float dy, float doubleTapSlop) { return distance(dx, dy) <= doubleTapSlop; }

// A heading that turned this far, either way, is a swirl attempt.
inline bool swirlAttempted(float turnRad) { return std::fabs(turnRad) > kSwirlAttemptRad; }

// The heading's step into [-pi, pi], held against pi in double as the shell's was.
inline float wrapTurn(float a) {
    constexpr double pi = 3.141592653589793;
    const float twoPi = static_cast<float>(2.0 * pi);
    if (!std::isfinite(a)) return a;
    while (static_cast<double>(a) > pi) a -= twoPi;
    while (static_cast<double>(a) < -pi) a += twoPi;
    return a;
}

// What one call answers, as bits.
inline constexpr uint32_t kRing = 1u << 0;     // the ring shows now, at the gesture's start
inline constexpr uint32_t kHold = 1u << 1;     // the hold fired
inline constexpr uint32_t kSwirl = 1u << 2;    // the swirl fired
inline constexpr uint32_t kOver = 1u << 3;     // the gesture's fingers are gone: stop following and ask end()
inline constexpr uint32_t kPublish = 1u << 4;  // hand the fingers down now to touch play
inline constexpr uint32_t kRelease = 1u << 5;  // touch play's fingers lift
inline constexpr uint32_t kPinch = 1u << 6;    // a pinch step: its scale factor and degrees

// The wait for a second tap: a down too soon (a bounce, wait on), one to follow, a double tap, or a touch that is
// no second tap and is read again from its own down as a new gesture.
enum Second : int { kBounce = 0, kFollow = 1, kDouble = 2, kNewTouch = 3 };

// What the gesture was, once its fingers are gone.
struct Outcome {
    int gesture = -1;      // the gesture to fire, -1 none
    int step = 0;          // a sideways swipe's step, +1 next (leftwards, like turning a page), -1 previous
    bool tap = false;      // a tap: gesture is the top or bottom tap, the double tap maybe still to come
    int64_t doubleWaitMs = 0;  // how long to wait for a second tap's down, 0 not at all
};

class GestureRead {
public:
    void configure(const Platform& p) { platform_ = p; }
    void area(const Area& a) { area_ = a; }

    // A first finger came down at the stage-local (x, y) at tMs. actions holds each gesture's action now
    // (kGestureCount of them); armed is whether the hold may run at all.
    void down(float x, float y, int64_t tMs, const int* actions, bool armed) {
        const Platform platform = platform_;
        const Area area = area_;
        *this = GestureRead();
        platform_ = platform;
        area_ = area;
        for (int g = 0; g < kGestureCount; ++g) {
            const int a = actions ? actions[g] : 0;
            actions_[g] = validAction(a) ? static_cast<Action>(a) : Action::Off;
        }
        startX_ = lastX_ = segX_ = x;
        startY_ = lastY_ = segY_ = y;
        startMs_ = now_ = tMs;
        startOurs_ = ours(area_, x, y);
        holds_ = armed && action(Gesture::LongPress) != Action::Off;
        swirls_ = startOurs_ && action(Gesture::Swirl) != Action::Off;
    }

    // How long to wait for the next event with the hold timer running; -1 when no timer runs.
    int64_t waitMs() const {
        if (!waitingOnHold()) return -1;
        const int64_t left = holdDeadline() - now_;
        return left < 1 ? 1 : left;
    }

    // The hold timer ran out: the ring shows, or, once shown, closes and the hold fires.
    uint32_t tick() {
        if (!waitingOnHold()) return ring();
        now_ = holdDeadline();
        if (!ringShown_) {
            ringShown_ = true;
            return ring();
        }
        done_ = true;
        return kHold;
    }

    // A pointer event: its time (< 0 keeps the last), the pressed fingers' (x, y) px in the event's order (live of
    // them; only the first two are read), and whether the finger that started the gesture is still down. A pinch
    // step goes to pinch[2] with kPinch.
    uint32_t event(int64_t tMs, const float* xy, int live, bool primaryDown, float* pinch) {
        if (tMs >= 0) now_ = tMs;
        if (!xy) live = 0;
        if (!multi_ && live >= 2) {
            multi_ = true;
            pairInside_ = startOurs_ && ours(area_, xy[0], xy[1]) && ours(area_, xy[2], xy[3]);
            for (int i = 0; i < 4; ++i) pairStart_[i] = pairLast_[i] = xy[i];
        }
        if (!multi_) {
            if (!primaryDown || live < 1) return kOver;
            const bool swirled = moveTo(xy[0], xy[1]);
            return ring() | (swirled ? kSwirl : 0u);
        }
        if (live < 2) {
            if (live == 0) return kOver;
        } else if (pairInside_) {
            trackPair(xy);
        }
        uint32_t answer = 0;
        const float factor = stepFactor_;
        const float degrees = stepDegrees_;
        stepFactor_ = 1.0f;
        stepDegrees_ = 0.0f;
        if (kind_ == static_cast<int>(Gesture::PinchTwist) && (factor != 1.0f || degrees != 0.0f)) {
            answer |= kPinch;
            if (pinch) {
                pinch[0] = factor;
                pinch[1] = degrees;
            }
        }
        if (kind_ >= 0 && action(static_cast<Gesture>(kind_)) == Action::TouchPlay) {
            published_ = true;
            answer |= kPublish;
        } else if (published_) {
            published_ = false;
            answer |= kRelease;
        }
        return answer;
    }

    // Whether touch play holds fingers that must now be lifted (the gesture ended or was cancelled); clears it.
    bool release() {
        const bool was = published_;
        published_ = false;
        return was;
    }

    // What the gesture was, once follow ends. splitY: where a tap turns from top to bottom (stage px).
    Outcome end(float splitY) {
        Outcome o;
        const float far = kSwipeDp * platform_.density;
        if (multi_) {
            const int g = pairGesture(far);
            if (g >= 0 && action(static_cast<Gesture>(g)) != Action::TouchPlay) o.gesture = g;
            return o;
        }
        if (done_) return o;
        if (moved_) {
            if (startOurs_ && !swirling_) swipe(far, o);
            return o;
        }
        // Held to the ring, or past its time with the hold off: that press was not a tap.
        if (ringShown_ || now_ - startMs_ > platform_.longPressMs) return o;
        o.tap = true;
        o.gesture = static_cast<int>(startY_ < splitY ? Gesture::TapTop : Gesture::TapBottom);
        if (action(Gesture::DoubleTap) != Action::Off) o.doubleWaitMs = platform_.doubleTapTimeoutMs;
        firstUpMs_ = now_;
        return o;
    }

    // While waiting for a second tap: a down at (x, y) at tMs.
    Second secondDown(float x, float y, int64_t tMs) {
        if (tMs < firstUpMs_ + platform_.doubleTapMinMs) return kBounce;
        secondX_ = x;
        secondY_ = y;
        return isSecondTap(x - startX_, y - startY_, platform_.doubleTapSlop) ? kFollow : kNewTouch;
    }

    // While following the second down: its change in the event, if there is one.
    Second secondMove(bool present, float x, float y, bool pressed) const {
        if (!present) return kDouble;
        if (distance(x - secondX_, y - secondY_) > platform_.touchSlop) return kNewTouch;
        return pressed ? kFollow : kDouble;
    }

    // The two-finger kind so far (a Gesture, -1 none yet), and the one-finger turn; for tests.
    int pairKind() const { return kind_; }
    float turn() const { return turn_; }

private:
    Action action(Gesture g) const { return actions_[static_cast<int>(g)]; }
    bool waitingOnHold() const { return holds_ && !moved_ && !multi_ && !done_; }
    int64_t holdDeadline() const { return startMs_ + (ringShown_ ? platform_.longPressMs + kHoldFillMs : platform_.longPressMs); }
    uint32_t ring() const { return ringShown_ && waitingOnHold() ? kRing : 0u; }

    bool moveTo(float x, float y) {
        if (!moved_ && distance(x - startX_, y - startY_) > platform_.touchSlop) moved_ = true;
        lastX_ = x;
        lastY_ = y;
        if (!moved_ || !swirls_ || done_) return false;
        const float turned = turnBy(x, y);
        if (swirlAttempted(turned)) swirling_ = true;
        if (turned < kSwirlTurnRad) return false;
        done_ = true;
        return true;
    }

    float turnBy(float x, float y) {
        const float dx = x - segX_;
        const float dy = y - segY_;
        if (distance(dx, dy) < kSwirlStepDp * platform_.density) return std::fabs(turn_);
        const auto h = static_cast<float>(std::atan2(static_cast<double>(dy), static_cast<double>(dx)));
        if (hasHeading_) turn_ += wrapTurn(h - heading_);
        heading_ = h;
        hasHeading_ = true;
        segX_ = x;
        segY_ = y;
        return std::fabs(turn_);
    }

    void trackPair(const float* now) {
        const PairReading r = readPair(pairStart_, pairLast_, now);
        for (int i = 0; i < 4; ++i) pairLast_[i] = now[i];
        panX_ = r.panX;
        panY_ = r.panY;
        stepFactor_ = r.stepFactor;
        stepDegrees_ = r.stepDegrees;
        if (kind_ == static_cast<int>(Gesture::PinchTwist)) return;
        if (std::fabs(r.zoom - 1.0f) > kPinchZoom || std::fabs(r.twist) > kPinchTwistRad) {
            kind_ = static_cast<int>(Gesture::PinchTwist);
        } else if (distance(panX_, panY_) > platform_.touchSlop) {
            kind_ = static_cast<int>(Gesture::TwoFingerDrag);
        }
    }

    int pairGesture(float far) const {
        if (kind_ == static_cast<int>(Gesture::PinchTwist)) return kind_;
        if (distance(panX_, panY_) > far) return static_cast<int>(Gesture::TwoFingerDrag);
        return -1;
    }

    void swipe(float far, Outcome& o) const {
        const float dx = lastX_ - startX_;
        const float dy = lastY_ - startY_;
        if (std::fabs(dx) >= std::fabs(dy)) {
            if (std::fabs(dx) > far) {
                o.gesture = static_cast<int>(Gesture::SwipeSideways);
                o.step = dx < 0.0f ? 1 : -1;
            }
        } else if (dy < -far) {
            o.gesture = static_cast<int>(Gesture::SwipeUp);
        } else if (dy > far) {
            o.gesture = static_cast<int>(Gesture::SwipeDown);
        }
    }

    Platform platform_;
    Area area_;
    Action actions_[kGestureCount] = {};
    bool holds_ = false, swirls_ = false, startOurs_ = false;
    bool moved_ = false, multi_ = false, swirling_ = false, done_ = false, ringShown_ = false;
    bool pairInside_ = false, published_ = false, hasHeading_ = false;
    float startX_ = 0.0f, startY_ = 0.0f, lastX_ = 0.0f, lastY_ = 0.0f, segX_ = 0.0f, segY_ = 0.0f;
    float heading_ = 0.0f, turn_ = 0.0f;
    int64_t startMs_ = 0, now_ = 0, firstUpMs_ = 0;
    float pairStart_[4] = {}, pairLast_[4] = {};
    float panX_ = 0.0f, panY_ = 0.0f, stepFactor_ = 1.0f, stepDegrees_ = 0.0f;
    int kind_ = -1;
    float secondX_ = 0.0f, secondY_ = 0.0f;
};

}  // namespace geode::viz::touch
