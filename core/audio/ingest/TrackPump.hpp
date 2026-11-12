#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "audio/ingest/AiffReader.hpp"
#include "audio/ingest/TrackFeed.hpp"

// An export's track, decoded as the export asks for it, for the export renderer's own sound engine: the same
// core/sound code hears the same audio as live, frame by frame, stepped at 1/fps. Streaming, never decoded up
// front: a long hi-res track would be hundreds of megabytes of floats.
//
// The pump owns a TrackFeed and a TrackSource. Asked for audio up to a target (a millisecond or a video frame's
// exact end, both counted from the feed's start), it steps the source until the feed knows its rate, aims the
// feed, then hands the listener the feed's frames, stepping the source each time the held chunk runs out, until
// the target is reached or the track has ended.
//
// Two sources: an AIFF through AiffReader (the platform extractor does not read AIFF), and a platform decoder
// behind DecodePort (audio/ingest/android/TrackDecoder: AMediaExtractor and AMediaCodec), whose pump rule is
// DecodeSource here: feed the codec one sample per step until end of stream is queued, take one output buffer,
// push what it holds, release it, stop after the buffer flagged end of stream. Pure: a host test drives it with
// a fake port (core/tests/track_pump_test). One thread, the export's.
namespace geode::audio::ingest {

// Where the track's bytes are: a descriptor the shell opened from the content URI and the span its
// AssetFileDescriptor declares. length < 0: none declared, the whole descriptor from its start.
struct TrackSpan {
    int fd = -1;
    int64_t offset = 0;
    int64_t length = -1;
};

// What the platform extractor is given for a span: the declared one, or for none the whole file, the length
// android.media.MediaExtractor.setDataSource(FileDescriptor) passes (0x7ffffffffffffff).
struct ExtractorSpan {
    int64_t offset = 0;
    int64_t length = 0;
};
inline constexpr int64_t kWholeFileLength = 0x7ffffffffffffffLL;
inline ExtractorSpan extractorSpan(const TrackSpan& span) {
    if (span.length < 0) return {0, kWholeFileLength};
    return {span.offset, span.length};
}

// Where the extractor seeks before decoding a range from startMs (to the sync sample at or before it, which
// TrackFeed then cuts), or -1: a range from the track's start decodes from the first sample.
inline int64_t decodeSeekUs(int64_t startMs) { return startMs > 0 ? startMs * 1000 : -1; }

// android.media.AudioFormat.ENCODING_PCM_FLOAT, the value the codec's output format carries under
// "pcm-encoding". Without that key, or with any other value, a decoder's output is 16-bit.
inline constexpr int32_t kPcmEncodingFloat = 4;
inline bool isFloatPcm(bool hasEncoding, int32_t encoding) { return hasEncoding && encoding == kPcmEncodingFloat; }

// The first track whose mime type starts "audio/" is the one decoded.
bool isAudioMime(const char* mime);

// One step of a track: pushes its next chunk (or nothing yet) into the feed; false once the track has run out.
class TrackSource {
public:
    virtual ~TrackSource() = default;
    virtual bool step(TrackFeed& feed) = 0;
};

// An AIFF, a read of TrackFeed::kAiffChunk samples a step.
class AiffSource final : public TrackSource {
public:
    explicit AiffSource(std::unique_ptr<AiffReader> reader) : reader_(std::move(reader)) {}
    bool step(TrackFeed& feed) override { return feed.pushAiff(*reader_); }

private:
    std::unique_ptr<AiffReader> reader_;
};

// One output buffer of a platform decoder, valid until DecodePort::releaseOutput.
struct DecodedChunk {
    const uint8_t* data = nullptr;
    size_t bytes = 0;  // 0: a buffer with no samples (the end-of-stream one often is)
    bool isFloat = false;
    int channels = 0;
    int sampleRate = 0;
    int64_t ptsUs = 0;
    bool endOfStream = false;
};

// A platform decoder over its extractor. A failure of the codec itself throws (std::runtime_error).
class DecodePort {
public:
    virtual ~DecodePort() = default;
    // When the codec has an input buffer free: the extractor's next sample into it, or end of stream once the
    // extractor has none. True once end of stream is queued; never called again after that.
    virtual bool feedInput() = 0;
    // The next output buffer if one is ready within the codec's wait; false when none is (a format change, a
    // codec still filling).
    virtual bool takeOutput(DecodedChunk& out) = 0;
    // Hands back the buffer takeOutput gave.
    virtual void releaseOutput() = 0;
};

class DecodeSource final : public TrackSource {
public:
    explicit DecodeSource(std::unique_ptr<DecodePort> port) : port_(std::move(port)) {}
    bool step(TrackFeed& feed) override;

private:
    std::unique_ptr<DecodePort> port_;
    bool inputDone_ = false;
    bool ended_ = false;
};

// Opens the platform decoder over a span, decoding from startMs; null when the bytes hold no audio track it
// can decode.
using DecoderOpener = std::function<std::unique_ptr<DecodePort>(const TrackSpan& span, int64_t startMs)>;

// The track at `span` from startMs: an AIFF through its own reader when the bytes are one, else the platform
// decoder; null when neither reads it. Each open reads from the span's start again, through its own duplicate of
// the descriptor (the duplicates share one file position: open the next only once the last is done with).
std::unique_ptr<TrackSource> openTrack(const TrackSpan& span, int64_t startMs, const DecoderOpener& openDecoder);

// Whoever hears the feed: hands it the feed's frames up to its target (TrackFeed::deliver into a sound engine);
// true once the target is reached.
class TrackListener {
public:
    virtual ~TrackListener() = default;
    virtual bool hear(TrackFeed& feed) = 0;
};

class TrackPump {
public:
    TrackPump(int64_t startMs, std::unique_ptr<TrackSource> source) : feed_(startMs), source_(std::move(source)) {}

    // Everything up to endMs into the range, or to the exact end of video frame n at fps (FrameClock's
    // samplesThrough). False once the track ran out first (or never told its rate).
    bool deliverMs(int64_t endMs, TrackListener& listener);
    bool deliverFrame(int64_t n, int fps, TrackListener& listener);

    bool ended() const { return ended_; }
    const TrackFeed& feed() const { return feed_; }

private:
    bool rateKnown();
    bool hearUntilTarget(TrackListener& listener);
    void step();

    TrackFeed feed_;
    std::unique_ptr<TrackSource> source_;
    bool ended_ = false;
};

}  // namespace geode::audio::ingest
