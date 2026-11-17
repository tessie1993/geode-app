#pragma once
#include <array>
#include <cstddef>

namespace geode::viz::touch {

// The gestures on the visual and what each may do (Settings › Gestures & controls). The numbers are the ABI: the
// shell's CanvasGesture and GestureAction ordinals. Stored choices are kept by name in the shell, never by number.
enum class Gesture : int {
    TapBottom = 0,
    TapTop,
    DoubleTap,
    SwipeSideways,
    SwipeUp,
    SwipeDown,
    LongPress,
    Swirl,
    TwoFingerDrag,
    PinchTwist,
};
inline constexpr int kGestureCount = 10;

enum class Action : int {
    Off = 0,
    ShowMiniPlayer,
    QuickSettings,
    ToggleSheet,
    StepStyle,
    StepPreset,
    StepPlaylist,
    Macros,
    Record,
    RandomizeAll,
    TouchPlay,
    PlayPause,
};
inline constexpr int kActionCount = 12;

inline bool validGesture(int g) { return g >= 0 && g < kGestureCount; }
inline bool validAction(int a) { return a >= 0 && a < kActionCount; }

// The owner's gesture set (ui-flow decisions › Gestures), by gesture; pinch and twist do nothing.
inline constexpr std::array<Action, kGestureCount> kDefaultActions = {
    Action::ShowMiniPlayer,  // tap bottom
    Action::ShowMiniPlayer,  // tap top
    Action::ToggleSheet,     // double tap
    Action::StepStyle,       // swipe sideways
    Action::ShowMiniPlayer,  // swipe up
    Action::Macros,          // swipe down
    Action::Record,          // long press
    Action::RandomizeAll,    // swirl
    Action::TouchPlay,       // two-finger drag
    Action::Off,             // pinch and twist
};

inline Action defaultAction(Gesture g) { return kDefaultActions[static_cast<size_t>(g)]; }

// The actions a gesture is offered, in the settings list's order: a sideways swipe steps through something, a
// two-finger drag and a pinch move over the visual (touch play), every other gesture opens or does one thing. Off
// is always last, and the current action stays on the list, first, when it is not suited, so a choice made
// elsewhere is never hidden. Writes up to capacity; returns how many there are (at most kMaxChoices).
inline constexpr int kMaxChoices = 8;

inline int choices(Gesture g, Action current, Action* out, int capacity) {
    static constexpr Action kSteps[] = {Action::StepStyle, Action::StepPreset, Action::StepPlaylist};
    static constexpr Action kMove[] = {Action::TouchPlay};
    static constexpr Action kOneShot[] = {Action::ShowMiniPlayer, Action::ToggleSheet, Action::Macros,
                                          Action::Record, Action::RandomizeAll, Action::PlayPause};
    const Action* suited = kOneShot;
    int suitedCount = static_cast<int>(std::size(kOneShot));
    if (g == Gesture::SwipeSideways) {
        suited = kSteps;
        suitedCount = static_cast<int>(std::size(kSteps));
    } else if (g == Gesture::TwoFingerDrag || g == Gesture::PinchTwist) {
        suited = kMove;
        suitedCount = static_cast<int>(std::size(kMove));
    }
    bool listed = current == Action::Off;
    for (int i = 0; i < suitedCount; ++i) listed = listed || suited[i] == current;
    int n = 0;
    const auto put = [&](Action a) {
        if (out && n < capacity) out[n] = a;
        ++n;
    };
    if (!listed) put(current);
    for (int i = 0; i < suitedCount; ++i) put(suited[i]);
    put(Action::Off);
    return n;
}

// A TV remote's, keyboard's or game pad's twin of each gesture, by Android key code (android.view.KeyEvent), so
// every mapped action is one key away without a touchscreen. With the options hidden (full screen) the D-pad is
// the visual's: left and right the sideways swipe (right is next), up and down the vertical swipes. With the
// options up the arrows are left to focus. The centre is the double tap either way, channel up and down step like
// a swipe, the colour keys, Menu, Info and Record carry the gestures with no direction. Back and the media keys
// are no gesture (the playback session takes those).
namespace key {
inline constexpr int kDpadUp = 19;
inline constexpr int kDpadDown = 20;
inline constexpr int kDpadLeft = 21;
inline constexpr int kDpadRight = 22;
inline constexpr int kDpadCenter = 23;
inline constexpr int kEnter = 66;
inline constexpr int kMenu = 82;
inline constexpr int kButtonA = 96;
inline constexpr int kButtonX = 99;
inline constexpr int kButtonY = 100;
inline constexpr int kMediaRecord = 130;
inline constexpr int kNumpadEnter = 160;
inline constexpr int kInfo = 165;
inline constexpr int kChannelUp = 166;
inline constexpr int kChannelDown = 167;
inline constexpr int kProgRed = 183;
inline constexpr int kProgGreen = 184;
inline constexpr int kProgYellow = 185;
inline constexpr int kProgBlue = 186;
}  // namespace key

struct KeyGesture {
    int gesture = -1;  // a Gesture, or -1: no gesture, the key is left to whoever else wants it
    int step = 0;      // a sideways step's direction, +1 next, -1 previous, as the finger's swipe reports it
};

inline KeyGesture keyGesture(int keyCode, bool optionsShown) {
    const auto is = [](Gesture g, int step = 0) { return KeyGesture{static_cast<int>(g), step}; };
    switch (keyCode) {
        case key::kDpadCenter:
        case key::kEnter:
        case key::kNumpadEnter:
        case key::kButtonA: return is(Gesture::DoubleTap);
        case key::kChannelUp: return is(Gesture::SwipeSideways, 1);
        case key::kChannelDown: return is(Gesture::SwipeSideways, -1);
        case key::kMediaRecord:
        case key::kProgRed: return is(Gesture::LongPress);
        case key::kProgGreen:
        case key::kButtonY: return is(Gesture::Swirl);
        case key::kProgYellow:
        case key::kButtonX: return is(Gesture::TwoFingerDrag);
        case key::kProgBlue:
        case key::kMenu: return is(Gesture::TapTop);
        case key::kInfo: return is(Gesture::TapBottom);
        default: break;
    }
    if (optionsShown) return {};
    switch (keyCode) {
        case key::kDpadLeft: return is(Gesture::SwipeSideways, -1);
        case key::kDpadRight: return is(Gesture::SwipeSideways, 1);
        case key::kDpadUp: return is(Gesture::SwipeUp);
        case key::kDpadDown: return is(Gesture::SwipeDown);
        default: return {};
    }
}

}  // namespace geode::viz::touch
