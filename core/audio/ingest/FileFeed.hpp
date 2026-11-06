#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "audio/ingest/Cursor.hpp"
#include "audio/ingest/Ring.hpp"

// The export's file ring (#490, #478): a decoded track handed to the export renderer frame-exact.
//
// Export equals live only if the export's sound engine hears the same audio the screen's did, sample for
// sample, and the same project always renders the same video. Before this the Kotlin TrackAudio converted
// the decoder's int16 to float in a per-sample loop, kept a pending chunk by hand, and cut each frame's
// audio at a whole-millisecond end ((n + 1) * 1000 / fps ms, then ms * rate / 1000 samples): two truncations
// that drift against the true frame clock and differ between frame rates. Here:
//
// - The decoder's bytes go straight into an ingest Ring (Ring.hpp) in the encoding MediaCodec gave them,
//   so the conversion is the ingest's one conversion, the same the player's tap uses.
// - Samples before the range start are dropped on the way in, from the chunk's presentation time (the
//   extractor seeks to the keyframe at or before the start), or from the running position when the source
//   has no timestamps (AIFF, read from the top).
// - Frame n of the video owns ring positions [floor(n * rate / fps), floor((n + 1) * rate / fps)): exact
//   integer accounting from the range's first sample, so no frame rate drifts, and the same input gives the
//   same per-frame samples whatever chunk sizes the decoder produced. The rate is the first written one;
//   a file that changes rate part-way keeps being counted at it (the engine still hears each block at its
//   own rate, through the cursor).
// - The reader is an ingest Cursor, so the export hears the ingest's stereo downmix (5.1 and 7.1 files
//   included) exactly as every live renderer does. The renderer's writeAudio hands projectM its raw PCM
//   from the same samples (#486), so an export's MilkDrop hears the real signal too (#360); there is no
//   second raw feed here.
//
// One thread, the export's: it is both the ring's writer and its only reader. The caller decodes while
// needs() says the next frame is not fully written yet, then delivers; the ring therefore never holds more
// than one frame plus one decoder chunk, far under its capacity.
namespace geode::audio::ingest {

class FileFeed {
public:
    // Frames the ring holds: a 192 kHz frame at 24 fps (8000) plus the largest decoder chunk, many times
    // over. Up to eight channels (7.1) are kept; more fold into the downmix's unknown-layout rule.
    static constexpr int kCapacityFrames = 1 << 17;
    static constexpr int kMaxChannels = 8;

    // What one frame hands the renderer: interleaved stereo, `frames` frames at `sampleRate`, in play
    // order; called once per format block. The pointer is valid only during the call.
    struct Sink {
        virtual ~Sink() = default;
        virtual void audio(const float* stereo, int frames, int sampleRate) = 0;
    };

    explicit FileFeed(int64_t startUs);

    // Takes one decoder chunk: `byteCount` bytes of interleaved PCM in `encoding` (android.media.AudioFormat
    // ENCODING_*), at `sampleRate` with `channels`, starting at presentation time `ptsUs` (negative: right
    // after the previous chunk). Returns the frames kept after the pre-start drop, or -1 when the format or
    // encoding is unusable (the chunk is ignored).
    int64_t write(const void* bytes, size_t byteCount, int encoding, int sampleRate, int channels, int64_t ptsUs);

    // True while video frame `frameIndex` at `fps` still needs samples that are not written yet.
    bool needs(int64_t frameIndex, int fps) const;

    // Hands `sink` every sample of frames up to and including `frameIndex` not delivered yet. Returns the frames delivered (fewer than the frame owns once the file has ended).
    int deliver(int64_t frameIndex, int fps, Sink& sink);

    // The ring position just past frame `frameIndex` at `fps`: floor((frameIndex + 1) * rate / fps). 0
    // before the first write (the rate is not known yet).
    int64_t endOf(int64_t frameIndex, int fps) const;

    int sampleRate() const { return clockRate_; }
    int64_t written() const { return ring_->written(); }
    int64_t delivered() const { return cursor_->position(); }
    // Frames the cursor ever skipped; stays 0 unless the ring overran, which the caller's protocol prevents.
    int64_t dropped() const { return dropped_; }

private:
    int64_t startUs_;
    std::unique_ptr<Ring> ring_;
    std::unique_ptr<Cursor> cursor_;
    std::vector<float> stereo_;
    int clockRate_ = 0;    // the rate frames are counted at: the first written one
    int rate_ = 0;         // the current format, as last given to the ring
    int channels_ = 0;
    int64_t sourceAt_ = 0; // source frame (at rate_) just past the last chunk, for chunks without a time
    int64_t dropped_ = 0;
};

}  // namespace geode::audio::ingest

// The C ABI's handle (core/api/geode_api.h, geode_file_feed_*): the feed itself, so geode_viz_feed_file can
// reach it from the viz API.
struct geode_file_feed {
    explicit geode_file_feed(int64_t startUs) : feed(startUs) {}
    geode::audio::ingest::FileFeed feed;
};
