#include <vector>

#include "api/geode_api.h"
#include "library/Search.hpp"

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

}  // extern "C"
