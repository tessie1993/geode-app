// The C ABI of core/shell (api/geode_shell_api.h): forwarders only.
#include "api/geode_shell_api.h"

#include <algorithm>

#include "shell/Behaviour.hpp"
#include "shell/ExportFps.hpp"

namespace shell = geode::shell;

extern "C" {

int geode_export_fps_options(int* out, int capacity) {
    const auto options = shell::exportFpsOptions();
    const int count = static_cast<int>(options.size());
    if (out) std::copy_n(options.begin(), std::clamp(capacity, 0, count), out);
    return count;
}

int geode_export_fps_read(int stored, int fallback) { return shell::exportFpsRead(stored, fallback); }

int geode_notification_buttons_ordered(unsigned chosen, int* out, int capacity) {
    const auto ordered = shell::notificationButtonsOrdered(chosen);
    const int count = static_cast<int>(ordered.size());
    for (int i = 0; out && i < std::min(capacity, count); ++i) out[i] = static_cast<int>(ordered[static_cast<size_t>(i)]);
    return count;
}

unsigned geode_notification_buttons_toggled(unsigned chosen, int button, int on) {
    return shell::notificationButtonsToggled(chosen, static_cast<shell::NotificationButton>(button), on != 0);
}

int geode_behaviour_last_screen(int last) {
    return static_cast<int>(shell::behaviourLastScreen(static_cast<shell::StartScreen>(last)));
}

int geode_behaviour_opens_on(int start, int last) {
    return static_cast<int>(shell::behaviourOpensOn(static_cast<shell::StartScreen>(start), static_cast<shell::StartScreen>(last)));
}

}  // extern "C"
