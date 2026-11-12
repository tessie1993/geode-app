#include "audio/ingest/AiffReader.hpp"

#include <cerrno>
#include <cstring>
#include <limits>
#include <unistd.h>

namespace geode::audio::ingest {

namespace {
constexpr size_t kReadAhead = size_t{1} << 16;
// COMM's fields: channels, frames, sample size and the ten-byte rate.
constexpr int kCommBytes = 18;
constexpr int64_t kMicros = 1'000'000;

uint32_t be32(const uint8_t* b) {
    return (uint32_t{b[0]} << 24) | (uint32_t{b[1]} << 16) | (uint32_t{b[2]} << 8) | uint32_t{b[3]};
}

// A signed 16-bit field.
int be16(const uint8_t* b) { return static_cast<int16_t>(static_cast<uint16_t>((b[0] << 8) | b[1])); }

bool tagIs(const uint8_t* b, const char* tag) { return std::memcmp(b, tag, 4) == 0; }
}  // namespace

AiffHeader::Pcm AiffHeader::pcm() const {
    if (bits == 16) return littleEndian ? kS16Le : kS16Be;
    if (littleEndian) return kNotPlayed;
    return bits == 24 ? kS24Be : (bits == 32 ? kS32Be : kNotPlayed);
}

int AiffHeaderScan::extendedRate(const uint8_t* ten) {
    const int exponent = ((ten[0] & 0x7F) << 8) | ten[1];
    uint64_t mantissa = 0;
    for (int i = 0; i < 8; ++i) mantissa = (mantissa << 8) | ten[2 + i];
    if (mantissa == 0) return 0;
    // The mantissa is a 64-bit whole number scaled by 2^(exponent - 16383 - 63): its low `below` bits are the
    // fraction, and the top one of those rounds the rate up.
    const int below = 63 - (exponent - 16383);
    if (below < 0 || below > 64) return 0;
    const uint64_t whole = below == 64 ? 0 : mantissa >> below;
    const uint64_t half = below == 0 ? 0 : (mantissa >> (below - 1)) & 1;
    const uint64_t rate = whole + half;
    return rate > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()) ? 0 : static_cast<int>(rate);
}

AiffHeaderScan::Status AiffHeaderScan::fail() {
    ask(Phase::kInvalid, 0, 0);
    return kInvalid;
}

void AiffHeaderScan::ask(Phase phase, int64_t skip, int read) {
    phase_ = phase;
    owed_ = skip;
    want_ = read;
}

AiffHeaderScan::Status AiffHeaderScan::onChunkHead(const uint8_t* b) {
    const int64_t size = be32(b + 4);
    const int64_t padded = size + (size & 1);
    if (tagIs(b, "COMM")) {
        if (size < kCommBytes) return fail();
        // The compression type is AIFF-C's alone; a plain AIFF's longer COMM carries nothing this reads.
        const int fields = aifc_ && size >= kMaxRead ? kMaxRead : kCommBytes;
        commRest_ = padded - fields;
        ask(Phase::kComm, 0, fields);
    } else if (tagIs(b, "SSND")) {
        if (!commSeen_) return fail();
        ssndSize_ = size;
        ask(Phase::kSsnd, 0, 8);
    } else {
        ask(Phase::kChunkHead, padded, 8);
    }
    return kMore;
}

AiffHeaderScan::Status AiffHeaderScan::onComm(const uint8_t* b, int n) {
    AiffHeader h;
    h.channels = be16(b);
    h.frames = be32(b + 2);
    h.bits = be16(b + 6);
    h.sampleRate = extendedRate(b + 8);
    if (n >= kMaxRead) {
        if (tagIs(b + kCommBytes, "sowt")) {
            h.littleEndian = true;
        } else if (!tagIs(b + kCommBytes, "NONE")) {
            return fail();
        }
    }
    if (h.channels <= 0 || h.sampleRate <= 0 || h.bits <= 0) return fail();
    header_ = h;
    commSeen_ = true;
    ask(Phase::kChunkHead, commRest_, 8);
    return kMore;
}

AiffHeaderScan::Status AiffHeaderScan::step(const uint8_t* bytes, int n) {
    if (phase_ == Phase::kDone) return kDone;
    if (phase_ == Phase::kInvalid) return kInvalid;
    if (n != read() || (n > 0 && !bytes)) return fail();
    const int64_t passed = skip();
    owed_ -= passed;
    at_ += passed + n;
    if (owed_ > 0) return kMore;  // a long gap, passed over in pieces
    switch (phase_) {
        case Phase::kForm:
            if (!tagIs(bytes, "FORM")) return fail();
            aifc_ = tagIs(bytes + 8, "AIFC");
            if (!aifc_ && !tagIs(bytes + 8, "AIFF")) return fail();
            ask(Phase::kChunkHead, 0, 8);
            return kMore;
        case Phase::kChunkHead:
            return onChunkHead(bytes);
        case Phase::kComm:
            return onComm(bytes, n);
        case Phase::kSsnd: {
            // SSND's size covers its offset and block size words, then `offset` bytes of padding, then the samples.
            const int64_t offset = be32(bytes);
            const int64_t body = ssndSize_ > 8 ? ssndSize_ - 8 : 0;
            header_.dataStart = at_ + offset;
            header_.dataBytes = body > offset ? body - offset : 0;
            ask(offset > 0 ? Phase::kDataGap : Phase::kDone, offset, 0);
            return offset > 0 ? kMore : kDone;
        }
        case Phase::kDataGap:
            ask(Phase::kDone, 0, 0);
            return kDone;
        case Phase::kDone:
        case Phase::kInvalid:
            break;
    }
    return fail();
}

int64_t aiffTimeUs(int64_t bytes, int bytesPerFrame, int sampleRate) {
    if (bytes <= 0 || bytesPerFrame <= 0 || sampleRate <= 0) return 0;
    const int64_t frames = bytes / bytesPerFrame;
    const int64_t seconds = frames / sampleRate;
    if (seconds > std::numeric_limits<int64_t>::max() / kMicros - 1) return std::numeric_limits<int64_t>::max();
    // Whole seconds and the rest apart, so a long file's frame count times a million never overflows.
    return seconds * kMicros + frames % sampleRate * kMicros / sampleRate;
}

int64_t aiffSeekOffset(int64_t timeUs, int bytesPerFrame, int sampleRate, int64_t dataBytes) {
    if (timeUs <= 0 || bytesPerFrame <= 0 || sampleRate <= 0 || dataBytes <= 0) return 0;
    const int64_t frames = dataBytes / bytesPerFrame;
    const int64_t seconds = timeUs / kMicros;
    // Past the last frame, also when the seconds times the rate would not fit.
    if (seconds > frames / sampleRate) return frames * bytesPerFrame;
    const int64_t frame = seconds * sampleRate + timeUs % kMicros * sampleRate / kMicros;
    return (frame < frames ? frame : frames) * bytesPerFrame;
}

FdSource::FdSource(int fd, int64_t startOffset, int64_t length) : fd_(fd), remaining_(length < 0 ? -1 : length) {
    if (fd_ < 0 || length < 0 || startOffset <= 0) return;
    // An asset descriptor's start offset is from the file's start, wherever a shared descriptor was left.
    if (::lseek(fd_, static_cast<off_t>(startOffset), SEEK_SET) >= 0) return;
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

std::unique_ptr<AiffReader> AiffReader::open(std::unique_ptr<ByteSource> source) {
    if (!source) return nullptr;
    std::unique_ptr<AiffReader> r(new AiffReader(std::move(source)));
    AiffHeaderScan scan;
    for (;;) {
        // A gap that runs past the end of the source shows in the read that follows it.
        r->skip(scan.skip());
        uint8_t bytes[AiffHeaderScan::kMaxRead] = {};
        const int want = scan.read();
        if (want > 0 && !r->readFully(bytes, static_cast<size_t>(want))) return nullptr;
        const AiffHeaderScan::Status status = scan.step(bytes, want);
        if (status == AiffHeaderScan::kInvalid) return nullptr;
        if (status == AiffHeaderScan::kDone) break;
    }
    const AiffHeader& header = scan.header();
    if (header.bits != 8 && header.bits != 16 && header.bits != 24 && header.bits != 32) return nullptr;
    r->channels_ = header.channels;
    r->sampleRate_ = header.sampleRate;
    r->bits_ = header.bits;
    r->totalFrames_ = header.frames;
    r->littleEndian_ = header.littleEndian;
    return r;
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
