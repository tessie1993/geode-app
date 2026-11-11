#include <vector>

#include "api/geode_api.h"
#include "library/Search.hpp"
#include "library/StudioFiles.hpp"

namespace {

std::string_view view(const char* s) { return s ? std::string_view(s) : std::string_view(); }

}  // namespace

extern "C" {

int geode_library_search(const char* const* rows, int track_count, const char* query, const char* focus,
                         const char* artist, const char* album, const char* title, int* out, int max_out) {
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
}

struct geode_library_index {
    geode::library::SearchIndex index;
};

geode_library_index* geode_library_index_create(const char* const* rows, const long long* durations_ms,
                                                const long long* added_sec, int track_count) {
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
}

void geode_library_index_destroy(geode_library_index* index) { delete index; }

int geode_library_index_order(const geode_library_index* index, int sort, int* out, int max_out) {
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
}

int geode_library_index_search(const geode_library_index* index, const char* query, int* out, int max_out) {
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
}

int geode_studio_move_takes_out(const char* projects_dir, const char* takes_dir) {
    if (!projects_dir || !takes_dir) return 0;
    return geode::library::studio::moveTakesOut(projects_dir, takes_dir).failed;
}

}  // extern "C"
