#pragma once

#include <cstdint>
#include <memory>

#include "audio/ingest/TrackPump.hpp"

// The platform decoder behind an export's track (TrackPump's DecodePort): AMediaExtractor over the span's
// descriptor, its first audio track, AMediaCodec decoding it. Android only (libmediandk), never in a host build;
// every decision is TrackPump's.
namespace geode::audio::ingest {

// A decoder from startMs (the extractor seeks to the sync sample at or before it), over its own duplicate of
// the span's descriptor; null when the bytes hold no audio track the platform decodes.
std::unique_ptr<DecodePort> openPlatformDecoder(const TrackSpan& span, int64_t startMs);
// The same, saying in *unreadable (when not null) whether a null is a source the extractor could not read at all
// (true) rather than one it read and found no audio track in (false). A name of its own: openPlatformDecoder is
// handed around as a DecoderOpener.
std::unique_ptr<DecodePort> openPlatformDecoderReporting(const TrackSpan& span, int64_t startMs, bool* unreadable);

}  // namespace geode::audio::ingest
