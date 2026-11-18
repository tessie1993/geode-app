#pragma once

#include <vector>

// The rules in Kotlin data/BehaviourPrefs.kt: which extra buttons the media notification carries and in what
// order, how switching one on makes room, and that "last used" is a choice, never a screen shown. The prefs
// file and the enum names it keeps stay in the shell. Host-tested in core/tests/shell_prefs_test.cpp.
namespace geode::shell {

// The extra buttons beside play, previous and next (NotificationButton). A set of them is a bit mask of
// notificationBit(button).
enum class NotificationButton : int { Shuffle, Repeat, Favourite };

constexpr unsigned notificationBit(NotificationButton b) { return 1u << static_cast<int>(b); }

// Android's media controls draw two buttons beside previous, play and next; a third is dropped.
inline constexpr int kNotificationRoom = 2;

// The order the session offers the chosen buttons in (controllers fill their slots from the front): favourite
// first, then shuffle, then repeat. (NotificationButton.ordered)
std::vector<NotificationButton> notificationButtonsOrdered(unsigned chosen);

// The chosen set with the button switched. Switching one on past kNotificationRoom keeps it and the buttons
// first in notificationButtonsOrdered's order, and switches the last other one off. (NotificationButton.toggled)
unsigned notificationButtonsToggled(unsigned chosen, NotificationButton button, bool on);

// The screens the app can open on (StartScreen). LastUsed reopens the last screen shown.
enum class StartScreen : int { LastUsed, Player, Library, Visuals, Studio };

// The last screen to keep: LastUsed is a choice, never a screen shown, so as a last screen it reads as the
// default (Player). (BehaviourCodec.normalize)
StartScreen behaviourLastScreen(StartScreen last);

// The screen to open on now: the chosen one, or the last screen shown for LastUsed. (BehaviourPrefs.opensOn)
StartScreen behaviourOpensOn(StartScreen start, StartScreen last);

}  // namespace geode::shell
