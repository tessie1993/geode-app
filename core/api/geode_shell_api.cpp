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
#include "shell/Tour.hpp"

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

// The tour's wire forms (geode_shell_api.h): a run and a progress as ints.
std::optional<shell::TourRun> runIn(const int* in, int len) {
    if (!in || len < GEODE_TOUR_RUN_HEAD) return std::nullopt;
    shell::TourRun run;
    run.at = in[0];
    run.seen = in[1];
    run.replay = in[2] != 0;
    for (int i = GEODE_TOUR_RUN_HEAD; i < len; ++i) run.chapters.push_back(static_cast<shell::TourChapter>(in[i]));
    return run;
}

shell::TourProgress progressIn(const int* in, int len) {
    shell::TourProgress p;
    if (!in || len < GEODE_TOUR_PROGRESS_HEAD) return p;
    p.off = in[0] != 0;
    if (in[1] >= 0) p.resume = static_cast<shell::TourSpot>(in[1]);
    const int rest = len - GEODE_TOUR_PROGRESS_HEAD;
    const int done = std::clamp(in[2], 0, rest);
    for (int i = 0; i < rest; ++i) {
        const int v = in[GEODE_TOUR_PROGRESS_HEAD + i];
        if (i < done) {
            p.done.push_back(static_cast<shell::TourChapter>(v));
        } else {
            p.tips.push_back(static_cast<shell::GestureTip>(v));
        }
    }
    return p;
}

shell::TourFacts factsOf(unsigned shown, int place, int playing) {
    return {shown, static_cast<shell::TourPlace>(place), playing != 0};
}

shell::TourCover coverOf(unsigned mask) {
    return {(mask & GEODE_TOUR_COVER_FIRST_RUN) != 0, (mask & GEODE_TOUR_COVER_PAYWALL) != 0, (mask & GEODE_TOUR_COVER_PIP) != 0,
            (mask & GEODE_TOUR_COVER_PERMISSION_RATIONALE) != 0, (mask & GEODE_TOUR_COVER_AWAY) != 0};
}

int intsOut(const std::vector<int>& wire, int* out, int capacity) {
    for (int i = 0; out && i < std::min(capacity, static_cast<int>(wire.size())); ++i) out[i] = wire[static_cast<size_t>(i)];
    return static_cast<int>(wire.size());
}

int runOut(const shell::TourRun& run, int* out, int capacity) {
    std::vector<int> wire{run.at, run.seen, run.replay ? 1 : 0};
    for (shell::TourChapter c : run.chapters) wire.push_back(static_cast<int>(c));
    return intsOut(wire, out, capacity);
}

int progressOut(const shell::TourProgress& p, int* out, int capacity) {
    std::vector<int> wire{p.off ? 1 : 0, p.resume ? static_cast<int>(*p.resume) : -1, static_cast<int>(p.done.size())};
    for (shell::TourChapter c : p.done) wire.push_back(static_cast<int>(c));
    for (shell::GestureTip t : p.tips) wire.push_back(static_cast<int>(t));
    return intsOut(wire, out, capacity);
}

shell::TourRect rectIn(const int* r) { return {r[0], r[1], r[2], r[3]}; }

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

int geode_opened_audio_route(const char* action, const char* scheme, const char* type, int from_history) try {
    return static_cast<int>(
        shell::openedAudioRoute(textOf(action), textOf(scheme), textOf(type), from_history != 0));
} catch (...) {
    return GEODE_OPENED_NOT_OURS;
}

int geode_opened_audio_refuses(const char* scheme, const char* authority, const char* own_package,
                               const char* provider_package, int read_granted) try {
    return shell::openedAudioRefuses(textOf(scheme), textOf(authority), own_package ? own_package : "",
                                     textOf(provider_package), read_granted != 0)
               ? 1
               : 0;
} catch (...) {
    return 1;
}

int geode_opened_audio_display_name(const char* raw, char* out, int capacity) try {
    const std::optional<std::string> name = shell::openedAudioDisplayName(textOf(raw));
    return name ? geode::api::textOut(*name, out, capacity) : -1;
} catch (...) {
    return -1;
}

int geode_opened_media_id(const unsigned char* random16, char* out, int capacity) {
    std::array<std::uint8_t, 16> random{};
    if (random16) std::copy_n(random16, random.size(), random.begin());
    return geode::api::textOut(shell::openedMediaId(random), out, capacity);
}

int geode_opened_audio_is_opened(const char* media_id) try {
    return shell::openedAudioIsOpened(textOf(media_id)) ? 1 : 0;
} catch (...) {
    return 0;
}

int geode_tour_reached(int spot, unsigned shown, int place, int playing) {
    return shell::tourReached(static_cast<shell::TourSpot>(spot), factsOf(shown, place, playing)) ? 1 : 0;
}

int geode_tour_step(const int* run, int run_len, unsigned shown, int place, int playing, int* out3) {
    const auto r = runIn(run, run_len);
    const auto step = r ? shell::tourStep(*r, factsOf(shown, place, playing)) : std::nullopt;
    if (!step) return 0;
    if (out3) {
        out3[0] = static_cast<int>(step->spot);
        out3[1] = step->index;
        out3[2] = step->count;
    }
    return 1;
}

int geode_tour_pass(const int* run, int run_len, int spot, int* out, int capacity) {
    const auto r = runIn(run, run_len);
    return runOut(shell::tourPass(r.value_or(shell::TourRun{}), static_cast<shell::TourSpot>(spot)), out, capacity);
}

int geode_tour_next_chapter(const int* run, int run_len, int* out, int capacity) {
    const auto r = runIn(run, run_len);
    const auto next = r ? shell::tourNextChapter(*r) : std::nullopt;
    return next ? runOut(*next, out, capacity) : -1;
}

int geode_tour_held(unsigned cover) { return shell::tourHeld(coverOf(cover)) ? 1 : 0; }

int geode_tour_shows(const int* run, int run_len, const int* progress, int progress_len, unsigned cover) {
    return shell::tourShows(runIn(run, run_len), progressIn(progress, progress_len), coverOf(cover)) ? 1 : 0;
}

int geode_tour_may_tip(int tip, const int* progress, int progress_len, unsigned cover, const int* run, int run_len) {
    return shell::tourMayTip(static_cast<shell::GestureTip>(tip), progressIn(progress, progress_len), coverOf(cover),
                             runIn(run, run_len))
               ? 1
               : 0;
}

int geode_tour_due(const int* progress, int progress_len, unsigned shown, int place, int playing) {
    const auto due = shell::tourDue(progressIn(progress, progress_len), factsOf(shown, place, playing));
    return due ? static_cast<int>(*due) : -1;
}

int geode_tour_resume(int chapter, const int* progress, int progress_len, int* out, int capacity) {
    return runOut(shell::tourResume(static_cast<shell::TourChapter>(chapter), progressIn(progress, progress_len)), out, capacity);
}

int geode_tour_replay(unsigned chapters, int* out, int capacity) {
    std::vector<shell::TourChapter> asked;
    for (int c = 0; c < shell::kTourChapterCount; ++c) {
        if (chapters & (1u << c)) asked.push_back(static_cast<shell::TourChapter>(c));
    }
    const auto run = shell::tourReplay(asked);
    return run ? runOut(*run, out, capacity) : -1;
}

int geode_tour_ended(const int* progress, int progress_len, const int* run, int run_len, int skipped, int* out, int capacity) {
    return progressOut(shell::tourEnded(progressIn(progress, progress_len), runIn(run, run_len).value_or(shell::TourRun{}),
                                        skipped != 0),
                       out, capacity);
}

int geode_tour_offers_pro(const int* run, int run_len, int skipped) {
    return shell::tourOffersPro(runIn(run, run_len).value_or(shell::TourRun{}), skipped != 0) ? 1 : 0;
}

int geode_tour_decode(const char* done, const char* resume, const char* tips, int legacy_seen, int off, int* out, int capacity) {
    return progressOut(shell::tourDecode(textOf(done), textOf(resume), textOf(tips), legacy_seen != 0, off != 0), out, capacity);
}

int geode_tour_encode(const int* progress, int progress_len, int which, char* out, int capacity) {
    const shell::TourProgress p = progressIn(progress, progress_len);
    switch (which) {
        case GEODE_TOUR_TEXT_DONE: return geode::api::textOut(shell::tourEncodeDone(p), out, capacity);
        case GEODE_TOUR_TEXT_RESUME: {
            const auto resume = shell::tourEncodeResume(p);
            return resume ? geode::api::textOut(*resume, out, capacity) : -1;
        }
        case GEODE_TOUR_TEXT_TIPS: return geode::api::textOut(shell::tourEncodeTips(p), out, capacity);
        default: return -1;
    }
}

void geode_tour_place_card(const int* hole, int card_width, int card_height, const int* safe, int gap, int* out2) {
    if (!safe || !out2) return;
    const shell::TourPoint at = shell::tourPlaceCard(hole ? std::optional<shell::TourRect>(rectIn(hole)) : std::nullopt,
                                                     card_width, card_height, rectIn(safe), gap);
    out2[0] = at.x;
    out2[1] = at.y;
}

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
        case 2: return e.label.c_str();
        default: return nullptr;
    }
}

void geode_auto_playlist_destroy(geode_auto_playlist* playlist) { delete playlist; }

int geode_auto_playlist_to_json(const char* const* fields, int count, char* out, int capacity) {
    std::vector<shell::PlaylistEntry> entries;
    for (int i = 0; fields && i < count; ++i) {
        const char* const* f = fields + static_cast<std::ptrdiff_t>(i) * GEODE_AUTO_ENTRY_FIELDS;
        entries.push_back({f[0] ? f[0] : "", textOf(f[1]), f[2] ? f[2] : ""});
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
    st.playlistEnabled = flag(GEODE_AUTO_SAVED_PLAYLIST_ON);
    st.playlistIntervalSec = saved(GEODE_AUTO_SAVED_PLAYLIST_INTERVAL);
    const shell::AutoVisualsState read = shell::autoVisualsApply(now, st, [](const std::string& id) { return id; });
    out[GEODE_AUTO_SAVED_RANDOM_ON] = read.randomEnabled ? 1 : 0;
    out[GEODE_AUTO_SAVED_RANDOM_INTERVAL] = read.randomIntervalSec;
    out[GEODE_AUTO_SAVED_RANDOM_STYLES] = read.randomIncludeStyles ? 1 : 0;
    out[GEODE_AUTO_SAVED_RANDOM_PRESETS] = read.randomIncludePresets ? 1 : 0;
    out[GEODE_AUTO_SAVED_PLAYLIST_ON] = read.playlistEnabled ? 1 : 0;
    out[GEODE_AUTO_SAVED_PLAYLIST_INTERVAL] = read.playlistIntervalSec;
}

}  // extern "C"
