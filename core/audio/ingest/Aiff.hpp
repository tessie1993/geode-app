#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// The AIFF reader (#653): the one source MediaExtractor cannot open, so the export decodes it itself.
//
// Before this the Kotlin AiffPcm walked the chunks, read COMM's 80-bit float rate, and converted each
// sample to s16 in a per-byte loop on the export thread. Now Kotlin only reads the file's bytes from the
// content resolver and hands them in, in whatever chunk sizes it likes; this walks the header and turns
// the sound data into interleaved s16 for the file ring (FileFeed) and the AAC transcoder.
//
// The conversion is the old one, kept on purpose: 8-bit (signed in AIFF) moves up a byte; 16-bit is
// as written; 24- and 32-bit keep their top 16 bits, truncated with no dither (the export's sound engine
// and the AAC encoder hear 16 bits either way). Big endian for AIFF and AIFC "NONE", little endian for
// AIFC "sowt"; any other compression, or a depth other than 8/16/24/32, is refused, as is SSND before COMM.
//
// Bytes are buffered only up to the next whole frame: the caller writes a chunk, reads until read() says
// it wants more, and writes the next. Chunks it skips (artwork, comments) are counted off, never held.
// One thread, the caller's.
namespace geode::audio::ingest {

// COMM's sample rate: an 80-bit IEEE extended float (big endian, 10 bytes), as a whole number of hertz.
// The fraction is dropped and the sign ignored; 0 when the value is zero or out of the 64-bit mantissa's
// range.
int aiffExtendedRate(const uint8_t bytes[10]);

class AiffReader {
public:
    enum class State { kHeader = 0, kReady = 1, kRefused = -1 };

    // Takes the next `count` bytes of the file. Returns the state after them.
    State write(const uint8_t* bytes, size_t count);

    // Up to `capacity` s16 samples (whole frames only) from the bytes written so far. Returns the samples
    // written to `out`, 0 when it needs more bytes, -1 once COMM's frame count has been read, the file was
    // refused, or `capacity` is under one frame (which no read could fill).
    int read(int16_t* out, int capacity);

    State state() const { return state_; }
    int channels() const { return channels_; }
    int sampleRate() const { return rate_; }
    // framesRead / totalFrames in [0, 1]; 0 when the total is unknown.
    float progress() const;

private:
    enum class Step { kForm, kChunkHead, kComm, kSsndHead, kSkip, kSamples };

    bool parse();
    size_t available() const { return buf_.size() - head_; }

    std::vector<uint8_t> buf_;
    size_t head_ = 0;
    State state_ = State::kHeader;
    Step step_ = Step::kForm;
    Step afterSkip_ = Step::kChunkHead;
    uint64_t skip_ = 0;
    uint32_t chunkSize_ = 0;
    int channels_ = 0;
    int bits_ = 0;
    int rate_ = 0;
    int64_t totalFrames_ = 0;
    int64_t framesRead_ = 0;
    bool littleEndian_ = false;
};

}  // namespace geode::audio::ingest
