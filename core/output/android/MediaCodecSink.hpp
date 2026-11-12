#pragma once
#include <memory>

#include "output/EncodeSink.hpp"

namespace geode::output::sink {

// The phone's codecs behind the sink's seam (MediaCodecSink.cpp, NDK media, API 26+): encoders by MIME type
// (AMediaCodec), the video one fed through AMediaCodec_createInputSurface, and an MP4 muxer (AMediaMuxer)
// writing into fd. The muxer takes its own duplicate of fd, so the caller may close fd once the muxer is
// made; fd must be open for reading and writing (MPEG-4 seeks back to write its index).
// Android only: kept out of every host-test compile line.
std::unique_ptr<Codecs> mediaCodecs(int fd);

}  // namespace geode::output::sink
