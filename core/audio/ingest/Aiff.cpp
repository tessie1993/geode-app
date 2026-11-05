#include "audio/ingest/Aiff.hpp"

#include <algorithm>
#include <cstring>

namespace geode::audio::ingest {

namespace {

uint32_t be16(const uint8_t* p) { return (static_cast<uint32_t>(p[0]) << 8) | p[1]; }
uint32_t be32(const uint8_t* p) { return (be16(p) << 16) | be16(p + 2); }
bool is(const uint8_t* p, const char* tag) { return std::memcmp(p, tag, 4) == 0; }

// COMM's fixed part: channels (2), frames (4), bits (2), rate (10).
constexpr size_t kCommFixed = 18;
constexpr size_t kCompressionId = 4;

}  // namespace

int aiffExtendedRate(const uint8_t bytes[10]) {
    const int exponent = static_cast<int>(be16(bytes) & 0x7FFF);
    const uint64_t mantissaHigh = be32(bytes + 2);
    if (exponent == 0 && mantissaHigh == 0) return 0;
    const int shift = 63 - (exponent - 16383);
    if (shift < 0 || shift > 63) return 0;
    // Only the top 32 bits of the mantissa count: no rate needs more.
    return static_cast<int>(static_cast<uint32_t>((mantissaHigh << 32) >> shift));
}

AiffReader::State AiffReader::write(const uint8_t* bytes, size_t count) {
    if (state_ == State::kRefused || !bytes || count == 0) return state_;
    if (head_ > 0) {
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(head_));
        head_ = 0;
    }
    buf_.insert(buf_.end(), bytes, bytes + count);
    while (state_ == State::kHeader && parse()) {
    }
    return state_;
}

// One header step on the buffered bytes. False when it needs more bytes (or the walk is over).
bool AiffReader::parse() {
    const uint8_t* p = buf_.data() + head_;
    switch (step_) {
        case Step::kForm:
            if (available() < 12) return false;
            if (!is(p, "FORM") || !(is(p + 8, "AIFF") || is(p + 8, "AIFC"))) {
                state_ = State::kRefused;
                return false;
            }
            head_ += 12;
            step_ = Step::kChunkHead;
            return true;
        case Step::kChunkHead: {
            if (available() < 8) return false;
            chunkSize_ = be32(p + 4);
            head_ += 8;
            if (is(p, "COMM")) {
                step_ = Step::kComm;
            } else if (is(p, "SSND")) {
                step_ = Step::kSsndHead;
            } else {
                skip_ = static_cast<uint64_t>(chunkSize_) + (chunkSize_ & 1);
                afterSkip_ = Step::kChunkHead;
                step_ = Step::kSkip;
            }
            return true;
        }
        case Step::kComm: {
            const bool compressed = chunkSize_ > kCommFixed;
            const size_t need = kCommFixed + (compressed ? kCompressionId : 0);
            if (available() < need) return false;
            channels_ = static_cast<int16_t>(be16(p));
            totalFrames_ = be32(p + 2);
            bits_ = static_cast<int16_t>(be16(p + 6));
            rate_ = aiffExtendedRate(p + 8);
            if (compressed) {
                if (is(p + kCommFixed, "sowt")) {
                    littleEndian_ = true;
                } else if (!is(p + kCommFixed, "NONE")) {
                    state_ = State::kRefused;
                    return false;
                }
            }
            head_ += need;
            // The rest of the chunk (AIFC's compression name) and its pad byte.
            const uint64_t size = chunkSize_;
            skip_ = (size > need ? size - need : 0) + (size & 1);
            afterSkip_ = Step::kChunkHead;
            step_ = Step::kSkip;
            return true;
        }
        case Step::kSsndHead: {
            if (available() < 8) return false;
            skip_ = be32(p);   // the offset to the first sample frame; block size is unused
            head_ += 8;
            if (channels_ <= 0 || rate_ <= 0 || (bits_ != 8 && bits_ != 16 && bits_ != 24 && bits_ != 32)) {
                state_ = State::kRefused;
                return false;
            }
            afterSkip_ = Step::kSamples;
            step_ = Step::kSkip;
            return true;
        }
        case Step::kSkip: {
            const auto n = static_cast<size_t>(std::min<uint64_t>(skip_, available()));
            head_ += n;
            skip_ -= n;
            if (skip_ > 0) return false;
            step_ = afterSkip_;
            if (step_ == Step::kSamples) state_ = State::kReady;
            return true;
        }
        case Step::kSamples:
            return false;
    }
    return false;
}

int AiffReader::read(int16_t* out, int capacity) {
    if (state_ == State::kRefused) return -1;
    if (state_ != State::kReady || !out || capacity <= 0) return 0;
    int64_t frames = capacity / channels_;
    if (totalFrames_ > 0) {
        const int64_t remaining = totalFrames_ - framesRead_;
        if (remaining <= 0) return -1;
        frames = std::min(frames, remaining);
    }
    const size_t bytesPer = static_cast<size_t>(bits_ / 8);
    const size_t frameBytes = bytesPer * static_cast<size_t>(channels_);
    frames = std::min<int64_t>(frames, static_cast<int64_t>(available() / frameBytes));
    if (frames <= 0) return 0;
    const auto samples = static_cast<size_t>(frames) * static_cast<size_t>(channels_);
    const uint8_t* p = buf_.data() + head_;
    // The top two bytes of each sample (one for 8-bit), wherever the byte order puts them.
    const size_t hi = littleEndian_ ? bytesPer - 1 : 0;
    const size_t lo = littleEndian_ ? bytesPer - 2 : 1;
    for (size_t i = 0; i < samples; ++i, p += bytesPer) {
        const uint32_t top = p[hi];
        const uint32_t next = bytesPer == 1 ? 0 : p[lo];
        out[i] = static_cast<int16_t>(static_cast<uint16_t>((top << 8) | next));
    }
    head_ += samples * bytesPer;
    framesRead_ += frames;
    return static_cast<int>(samples);
}

float AiffReader::progress() const {
    if (totalFrames_ <= 0) return 0.0f;
    return std::clamp(static_cast<float>(framesRead_) / static_cast<float>(totalFrames_), 0.0f, 1.0f);
}

}  // namespace geode::audio::ingest
