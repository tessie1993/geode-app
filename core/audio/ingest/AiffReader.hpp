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
namespace geode::audio::ingest {

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
    // Null when the bytes are not an AIFF this reads: not FORM/AIFF or AIFC, a compression other than NONE
    // or sowt, no SSND chunk, no channels or rate, a sample size other than 8, 16, 24 or 32 bits, or a
    // header cut short. The source is dropped (closed) then.
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
    bool readInt16(int16_t& v);
    bool readInt32(int32_t& v);

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
