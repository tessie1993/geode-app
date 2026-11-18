#include "shell/Behaviour.hpp"

namespace geode::shell {

std::vector<NotificationButton> notificationButtonsOrdered(unsigned chosen) {
    // Favourite first: it is the one asked for beyond the defaults, so it is never the one dropped.
    static constexpr NotificationButton kPriority[] = {NotificationButton::Favourite, NotificationButton::Shuffle,
                                                       NotificationButton::Repeat};
    std::vector<NotificationButton> out;
    for (NotificationButton b : kPriority)
        if (chosen & notificationBit(b)) out.push_back(b);
    return out;
}

unsigned notificationButtonsToggled(unsigned chosen, NotificationButton button, bool on) {
    if (!on) return chosen & ~notificationBit(button);
    unsigned out = notificationBit(button);
    int taken = 1;
    for (NotificationButton b : notificationButtonsOrdered(chosen & ~notificationBit(button))) {
        if (taken >= kNotificationRoom) break;
        out |= notificationBit(b);
        ++taken;
    }
    return out;
}

StartScreen behaviourLastScreen(StartScreen last) { return last == StartScreen::LastUsed ? StartScreen::Player : last; }

StartScreen behaviourOpensOn(StartScreen start, StartScreen last) { return start == StartScreen::LastUsed ? last : start; }

}  // namespace geode::shell
