#include <new>

#include "api/geode_api.h"
#include "viz/input/GestureMap.hpp"
#include "viz/input/GestureRead.hpp"

namespace touch = geode::viz::touch;

// The header's numbers are the reader's; a drift would make the shell read one answer and mean another.
static_assert(touch::kGestureCount == GEODE_GESTURE_COUNT && touch::kActionCount == GEODE_GESTURE_ACTION_COUNT);
static_assert(touch::kMaxChoices == GEODE_GESTURE_MAX_CHOICES);
static_assert(touch::kHoldFillMs == GEODE_GESTURE_HOLD_FILL_MS);
static_assert(touch::kRing == GEODE_GESTURE_RING && touch::kHold == GEODE_GESTURE_HOLD && touch::kSwirl == GEODE_GESTURE_SWIRL);
static_assert(touch::kOver == GEODE_GESTURE_OVER && touch::kPublish == GEODE_GESTURE_PUBLISH);
static_assert(touch::kRelease == GEODE_GESTURE_RELEASE && touch::kPinch == GEODE_GESTURE_PINCH);
static_assert(touch::kBounce == GEODE_GESTURE_SECOND_BOUNCE && touch::kFollow == GEODE_GESTURE_SECOND_FOLLOW);
static_assert(touch::kDouble == GEODE_GESTURE_SECOND_DOUBLE && touch::kNewTouch == GEODE_GESTURE_SECOND_NEW_TOUCH);

struct geode_gesture {
    touch::GestureRead read;
};

extern "C" {

int geode_gesture_default_action(int gesture) {
    if (!touch::validGesture(gesture)) return -1;
    return static_cast<int>(touch::defaultAction(static_cast<touch::Gesture>(gesture)));
}

int geode_gesture_choices(int gesture, int current, int* out, int capacity) {
    if (!touch::validGesture(gesture)) return 0;
    const auto now = touch::validAction(current) ? static_cast<touch::Action>(current) : touch::Action::Off;
    touch::Action list[touch::kMaxChoices];
    const int n = touch::choices(static_cast<touch::Gesture>(gesture), now, list, touch::kMaxChoices);
    if (out) {
        for (int i = 0; i < n && i < capacity; ++i) out[i] = static_cast<int>(list[i]);
    }
    return n;
}

int geode_gesture_for_key(int key_code, int options_shown, int* step_out) {
    const touch::KeyGesture k = touch::keyGesture(key_code, options_shown != 0);
    if (step_out) *step_out = k.step;
    return k.gesture;
}

geode_gesture* geode_gesture_create(void) { return new (std::nothrow) geode_gesture(); }

void geode_gesture_destroy(geode_gesture* g) { delete g; }

void geode_gesture_configure(geode_gesture* g, float touch_slop, float double_tap_slop, float density, long long long_press_ms,
                             long long double_tap_min_ms, long long double_tap_timeout_ms) {
    if (!g) return;
    g->read.configure({touch_slop, double_tap_slop, density, static_cast<int64_t>(long_press_ms),
                       static_cast<int64_t>(double_tap_min_ms), static_cast<int64_t>(double_tap_timeout_ms)});
}

void geode_gesture_area(geode_gesture* g, float window_w, float window_h, float left, float top, float right, float bottom,
                        float origin_x, float origin_y, int placed) {
    if (!g) return;
    g->read.area({window_w, window_h, left, top, right, bottom, origin_x, origin_y, placed != 0});
}

void geode_gesture_down(geode_gesture* g, float x, float y, long long t_ms, const int* actions, int armed) {
    if (g) g->read.down(x, y, t_ms, actions, armed != 0);
}

long long geode_gesture_wait_ms(const geode_gesture* g) { return g ? g->read.waitMs() : -1; }

unsigned geode_gesture_tick(geode_gesture* g) { return g ? g->read.tick() : 0u; }

unsigned geode_gesture_event(geode_gesture* g, long long t_ms, const float* xy, int live, int primary_down, float* pinch) {
    if (!g) return GEODE_GESTURE_OVER;
    return g->read.event(t_ms, xy, live, primary_down != 0, pinch);
}

int geode_gesture_release(geode_gesture* g) { return g && g->read.release() ? 1 : 0; }

int geode_gesture_end(geode_gesture* g, float split_y, int* out) {
    if (!g) return -1;
    const touch::Outcome o = g->read.end(split_y);
    if (out) {
        out[0] = o.step;
        out[1] = o.tap ? 1 : 0;
        out[2] = static_cast<int>(o.doubleWaitMs);
    }
    return o.gesture;
}

int geode_gesture_second_down(geode_gesture* g, float x, float y, long long t_ms) {
    return g ? static_cast<int>(g->read.secondDown(x, y, t_ms)) : GEODE_GESTURE_SECOND_NEW_TOUCH;
}

int geode_gesture_second_move(geode_gesture* g, int present, float x, float y, int pressed) {
    return g ? static_cast<int>(g->read.secondMove(present != 0, x, y, pressed != 0)) : GEODE_GESTURE_SECOND_NEW_TOUCH;
}

}  // extern "C"
