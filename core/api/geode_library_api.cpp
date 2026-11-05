#include <algorithm>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

int geode_studio_move_takes_out(const char* projects_dir, const char* takes_dir) {
    if (!projects_dir || !takes_dir) return 0;
    return geode::library::studio::moveTakesOut(projects_dir, takes_dir).failed;
}

int geode_library_filter(const char* const* cells, int row_count, int fields_per_row,
                         const unsigned char* preferred, const char* query, int flags, int* out, int max_out) {
    if (row_count <= 0 || fields_per_row < 0 || !cells || !out || max_out <= 0) return 0;
    const auto rows = static_cast<size_t>(row_count);
    const auto width = static_cast<size_t>(fields_per_row) + 1;
    std::vector<std::string_view> fields(rows * width);
    std::vector<geode::library::FilterRow> table(rows);
    for (size_t r = 0; r < rows; ++r) {
        const char* const* row = cells + r * width;
        for (size_t f = 1; f < width; ++f) fields[r * width + f] = view(row[f]);
        table[r].key = row[0] ? std::optional<std::string_view>(row[0]) : std::nullopt;
        table[r].preferred = preferred && preferred[r] != 0;
        table[r].fields = std::span<const std::string_view>(fields.data() + r * width + 1, width - 1);
    }
    geode::library::FilterOptions options;
    options.typos = (flags & GEODE_LIBRARY_FILTER_TYPOS) != 0;
    options.blankKeepsAll = (flags & GEODE_LIBRARY_FILTER_BLANK_ALL) != 0;
    const std::vector<int> kept =
        geode::library::filterRows(table, view(query), static_cast<size_t>(max_out), options);
    for (size_t i = 0; i < kept.size(); ++i) out[i] = kept[i];
    return static_cast<int>(kept.size());
}

int geode_library_fold(const char* text, char* out, int cap) {
    const std::string folded = geode::library::foldText(view(text));
    if (out && cap > 0) {
        const size_t n = std::min(folded.size(), static_cast<size_t>(cap) - 1);
        std::memcpy(out, folded.data(), n);
        out[n] = '\0';
    }
    return static_cast<int>(folded.size());
}

}  // extern "C"
