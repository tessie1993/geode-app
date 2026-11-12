#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// An AIFF or AIFF-C (NONE or sowt) file read as 16-bit PCM, for the two readers MediaExtractor cannot
// serve: the export's track audio (TrackPump) and the soundtrack transcoder (export/AudioTranscoder). The
// header walk and the sample rule are those the transcoder has always read AIFF with: 8-bit
// samples are shifted up a byte, 16-, 24- and 32-bit samples keep their top two bytes (no dither, no
// rounding), whatever the byte order.
//
// The bytes come from a ByteSource, read once, front to back: the shell hands over a file descriptor
// (FdSource), a host test a buffer (MemorySource). One thread at a time.
//
// The header is parsed in one place, AiffHeaderScan, for both this reader and the player's extractor, which
// reads the file through Media3 and so feeds the scan the bytes it asks for.
namespace geode::audio::ingest {

// What an AIFF's COMM and SSND chunks say.
struct AiffHeader {
    // How the player takes the samples; kNotPlayed for a width or byte order its sink has no encoding for.
    enum Pcm : int { kNotPlayed = 0, kS16Le = 1, kS16Be = 2, kS24Be = 3, kS32Be = 4 };

    int channels = 0;
    int sampleRate = 0;         // the 80-bit extended rate rounded to the nearest hertz
    int bits = 0;
    bool littleEndian = false;  // AIFF-C "sowt"
    int64_t frames = 0;         // COMM's count
    int64_t dataStart = 0;      // the file offset of the first sample byte
    int64_t dataBytes = 0;      // the sample bytes SSND's size leaves after its offset; the file may hold fewer

    Pcm pcm() const;
    // 0 for a sample width that is not whole bytes.
    int bytesPerFrame() const { return bits > 0 && bits % 8 == 0 ? channels * (bits / 8) : 0; }
};

// The walk over an AIFF's chunks up to its first sample, fed by whoever holds the file: pass over skip()
// bytes, read read() bytes, hand them to step(), and repeat while it answers kMore. The scan never asks for
// more than kMaxRead bytes at once nor trusts a size in the file with more than a skip: a file cut short
// fails in the feeder's read, and every size is taken as the unsigned 32 bits the format gives it.
//
// Refused (kInvalid): not FORM/AIFF or AIFC, a COMM chunk under 18 bytes, no channels, no rate or no sample
// size, an AIFF-C compression other than NONE or sowt, SSND before COMM, a step that is not the bytes asked
// for. A second COMM replaces the first.
class AiffHeaderScan {
public:
    enum Status : int { kMore = 0, kDone = 1, kInvalid = -1 };
    // The most step() takes at once: COMM's 18 bytes and AIFF-C's compression type.
    static constexpr int kMaxRead = 22;
    // The most one request passes over; a longer gap is asked for in pieces, each a step of no bytes.
    static constexpr int64_t kMaxSkip = int64_t{1} << 30;

    int64_t skip() const { return owed_ < kMaxSkip ? owed_ : kMaxSkip; }
    int read() const { return owed_ <= kMaxSkip ? want_ : 0; }
    // `n` must be read(); bytes may be null when it is 0. After kDone the file stands at header().dataStart.
    Status step(const uint8_t* bytes, int n);
    bool done() const { return phase_ == Phase::kDone; }
    // Complete only once done().
    const AiffHeader& header() const { return header_; }

    // The rate in COMM's ten bytes, rounded to the nearest hertz; 0 for none or one past what an int holds.
    static int extendedRate(const uint8_t* ten);

private:
    enum class Phase { kForm, kChunkHead, kComm, kSsnd, kDataGap, kDone, kInvalid };

    Status fail();
    void ask(Phase phase, int64_t skip, int read);
    Status onChunkHead(const uint8_t* b);
    Status onComm(const uint8_t* b, int n);

    Phase phase_ = Phase::kForm;
    int64_t owed_ = 0;      // bytes still to pass over before the phase's read
    int want_ = 12;         // the phase's read
    int64_t at_ = 0;        // the file offset after everything passed over and read so far
    int64_t commRest_ = 0;  // what follows COMM's parsed fields, its pad byte included
    int64_t ssndSize_ = 0;
    bool aifc_ = false;
    bool commSeen_ = false;
    AiffHeader header_;
};

// Where a byte offset into the samples falls in time, and back, for the player's seek map. Both count whole
// frames and neither overflows on a long file or a far seek.
// The start time, in microseconds, of the frame `bytes` into the samples lies in; 0 for a bad layout.
int64_t aiffTimeUs(int64_t bytes, int bytesPerFrame, int sampleRate);
// The byte offset of the frame playing at timeUs, held to the whole frames in dataBytes.
int64_t aiffSeekOffset(int64_t timeUs, int bytesPerFrame, int sampleRate, int64_t dataBytes);

// The player's extractor between the header and the end of the samples, which start at dataStart and run
// dataBytes; it hands Media3 at most kAiffMaxSampleBytes at once.
inline constexpr int kAiffMaxSampleBytes = 32 * 1024;
// The bytes to pass over at `position` to reach the first sample; 0 on or past it.
int64_t aiffGapBytes(int64_t position, int64_t dataStart);
// The most to read at `position`; 0 once the samples are done.
int aiffReadBytes(int64_t position, int64_t dataStart, int64_t dataBytes);
// Of `pending` bytes held, those in whole frames, which go out now; a part frame waits.
int aiffWholeBytes(int pending, int bytesPerFrame);
// The start time of the `pending` bytes held, which end at `position`.
int64_t aiffPendingTimeUs(int64_t position, int64_t dataStart, int pending, int bytesPerFrame, int sampleRate);

class ByteSource {
public:
    virtual ~ByteSource() = default;
    // Up to n bytes into dst: the count, 0 at the end, < 0 on an error (treated as the end).
    virtual long read(uint8_t* dst, size_t n) = 0;
};

// A file descriptor the source owns and closes. With length >= 0 it starts startOffset bytes on from the
// descriptor's position and stops after length bytes (an AssetFileDescriptor's span); with length < 0 it
// reads from where the descriptor is to its end, as ParcelFileDescriptor's stream does.
class FdSource final : public ByteSource {
public:
    FdSource(int fd, int64_t startOffset, int64_t length);
    ~FdSource() override;
    FdSource(const FdSource&) = delete;
    FdSource& operator=(const FdSource&) = delete;
    long read(uint8_t* dst, size_t n) override;

private:
    int fd_;
    int64_t remaining_;  // < 0: no limit
};

// A copy of a byte span, for host tests.
class MemorySource final : public ByteSource {
public:
    explicit MemorySource(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
    long read(uint8_t* dst, size_t n) override;

private:
    std::vector<uint8_t> bytes_;
    size_t at_ = 0;
};

class AiffReader {
public:
    // Null when the bytes are not an AIFF this reads: a header AiffHeaderScan refuses or one cut short (no
    // SSND chunk), or a sample size other than 8, 16, 24 or 32 bits. The source is dropped (closed) then.
    // The samples are read to the end of the source or COMM's frame count, whatever SSND's size says.
    static std::unique_ptr<AiffReader> open(std::unique_ptr<ByteSource> source);

    int channels() const { return channels_; }
    int sampleRate() const { return sampleRate_; }
    int bitsPerSample() const { return bits_; }
    bool littleEndian() const { return littleEndian_; }
    // The COMM chunk's frame count; 0 reads to the end of the file.
    int64_t totalFrames() const { return totalFrames_; }
    int64_t durationUs() const { return sampleRate_ > 0 ? totalFrames_ * 1'000'000 / sampleRate_ : 0; }
    // Frames read so far over the total, 0..1; 0 with no total.
    float progress() const;

    // Up to `capacity` interleaved samples (whole frames while the file has them), as 16-bit; returns the
    // samples, or -1 at the end.
    int readS16(int16_t* out, int capacity);

    // The rule above, for one sample of bits / 8 bytes at `raw`.
    static int16_t toS16(const uint8_t* raw, int bits, bool littleEndian);

private:
    explicit AiffReader(std::unique_ptr<ByteSource> source);
    bool fill();
    bool readFully(uint8_t* dst, size_t n);
    void skip(int64_t n);

    std::unique_ptr<ByteSource> source_;
    std::vector<uint8_t> buffer_;  // the read-ahead the Kotlin BufferedInputStream gave (64 KiB)
    size_t at_ = 0;
    size_t end_ = 0;
    std::vector<uint8_t> raw_;
    int channels_ = 0;
    int sampleRate_ = 0;
    int bits_ = 0;
    int64_t totalFrames_ = 0;
    int64_t framesRead_ = 0;
    bool littleEndian_ = false;
};

}  // namespace geode::audio::ingest
