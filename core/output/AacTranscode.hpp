#pragma once

// An export's soundtrack, decoded and encoded to AAC-LC: the track (an AIFF through AiffReader, anything else
// through the platform decoder behind DecodePort) cut to a range, folded to at most two channels, brought to the
// encoder's rate (audio/dsp/PcmResampler) and fed to an AAC encoder (the encode sink's Encoder seam). Each
// encoded access unit is appended to the Host's file and listed with its byte offset, size, PTS and flags, the
// table the export's muxer copies the track from.
//
// The rules are the ones the transcoder always had:
// - the encoder runs at outputRateHz, or at audio::aacRateFor(the decoded rate) when that is 0;
// - a decoder seeks to the sync sample before the start; each decoded buffer is trimmed to it
//   (audio::trimS16) and queued from the trimmed buffer's PTS, advancing by the bytes queued;
// - an AIFF skips the frames before the start (audio::samplesBeforeUs) and is stamped by every byte it fed;
// - a range ends after the last decoded buffer at or before start + maxDuration (a decoder) or once the fed
//   audio passes maxDuration (an AIFF); maxDuration 0 runs to the end of the track;
// - kMaxIdleSteps steps in a row without anything moving is a stall.
//
// One thread. step() until it returns false, then read the outcome. Pure: core/tests/aac_transcode_test.cpp
// drives it with a fake decoder and encoder.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio/dsp/PcmResampler.hpp"
#include "audio/ingest/AiffReader.hpp"
#include "audio/ingest/TrackPump.hpp"
#include "output/EncodeSink.hpp"

namespace geode::output::aac {

enum class State { Running = 1, Done = 2, Failed = 3 };

enum class Failure {
    None = 0,
    NoAudio = 1,   // no audio track the platform decodes
    Codec = 2,     // a codec refused to open or failed mid-stream; detail() says which
    Stalled = 3,   // nothing moved for kMaxIdleSteps steps
    NoFormat = 4,  // the encoder ended without naming its output format
    Write = 5,     // the scratch file could not be written (a full disk)
    Unreadable = 6,  // the source's bytes could not be opened or read at all
};

inline constexpr int kMaxIdleSteps = 1000;
inline constexpr int kBitRate = 192'000;
// Samples per AIFF read, less what leaves a part frame.
inline constexpr int kAiffReadSamples = 16384;

// One encoded access unit in the Host's file.
struct Sample {
    int64_t offset = 0;
    int32_t size = 0;
    int64_t ptsUs = 0;
    uint32_t flags = 0;
};

struct Request {
    int64_t startMs = 0;
    int64_t maxDurationMs = 0;  // 0: to the end of the track
    int outputRateHz = 0;       // 0: aacRateFor the decoded rate
};

// The finished track's format as a muxer is handed it whole (EncodeSink's Codecs::trackFormat): the keys
// MediaFormat.createAudioFormat sets (mime, sample-rate, channel-count), the bit rate and, when csdBytes > 0, csd-0.
std::vector<sink::FormatEntry> formatEntries(int sampleRateHz, int channels, int bitRate, const uint8_t* csd,
                                             size_t csdBytes);

class Host {
public:
    virtual ~Host() = default;
    // An AAC encoder, not yet configured; null when the device has none.
    virtual std::unique_ptr<sink::Encoder> createEncoder() = 0;
    // Appends to the scratch file; false when it could not.
    virtual bool write(const uint8_t* data, size_t bytes) = 0;
    // Once the encoder has ended: everything written is in the file; false when it could not be.
    virtual bool finish() = 0;
    // Waits a little: nothing moved and no codec call waited this step.
    virtual void idleWait() = 0;
};

class Transcode {
public:
    // A null reader or port fails the job at once: with NoAudio, or Unreadable when the opener said the source could
    // not be read at all.
    Transcode(const Request& request, std::unique_ptr<audio::ingest::AiffReader> aiff, Host& host);
    Transcode(const Request& request, std::unique_ptr<audio::ingest::DecodePort> port, Host& host,
              bool unreadable = false);
    ~Transcode();
    Transcode(const Transcode&) = delete;
    Transcode& operator=(const Transcode&) = delete;

    // One pass; false once the job is done or failed.
    bool step();

    State state() const { return state_; }
    Failure failure() const { return failure_; }
    const std::string& detail() const { return detail_; }
    // 0..1 through the range, or 0 while its length is unknown.
    float progress() const { return progress_; }

    // The encoded track; complete once state() is Done.
    const std::vector<Sample>& samples() const { return samples_; }
    const sink::TrackFormat* format() const { return format_.get(); }
    int sampleRate() const { return encoderRate_; }
    int channels() const { return channels_; }
    int64_t bytes() const { return outBytes_; }
    // The last access unit's PTS plus one AAC frame at the encoder's rate; 0 for an empty track.
    int64_t durationUs() const;

private:
    void stepAiff(bool* moved);
    void stepDecoder(bool* moved, bool* waited);
    // Converts, folds, trims and resamples one decoded buffer into the carry; false when none of it is kept.
    bool takeChunk(const audio::ingest::DecodedChunk& chunk);
    bool openEncoder(int sourceRate, int sourceChannels);
    int feedEncoder();
    int drainEncoder();
    void resample(const int16_t* in, size_t frames);
    bool fail(Failure failure, const char* detail = nullptr);
    int failStep(Failure failure, const char* detail);

    const Request request_;
    Host& host_;
    std::unique_ptr<audio::ingest::AiffReader> aiff_;
    std::unique_ptr<audio::ingest::DecodePort> port_;
    std::unique_ptr<sink::Encoder> encoder_;
    std::unique_ptr<audio::PcmResampler> resampler_;
    std::unique_ptr<sink::TrackFormat> format_;

    State state_ = State::Running;
    Failure failure_ = Failure::None;
    std::string detail_;
    float progress_ = 0.0f;

    int sourceRate_ = 0;
    int channels_ = 0;
    int encoderRate_ = 0;
    int64_t estimatedUs_ = 0;

    bool sourceDone_ = false;
    bool inputDone_ = false;  // the decoder's end of input is queued
    bool eosSent_ = false;
    bool encoderDone_ = false;
    int idleSteps_ = 0;

    std::vector<int16_t> carry_;
    size_t carryAt_ = 0;  // samples of carry_ already queued
    int64_t carryTimeUs_ = 0;
    int64_t fedBytes_ = 0;  // the AIFF's bytes queued in all
    int64_t outBytes_ = 0;
    std::vector<Sample> samples_;

    std::vector<int16_t> pcm_;
    std::vector<float> floats_;
    std::vector<int16_t> folded_;
    std::vector<int16_t> read_;
};

}  // namespace geode::output::aac
