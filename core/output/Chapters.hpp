#pragma once
#include <cstdint>
#include <vector>

namespace geode::output {

// Chapter spans for a finished render (ADR 0006, core/output): only the times; a chapter's title is whoever names
// it from `source`. Nothing writes chapters into a render yet: MediaMuxer and AMediaMuxer have no chapter track.
struct ChapterSpan {
    int64_t startMs = 0;
    int64_t durationMs = 0;
    // The mix clip or marker this chapter opens at, 0-based; kLeadIn for the lead-in chapter fromMarkers adds.
    int source = 0;

    static constexpr int kLeadIn = -1;
    int64_t endMs() const { return startMs + durationMs; }
};

// One chapter per mix clip, at where it landed once muxed. Fewer than two clips is no chapters: one track
// is not a mix, and a lone 0:00 chapter is noise.
std::vector<ChapterSpan> chaptersFromClips(const int64_t* startsMs, const int64_t* durationsMs, int count);

// The editor's marker lane: each marker opens a chapter that runs to the next, the last one to totalMs.
// Markers are sorted first. When the first marker is after 0:00 a lead-in chapter (source kLeadIn) covers
// the opening, because players and video sites ignore a chapter list that does not start at 0:00. Fewer than two
// markers is no chapters.
std::vector<ChapterSpan> chaptersFromMarkers(const int64_t* markersMs, int count, int64_t totalMs);

}  // namespace geode::output
