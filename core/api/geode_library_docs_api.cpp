// The C ABI of the library's documents (core/library: History, SmartPlaylist, PlaylistRules, SessionDoc): text in
// and text out, the arrays gathered and the file stem rule handed in. Marshals only.
#include <string>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "library/History.hpp"
#include "library/PlaylistRules.hpp"
#include "library/SessionDoc.hpp"
#include "library/SmartPlaylist.hpp"
#include "viz/LookFiles.hpp"

namespace {

using geode::library::PlaylistEditKind;
static_assert(static_cast<int>(PlaylistEditKind::Create) == GEODE_MUSIC_PLAYLIST_CREATE);
static_assert(static_cast<int>(PlaylistEditKind::Import) == GEODE_MUSIC_PLAYLIST_IMPORT);
static_assert(static_cast<int>(PlaylistEditKind::AddTrack) == GEODE_MUSIC_PLAYLIST_ADD);
static_assert(static_cast<int>(PlaylistEditKind::RemoveTrack) == GEODE_MUSIC_PLAYLIST_REMOVE);
static_assert(static_cast<int>(PlaylistEditKind::Move) == GEODE_MUSIC_PLAYLIST_MOVE);
static_assert(static_cast<int>(PlaylistEditKind::Rename) == GEODE_MUSIC_PLAYLIST_RENAME);

// A playlist's file stem: the look files' rule, which the shell's PresetStore.safeFileName also asks.
std::string playlistStem(const std::string& name) { return geode::viz::files::safeFileName(name); }

std::string textOf(const char* s) { return s ? s : ""; }

const char* hold(std::string& slot, std::string value) {
    slot = std::move(value);
    return slot.c_str();
}

std::vector<std::string> textsOf(const char* const* texts, int count) {
    std::vector<std::string> out;
    for (int i = 0; texts && i < count; ++i) out.push_back(textOf(texts[i]));
    return out;
}

// A smart playlist as it crosses: the flag, the limit and 3 strings per rule (field, op, value).
geode::library::SmartPlaylist smartOf(const char* name, int match_all, int limit, const char* const* rules,
                                      int rule_count) {
    geode::library::SmartPlaylist p;
    p.name = textOf(name);
    p.matchAll = match_all != 0;
    p.limit = limit;
    for (int i = 0; rules && i < rule_count; ++i) {
        const char* const* cell = rules + static_cast<size_t>(i) * 3;
        if (auto rule = geode::library::smartRuleOf(textOf(cell[0]), textOf(cell[1]), textOf(cell[2]))) {
            p.rules.push_back(std::move(*rule));
        }
    }
    return p;
}

}  // namespace

extern "C" {

// The play history: the module's own calls, text in and text out.
int geode_history_corrupt(const char* text) try {
    return geode::library::historyParse(textOf(text)).corrupt ? 1 : 0;
} catch (...) {
    return 0;
}

const char* geode_history_record_play(const char* state, const char* uri, const char* title, const char* artist,
                                      long long now_ms) try {
    thread_local std::string out;
    return hold(out, geode::library::historyRecordPlay(textOf(state), textOf(uri), textOf(title), textOf(artist), now_ms).state);
} catch (...) {
    return nullptr;
}

const char* geode_history_add_listen(const char* state, const char* uri, long long delta_ms) try {
    thread_local std::string out;
    return hold(out, geode::library::historyAddListen(textOf(state), textOf(uri), delta_ms).state);
} catch (...) {
    return nullptr;
}

const char* geode_history_file_text(const char* state) try {
    thread_local std::string out;
    return hold(out, geode::library::historyFileText(textOf(state)));
} catch (...) {
    return nullptr;
}

const char* geode_history_recent(const char* state, int limit) try {
    thread_local std::string out;
    return hold(out, geode::library::historyWire(geode::library::historyRecent(textOf(state), limit)));
} catch (...) {
    return nullptr;
}

// Smart playlists: the module's own calls; the ABI only gathers the arrays.
const char* geode_smart_list(const char* const* texts, int count) try {
    thread_local std::string out;
    return hold(out, geode::library::smartWire(geode::library::smartList(textsOf(texts, count))));
} catch (...) {
    return nullptr;
}

const char* geode_smart_save_text(const char* name, int match_all, int limit, const char* const* rules,
                                  int rule_count) try {
    thread_local std::string out;
    const auto text = geode::library::smartSaveText(smartOf(name, match_all, limit, rules, rule_count));
    return text ? hold(out, *text) : nullptr;
} catch (...) {
    return nullptr;
}

int geode_smart_resolve(int match_all, int limit, const char* const* rules, int rule_count, const char* const* tracks,
                        const long long* durations_ms, const long long* added_sec, int track_count, const char* history,
                        const char* const* favourites, int favourite_count, long long now_ms, int* out,
                        int max_out) try {
    if (track_count < 0 || (track_count > 0 && !tracks)) return 0;
    std::vector<geode::library::SmartTrack> library;
    library.reserve(static_cast<size_t>(track_count));
    for (int i = 0; i < track_count; ++i) {
        const char* const* cell = tracks + static_cast<size_t>(i) * 5;
        library.push_back({textOf(cell[0]), textOf(cell[1]), textOf(cell[2]), textOf(cell[3]), textOf(cell[4]),
                           durations_ms ? durations_ms[i] : 0, added_sec ? added_sec[i] : 0});
    }
    const std::vector<int> rows =
        geode::library::smartResolveHistory(smartOf(nullptr, match_all, limit, rules, rule_count), library,
                                            textOf(history), textsOf(favourites, favourite_count), now_ms);
    const int n = static_cast<int>(rows.size());
    if (out) {
        for (int i = 0; i < n && i < max_out; ++i) out[i] = rows[static_cast<size_t>(i)];
    }
    return n;
} catch (...) {
    return 0;
}

// Ordinary playlists: the module's own calls; the ABI gathers the arrays and hands in the stem rule.
const char* geode_music_playlist_list(const char* const* texts, int count) try {
    thread_local std::string out;
    return hold(out, geode::library::playlistWire(geode::library::playlistList(textsOf(texts, count))));
} catch (...) {
    return nullptr;
}

const char* geode_music_playlist_apply(int kind, const char* name, const char* new_name, const char* track,
                                       const char* const* tracks, int track_count, int from, int to,
                                       const char* const* texts, int text_count, const char* named) try {
    thread_local std::string out;
    if (kind < GEODE_MUSIC_PLAYLIST_CREATE || kind > GEODE_MUSIC_PLAYLIST_RENAME) return nullptr;
    geode::library::PlaylistEdit edit;
    edit.kind = static_cast<PlaylistEditKind>(kind);
    edit.name = textOf(name);
    edit.newName = textOf(new_name);
    edit.track = textOf(track);
    edit.tracks = textsOf(tracks, track_count);
    edit.from = from;
    edit.to = to;
    geode::library::PlaylistFolder folder;
    folder.fileTexts = textsOf(texts, text_count);
    if (named) folder.namedFileText = std::string(named);
    return hold(out, geode::library::playlistOutcomeWire(geode::library::playlistApply(folder, edit, playlistStem)));
} catch (...) {
    return nullptr;
}

const char* geode_music_playlist_migration(const char* const* file_names, const char* const* texts, int count) try {
    thread_local std::string out;
    std::vector<geode::library::PlaylistFileText> files;
    for (int i = 0; file_names && i < count; ++i) {
        files.push_back({textOf(file_names[i]), texts ? textOf(texts[i]) : std::string()});
    }
    return hold(out, geode::library::playlistRenameWire(geode::library::playlistMigrationPlan(files, playlistStem)));
} catch (...) {
    return nullptr;
}

const char* geode_music_playlist_import_base(const char* parsed_name, const char* file_name, const char* untitled) try {
    thread_local std::string out;
    return hold(out, geode::library::playlistImportBase(textOf(parsed_name), textOf(file_name), textOf(untitled)));
} catch (...) {
    return nullptr;
}

// The saved play queue.
const char* geode_session_read(const char* text, const char* position_text) try {
    thread_local std::string out;
    auto session = geode::library::sessionParse(textOf(text));
    if (session) geode::library::sessionApplyPosition(*session, textOf(position_text));
    return hold(out, session ? geode::library::sessionWire(*session) : std::string());
} catch (...) {
    return nullptr;
}

const char* geode_session_write(const char* const* tracks, int count, int index, long long position_ms) try {
    thread_local std::string out;
    geode::library::SavedSession session;
    for (int i = 0; tracks && i < count; ++i) {
        const char* const* cell = tracks + static_cast<size_t>(i) * 3;
        session.tracks.push_back({textOf(cell[0]), textOf(cell[1]), textOf(cell[2])});
    }
    session.index = index;
    session.positionMs = position_ms;
    const geode::library::SessionWrite write = geode::library::sessionWrite(session);
    return hold(out, write.clear ? std::string() : write.json);
} catch (...) {
    return nullptr;
}

const char* geode_session_position_write(int index, long long position_ms) try {
    thread_local std::string out;
    return hold(out, geode::library::sessionPositionWrite(index, position_ms));
} catch (...) {
    return nullptr;
}

}  // extern "C"
