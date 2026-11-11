#include "audio/ingest/AiffReader.hpp"

#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace geode::audio::ingest {

namespace {
constexpr size_t kReadAhead = size_t{1} << 16;
}  // namespace

FdSource::FdSource(int fd, int64_t startOffset, int64_t length) : fd_(fd), remaining_(length < 0 ? -1 : length) {
    if (fd_ < 0 || length < 0 || startOffset <= 0) return;
    if (::lseek(fd_, static_cast<off_t>(startOffset), SEEK_CUR) >= 0) return;
    // Not seekable (a pipe): read the lead-in away.
    uint8_t scrap[4096];
    int64_t left = startOffset;
    while (left > 0) {
        const ssize_t n = ::read(fd_, scrap, static_cast<size_t>(left < 4096 ? left : 4096));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        left -= n;
    }
}

FdSource::~FdSource() {
    if (fd_ >= 0) ::close(fd_);
}

long FdSource::read(uint8_t* dst, size_t n) {
    if (fd_ < 0 || remaining_ == 0) return 0;
    if (remaining_ > 0 && static_cast<int64_t>(n) > remaining_) n = static_cast<size_t>(remaining_);
    for (;;) {
        const ssize_t got = ::read(fd_, dst, n);
        if (got < 0 && errno == EINTR) continue;
        if (got > 0 && remaining_ > 0) remaining_ -= got;
        return static_cast<long>(got);
    }
}

long MemorySource::read(uint8_t* dst, size_t n) {
    const size_t left = bytes_.size() - at_;
    const size_t take = n < left ? n : left;
    if (take > 0) std::memcpy(dst, bytes_.data() + at_, take);
    at_ += take;
    return static_cast<long>(take);
}

AiffReader::AiffReader(std::unique_ptr<ByteSource> source) : source_(std::move(source)), buffer_(kReadAhead) {}

bool AiffReader::fill() {
    if (at_ < end_) return true;
    const long n = source_->read(buffer_.data(), buffer_.size());
    at_ = 0;
    end_ = n > 0 ? static_cast<size_t>(n) : 0;
    return end_ > 0;
}

bool AiffReader::readFully(uint8_t* dst, size_t n) {
    while (n > 0) {
        if (!fill()) return false;
        const size_t take = n < end_ - at_ ? n : end_ - at_;
        std::memcpy(dst, buffer_.data() + at_, take);
        at_ += take;
        dst += take;
        n -= take;
    }
    return true;
}

// DataInputStream.skipBytes: as many as there are, never backwards.
void AiffReader::skip(int64_t n) {
    while (n > 0 && fill()) {
        const size_t take = static_cast<int64_t>(end_ - at_) < n ? end_ - at_ : static_cast<size_t>(n);
        at_ += take;
        n -= static_cast<int64_t>(take);
    }
}

bool AiffReader::readInt16(int16_t& v) {
    uint8_t b[2];
    if (!readFully(b, 2)) return false;
    v = static_cast<int16_t>(static_cast<uint16_t>((b[0] << 8) | b[1]));
    return true;
}

bool AiffReader::readInt32(int32_t& v) {
    uint8_t b[4];
    if (!readFully(b, 4)) return false;
    v = static_cast<int32_t>((uint32_t{b[0]} << 24) | (uint32_t{b[1]} << 16) | (uint32_t{b[2]} << 8) | uint32_t{b[3]});
    return true;
}

std::unique_ptr<AiffReader> AiffReader::open(std::unique_ptr<ByteSource> source) {
    if (!source) return nullptr;
    std::unique_ptr<AiffReader> r(new AiffReader(std::move(source)));
    uint8_t tag[4];
    int32_t ignored = 0;
    if (!r->readFully(tag, 4) || std::memcmp(tag, "FORM", 4) != 0) return nullptr;
    if (!r->readInt32(ignored) || !r->readFully(tag, 4)) return nullptr;
    if (std::memcmp(tag, "AIFF", 4) != 0 && std::memcmp(tag, "AIFC", 4) != 0) return nullptr;

    int channels = 0;
    int bits = 0;
    int rate = 0;
    int64_t frames = 0;
    bool littleEndian = false;
    for (;;) {
        int32_t size = 0;
        if (!r->readFully(tag, 4) || !r->readInt32(size)) return nullptr;
        if (std::memcmp(tag, "COMM", 4) == 0) {
            int16_t ch = 0;
            int32_t count = 0;
            int16_t sampleBits = 0;
            if (!r->readInt16(ch) || !r->readInt32(count) || !r->readInt16(sampleBits)) return nullptr;
            channels = ch;
            frames = static_cast<int64_t>(static_cast<uint32_t>(count));
            bits = sampleBits;
            // The 80-bit extended rate: its exponent and the high 32 bits of the mantissa, as the Kotlin read it.
            int16_t exponentBits = 0;
            int32_t mantissaHigh = 0;
            int32_t mantissaLow = 0;
            if (!r->readInt16(exponentBits) || !r->readInt32(mantissaHigh) || !r->readInt32(mantissaLow)) return nullptr;
            const int exponent = exponentBits & 0x7FFF;
            const uint64_t high = static_cast<uint32_t>(mantissaHigh);
            rate = 0;
            if (exponent != 0 || high != 0) {
                const int shift = 63 - (exponent - 16383);
                if (shift >= 0 && shift <= 63) rate = static_cast<int32_t>(static_cast<uint32_t>((high << 32) >> shift));
            }
            int32_t consumed = 18;
            if (size > consumed) {
                if (!r->readFully(tag, 4)) return nullptr;
                consumed += 4;
                if (std::memcmp(tag, "sowt", 4) == 0) {
                    littleEndian = true;
                } else if (std::memcmp(tag, "NONE", 4) != 0) {
                    return nullptr;
                }
                r->skip(static_cast<int64_t>(size) - consumed + (size & 1));
            } else if ((size & 1) == 1) {
                r->skip(1);
            }
        } else if (std::memcmp(tag, "SSND", 4) == 0) {
            int32_t offset = 0;
            if (!r->readInt32(offset) || !r->readInt32(ignored)) return nullptr;
            if (offset > 0) r->skip(offset);
            if (channels <= 0 || rate <= 0 || (bits != 8 && bits != 16 && bits != 24 && bits != 32)) return nullptr;
            r->channels_ = channels;
            r->sampleRate_ = rate;
            r->bits_ = bits;
            r->totalFrames_ = frames;
            r->littleEndian_ = littleEndian;
            return r;
        } else {
            r->skip(static_cast<int64_t>(size) + (size & 1));
        }
    }
}

float AiffReader::progress() const {
    if (totalFrames_ <= 0) return 0.0f;
    const float p = static_cast<float>(framesRead_) / static_cast<float>(totalFrames_);
    return p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p);
}

int16_t AiffReader::toS16(const uint8_t* raw, int bits, bool littleEndian) {
    if (bits == 8) return static_cast<int16_t>(static_cast<uint16_t>(static_cast<int8_t>(raw[0]) * 256));
    if (bits != 16 && bits != 24 && bits != 32) return 0;
    const int bytesPer = bits / 8;
    const int hi = littleEndian ? bytesPer - 1 : 0;
    const int lo = littleEndian ? bytesPer - 2 : 1;
    return static_cast<int16_t>(static_cast<uint16_t>((raw[hi] << 8) | raw[lo]));
}

int AiffReader::readS16(int16_t* out, int capacity) {
    if (!out || capacity <= 0) return -1;
    const int bytesPer = bits_ / 8;
    int64_t maxSamples = capacity - (capacity % channels_);
    if (totalFrames_ > 0) {
        const int64_t remaining = (totalFrames_ - framesRead_) * channels_;
        if (remaining <= 0) return -1;
        if (remaining < maxSamples) maxSamples = remaining;
    }
    const size_t want = static_cast<size_t>(maxSamples) * static_cast<size_t>(bytesPer);
    if (raw_.size() < want) raw_.resize(want);
    size_t got = 0;
    while (got < want && fill()) {
        const size_t take = want - got < end_ - at_ ? want - got : end_ - at_;
        std::memcpy(raw_.data() + got, buffer_.data() + at_, take);
        at_ += take;
        got += take;
    }
    const int samples = static_cast<int>(got / static_cast<size_t>(bytesPer));
    if (samples == 0) return -1;
    for (int i = 0; i < samples; ++i) out[i] = toS16(raw_.data() + static_cast<size_t>(i) * bytesPer, bits_, littleEndian_);
    framesRead_ += samples / channels_;
    return samples;
}

}  // namespace geode::audio::ingest
