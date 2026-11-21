#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "output/EncodeSink.hpp"

struct AMediaFormat;

namespace geode::output::sink {

// The phone's codecs behind the sink's seam (MediaCodecSink.cpp, NDK media, API 26+): encoders by MIME type
// (AMediaCodec), the video one fed through AMediaCodec_createInputSurface, and an MP4 muxer (AMediaMuxer)
// writing into fd. The muxer takes its own duplicate of fd, so the caller may close fd once the muxer is
// made; fd must be open for reading and writing (MPEG-4 seeks back to write its index).
// Android only: kept out of every host-test compile line.
std::unique_ptr<Codecs> mediaCodecs(int fd);

// A format the caller made (AMediaExtractor_getTrackFormat) as a track format for these Codecs' muxer; takes it over
// and deletes it with the track format.
std::unique_ptr<TrackFormat> trackFormatOf(AMediaFormat* owned);

// What an audio encoder's output format (from one of these Codecs) names: its rate, channels and codec-specific
// data (csd-0), for a muxer elsewhere. False when it names no rate or channels.
bool audioTrackInfo(const TrackFormat& format, int* rateHz, int* channels, std::vector<uint8_t>* csd);

}  // namespace geode::output::sink
