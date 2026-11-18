// The C ABI of core/shell (api/geode_shell_api.h): forwarders only.
#include "api/geode_shell_api.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "api/geode_text_out.hpp"
#include "shell/Behaviour.hpp"
#include "shell/ExportFps.hpp"
#include "shell/StyleShelf.hpp"

namespace shell = geode::shell;

namespace {

std::vector<std::string> idsOf(const char* const* ids, int count) {
    std::vector<std::string> out;
    for (int i = 0; ids && i < count; ++i) out.emplace_back(ids[i] ? ids[i] : "");
    return out;
}

// A list as text-out: each id followed by a newline.
int linesOut(const std::vector<std::string>& ids, char* out, int capacity) {
    std::string text;
    for (const std::string& id : ids) {
        text += id;
        text += '\n';
    }
    return geode::api::textOut(text, out, capacity);
}

shell::StyleShelf shelfApply(std::vector<std::string> favourites, std::vector<std::string> recents,
                             shell::StyleShelfEvent::Kind kind, const char* id) {
    return shell::styleShelfApply({std::move(favourites), std::move(recents), false}, {kind, id ? id : "", false});
}

}  // namespace

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

int geode_style_shelf_toggle_favourite(const char* const* favourites, int count, const char* id, char* out, int capacity) {
    return linesOut(shelfApply(idsOf(favourites, count), {}, shell::StyleShelfEvent::ToggleFavourite, id).favourites, out, capacity);
}

int geode_style_shelf_push_recent(const char* const* recents, int count, const char* id, char* out, int capacity) {
    return linesOut(shelfApply({}, idsOf(recents, count), shell::StyleShelfEvent::PushRecent, id).recents, out, capacity);
}

int geode_style_shelf_remove_recent(const char* const* recents, int count, const char* id, char* out, int capacity) {
    return linesOut(shelfApply({}, idsOf(recents, count), shell::StyleShelfEvent::RemoveRecent, id).recents, out, capacity);
}

int geode_style_shelf_yours(const char* const* favourites, int favourite_count, const char* const* recents, int recent_count,
                            const char* const* order, int order_count, const char* on_screen, char* out, int capacity) {
    const shell::StyleShelf shelf{idsOf(favourites, favourite_count), idsOf(recents, recent_count), false};
    const std::optional<std::string> screen = on_screen ? std::optional<std::string>(on_screen) : std::nullopt;
    return linesOut(shell::styleShelfYours(shelf, idsOf(order, order_count), screen), out, capacity);
}

}  // extern "C"
