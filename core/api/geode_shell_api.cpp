// The C ABI of core/shell (api/geode_shell_api.h): forwarders only.
#include "api/geode_shell_api.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "api/geode_text_out.hpp"
#include "shell/AutoVisuals.hpp"
#include "shell/Behaviour.hpp"
#include "shell/ExportFps.hpp"
#include "shell/OpenedAudio.hpp"
#include "shell/PresetList.hpp"
#include "shell/StyleShelf.hpp"

namespace shell = geode::shell;

struct geode_auto_playlist {
    std::vector<shell::PlaylistEntry> entries;
};

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
    return shell::styleShelfApply({std::move(favourites), std::move(recents)}, {kind, id ? id : "", false});
}

std::vector<shell::PresetEntry> presetRows(const char* const* names, const char* const* folders, const int* flags,
                                           int count) {
    std::vector<shell::PresetEntry> rows;
    for (int i = 0; i < count; ++i) {
        shell::PresetEntry e;
        e.name = names && names[i] ? names[i] : "";
        e.folder = folders && folders[i] ? folders[i] : "";
        const int f = flags ? flags[i] : 0;
        e.builtIn = (f & GEODE_PRESET_BUILT_IN) != 0;
        e.forThisStyle = (f & GEODE_PRESET_FOR_THIS_STYLE) != 0;
        e.onStage = (f & GEODE_PRESET_ON_STAGE) != 0;
        rows.push_back(std::move(e));
    }
    return rows;
}

static_assert(static_cast<int>(shell::PresetFilter::Kind::All) == GEODE_PRESET_FILTER_ALL);
static_assert(static_cast<int>(shell::PresetFilter::Kind::ThisStyle) == GEODE_PRESET_FILTER_THIS_STYLE);
static_assert(static_cast<int>(shell::PresetFilter::Kind::Mine) == GEODE_PRESET_FILTER_MINE);
static_assert(static_cast<int>(shell::PresetFilter::Kind::BuiltIn) == GEODE_PRESET_FILTER_BUILT_IN);
static_assert(static_cast<int>(shell::PresetFilter::Kind::Folder) == GEODE_PRESET_FILTER_FOLDER);

static_assert(static_cast<int>(shell::OpenedRoute::NotOurs) == GEODE_OPENED_NOT_OURS);
static_assert(static_cast<int>(shell::OpenedRoute::Stale) == GEODE_OPENED_STALE);
static_assert(static_cast<int>(shell::OpenedRoute::Open) == GEODE_OPENED_OPEN);

std::optional<std::string> textOf(const char* text) { return text ? std::optional<std::string>(text) : std::nullopt; }

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
    const shell::StyleShelf shelf{idsOf(favourites, favourite_count), idsOf(recents, recent_count)};
    const std::optional<std::string> screen = on_screen ? std::optional<std::string>(on_screen) : std::nullopt;
    return linesOut(shell::styleShelfYours(shelf, idsOf(order, order_count), screen), out, capacity);
}

int geode_preset_filters(const char* const* folders, int count, int* out, int capacity) {
    const std::vector<std::string> names = idsOf(folders, count);
    const auto chips = shell::presetFilters(names);
    for (int i = 0; out && i < std::min(capacity, static_cast<int>(chips.size())); ++i) {
        const shell::PresetFilter& chip = chips[static_cast<size_t>(i)];
        int folder = -1;
        if (chip.kind == shell::PresetFilter::Kind::Folder) {
            folder = static_cast<int>(std::find(names.begin(), names.end(), chip.name) - names.begin());
        }
        out[2 * i] = static_cast<int>(chip.kind);
        out[2 * i + 1] = folder;
    }
    return static_cast<int>(chips.size());
}

int geode_preset_apply(const char* const* names, const char* const* folders, const int* flags, int count, int filter_kind,
                       const char* filter_folder, int* out) {
    const shell::PresetFilter filter{static_cast<shell::PresetFilter::Kind>(filter_kind), filter_folder ? filter_folder : ""};
    const auto rows = shell::presetApply(presetRows(names, folders, flags, count), filter);
    if (out) std::copy(rows.begin(), rows.end(), out);
    return static_cast<int>(rows.size());
}

int geode_preset_applied(const char* const* names, const int* flags, int count, const char* last_tapped) {
    const std::optional<std::string> tapped = last_tapped ? std::optional<std::string>(last_tapped) : std::nullopt;
    return shell::presetApplied(presetRows(names, nullptr, flags, count), tapped);
}

int geode_opened_audio_route(const char* action, const char* scheme, const char* type, const char* last_segment,
                             int from_history) {
    return static_cast<int>(
        shell::openedAudioRoute(textOf(action), textOf(scheme), textOf(type), textOf(last_segment), from_history != 0));
}

int geode_opened_audio_refuses(const char* scheme, const char* authority, const char* own_package,
                               const char* provider_package, int read_granted) {
    return shell::openedAudioRefuses(textOf(scheme), textOf(authority), own_package ? own_package : "",
                                     textOf(provider_package), read_granted != 0)
               ? 1
               : 0;
}

int geode_opened_audio_display_name(const char* raw, char* out, int capacity) {
    const std::optional<std::string> name = shell::openedAudioDisplayName(textOf(raw));
    return name ? geode::api::textOut(*name, out, capacity) : -1;
}

int geode_opened_media_id(const unsigned char* random16, char* out, int capacity) {
    std::array<std::uint8_t, 16> random{};
    if (random16) std::copy_n(random16, random.size(), random.begin());
    return geode::api::textOut(shell::openedMediaId(random), out, capacity);
}

int geode_opened_audio_is_opened(const char* media_id) { return shell::openedAudioIsOpened(textOf(media_id)) ? 1 : 0; }

geode_auto_playlist* geode_auto_playlist_read(const char* json, const char* (*resolve)(const char*)) {
    auto* playlist = new geode_auto_playlist;
    if (!json) return playlist;
    playlist->entries = shell::autoVisualsEntriesFromJson(json, [resolve](const std::string& id) {
        const char* resolved = resolve ? resolve(id.c_str()) : nullptr;
        return resolved ? std::string(resolved) : id;
    });
    return playlist;
}

int geode_auto_playlist_count(const geode_auto_playlist* playlist) {
    return playlist ? static_cast<int>(playlist->entries.size()) : 0;
}

const char* geode_auto_playlist_field(const geode_auto_playlist* playlist, int i, int field) {
    if (!playlist || i < 0 || i >= static_cast<int>(playlist->entries.size())) return nullptr;
    const shell::PlaylistEntry& e = playlist->entries[static_cast<size_t>(i)];
    switch (field) {
        case 0: return e.sceneId.c_str();
        case 1: return e.presetName ? e.presetName->c_str() : nullptr;
        case 2: return e.milkPath ? e.milkPath->c_str() : nullptr;
        case 3: return e.label.c_str();
        default: return nullptr;
    }
}

void geode_auto_playlist_destroy(geode_auto_playlist* playlist) { delete playlist; }

int geode_auto_playlist_to_json(const char* const* fields, int count, char* out, int capacity) {
    std::vector<shell::PlaylistEntry> entries;
    for (int i = 0; fields && i < count; ++i) {
        const char* const* f = fields + static_cast<std::ptrdiff_t>(i) * GEODE_AUTO_ENTRY_FIELDS;
        entries.push_back({f[0] ? f[0] : "", textOf(f[1]), textOf(f[2]), f[3] ? f[3] : ""});
    }
    return geode::api::textOut(shell::autoVisualsEntriesToJson(entries), out, capacity);
}

void geode_auto_visuals_apply(const int* state, const int* stored, const unsigned char* has, int playlist_count, int* out) {
    if (!state || !out) return;
    shell::AutoVisualsState now;
    now.randomEnabled = state[GEODE_AUTO_SAVED_RANDOM_ON] != 0;
    now.randomIntervalSec = state[GEODE_AUTO_SAVED_RANDOM_INTERVAL];
    now.randomIncludeStyles = state[GEODE_AUTO_SAVED_RANDOM_STYLES] != 0;
    now.randomIncludePresets = state[GEODE_AUTO_SAVED_RANDOM_PRESETS] != 0;
    now.randomIncludeMilk = state[GEODE_AUTO_SAVED_RANDOM_MILK] != 0;
    now.playlistEnabled = state[GEODE_AUTO_SAVED_PLAYLIST_ON] != 0;
    now.playlistIntervalSec = state[GEODE_AUTO_SAVED_PLAYLIST_INTERVAL];
    // Only the playlist's size takes part in the rules here; its entries are read by geode_auto_playlist_read.
    now.playlist.resize(static_cast<size_t>(std::max(playlist_count, 0)));
    auto saved = [&](int k) { return stored && has && has[k] != 0 ? std::optional<int>(stored[k]) : std::nullopt; };
    auto flag = [&](int k) { return saved(k) ? std::optional<bool>(*saved(k) != 0) : std::nullopt; };
    shell::AutoVisualsStored st;
    st.randomIntervalSec = saved(GEODE_AUTO_SAVED_RANDOM_INTERVAL);
    st.randomIncludeStyles = flag(GEODE_AUTO_SAVED_RANDOM_STYLES);
    st.randomIncludePresets = flag(GEODE_AUTO_SAVED_RANDOM_PRESETS);
    st.randomIncludeMilk = flag(GEODE_AUTO_SAVED_RANDOM_MILK);
    st.playlistEnabled = flag(GEODE_AUTO_SAVED_PLAYLIST_ON);
    st.playlistIntervalSec = saved(GEODE_AUTO_SAVED_PLAYLIST_INTERVAL);
    const shell::AutoVisualsState read = shell::autoVisualsApply(now, st, [](const std::string& id) { return id; });
    out[GEODE_AUTO_SAVED_RANDOM_ON] = read.randomEnabled ? 1 : 0;
    out[GEODE_AUTO_SAVED_RANDOM_INTERVAL] = read.randomIntervalSec;
    out[GEODE_AUTO_SAVED_RANDOM_STYLES] = read.randomIncludeStyles ? 1 : 0;
    out[GEODE_AUTO_SAVED_RANDOM_PRESETS] = read.randomIncludePresets ? 1 : 0;
    out[GEODE_AUTO_SAVED_RANDOM_MILK] = read.randomIncludeMilk ? 1 : 0;
    out[GEODE_AUTO_SAVED_PLAYLIST_ON] = read.playlistEnabled ? 1 : 0;
    out[GEODE_AUTO_SAVED_PLAYLIST_INTERVAL] = read.playlistIntervalSec;
}

}  // extern "C"
