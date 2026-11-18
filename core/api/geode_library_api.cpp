#include <string>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "library/History.hpp"
#include "library/Search.hpp"
#include "library/SmartPlaylist.hpp"

namespace {

std::string_view view(const char* s) { return s ? std::string_view(s) : std::string_view(); }

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

static_assert(geode::library::kMaxSheetTracks == GEODE_SEARCH_SHEET_TRACKS);

std::vector<std::string_view> names(const char* const* texts, int count) {
    std::vector<std::string_view> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) out.push_back(view(texts[i]));
    return out;
}

// The packed answer: the counts of each list, then each list.
int pack(const std::vector<std::vector<int>>& lists, int* out, int max_out) {
    std::vector<int> packed;
    for (const auto& list : lists) packed.push_back(static_cast<int>(list.size()));
    for (const auto& list : lists) packed.insert(packed.end(), list.begin(), list.end());
    const int n = static_cast<int>(packed.size());
    if (out) {
        for (int i = 0; i < n && i < max_out; ++i) out[i] = packed[static_cast<size_t>(i)];
    }
    return n;
}

}  // namespace

extern "C" {

int geode_library_search(const char* const* rows, int track_count, const char* query, const char* focus,
                         const char* artist, const char* album, const char* title, int* out, int max_out) try {
    if (track_count < 0 || (track_count > 0 && !rows)) return 0;
    std::vector<geode::library::SearchRow> library;
    library.reserve(static_cast<size_t>(track_count));
    for (int i = 0; i < track_count; ++i) {
        const size_t at = static_cast<size_t>(i) * 3;
        library.push_back({view(rows[at]), view(rows[at + 1]), view(rows[at + 2])});
    }
    const geode::library::SearchRequest request{view(query), view(focus), view(artist), view(album), view(title)};
    const std::vector<int> hits = geode::library::searchTracks(library, request);
    const int n = static_cast<int>(hits.size());
    if (out) {
        for (int i = 0; i < n && i < max_out; ++i) out[i] = hits[static_cast<size_t>(i)];
    }
    return n;
} catch (...) {
    return 0;
}

struct geode_library_index {
    geode::library::SearchIndex index;
};

geode_library_index* geode_library_index_create(const char* const* rows, const long long* durations_ms,
                                                const long long* added_sec, int track_count) try {
    if (track_count < 0 || (track_count > 0 && !rows)) return nullptr;
    const auto n = static_cast<size_t>(track_count);
    std::vector<geode::library::SearchRow> library;
    library.reserve(n);
    for (size_t i = 0; i < n; ++i) library.push_back({view(rows[i * 3]), view(rows[i * 3 + 1]), view(rows[i * 3 + 2])});
    std::vector<long long> durations;
    std::vector<long long> added;
    if (durations_ms) durations.assign(durations_ms, durations_ms + n);
    if (added_sec) added.assign(added_sec, added_sec + n);
    return new geode_library_index{geode::library::SearchIndex(library, durations, added)};
} catch (...) {
    return nullptr;
}

void geode_library_index_destroy(geode_library_index* index) { delete index; }

int geode_library_index_order(const geode_library_index* index, int sort, int* out, int max_out) try {
    if (!index) return 0;
    using geode::library::Sort;
    const Sort by = sort >= static_cast<int>(Sort::Title) && sort <= static_cast<int>(Sort::Added)
                        ? static_cast<Sort>(sort)
                        : Sort::Title;
    const std::vector<int> rows = index->index.order(by);
    const int n = static_cast<int>(rows.size());
    if (out) {
        for (int i = 0; i < n && i < max_out; ++i) out[i] = rows[static_cast<size_t>(i)];
    }
    return n;
} catch (...) {
    return 0;
}

int geode_library_index_search(const geode_library_index* index, const char* query, int* out, int max_out) try {
    if (!index) return 0;
    const geode::library::SearchHits hits = index->index.search(view(query));
    std::vector<int> packed = {static_cast<int>(hits.tracks.size()), static_cast<int>(hits.albums.size()),
                               static_cast<int>(hits.artists.size())};
    packed.insert(packed.end(), hits.tracks.begin(), hits.tracks.end());
    packed.insert(packed.end(), hits.albums.begin(), hits.albums.end());
    packed.insert(packed.end(), hits.artists.begin(), hits.artists.end());
    const int n = static_cast<int>(packed.size());
    if (out) {
        for (int i = 0; i < n && i < max_out; ++i) out[i] = packed[static_cast<size_t>(i)];
    }
    return n;
} catch (...) {
    return 0;
}

int geode_search_sheet(const char* const* tracks, const unsigned char* preferred, int track_count,
                       const char* const* playlists, int playlist_count, const char* const* presets, int preset_count,
                       const char* query, int* out, int max_out) try {
    using geode::library::SheetTrack;
    const bool counted = track_count >= 0 && playlist_count >= 0 && preset_count >= 0;
    const bool held = (track_count <= 0 || tracks) && (playlist_count <= 0 || playlists) && (preset_count <= 0 || presets);
    geode::library::SheetHits hits;
    if (counted && held) {
        std::vector<SheetTrack> rows;
        rows.reserve(static_cast<size_t>(track_count));
        for (int i = 0; i < track_count; ++i) {
            const char* const* cell = tracks + static_cast<size_t>(i) * GEODE_SEARCH_SHEET_FIELDS;
            rows.push_back(SheetTrack{{view(cell[0]), view(cell[1]), view(cell[2])}, view(cell[3]), view(cell[4]),
                                      preferred != nullptr && preferred[i] != 0});
        }
        hits = geode::library::searchSheet(rows, names(playlists, playlist_count), names(presets, preset_count),
                                           view(query));
    }
    return pack({hits.tracks, hits.playlists, hits.presets}, out, max_out);
} catch (...) {
    return 0;
}

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

}  // extern "C"
