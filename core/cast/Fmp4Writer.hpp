#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace geode::cast {

// The cast's fragmented MP4 (ISO BMFF, CMAF-shaped), written natively from the encoders' own buffers: an init
// segment (ftyp + moov, one H.264 track then one AAC track, mvex/trex) once both codec configs are known, then
// one moof + mdat per fragment. Each fragment opens on a video keyframe and closes at the first keyframe at least
// nine tenths of fragmentUs after it (frame PTS are whole microseconds, so a second of frames can be a few short),
// so a fragment is one HLS segment. Every traf is self-contained: tfhd default-base-is-moof,
// a version 1 tfdt, and a trun with each sample's duration, size and flags and the data offset from the moof.
// core/cast/LiveSegments cuts this exactly as it cut the shell's muxer before (its rewrite keeps the tfdt).
//
// Input is what an Android encoder puts out: H.264 in Annex B (start codes; the codec config holds the SPS and
// PPS), AAC as raw access units with the AudioSpecificConfig as its codec config. NAL units are rewritten with
// 4-byte lengths; SPS, PPS and access unit delimiters go to the avcC only (avc1). Video before the first
// keyframe is dropped; samples before both configs are known are held, up to kMaxHeld.
//
// Time: a sample's decode time is its PTS from the first sample's, in the track's timescale (90 kHz video, the
// sample rate for audio), never going back: a PTS at or before the previous sample's is moved to just after it
// (one tick for video, one AAC frame for audio). A sample's duration is the next one's decode time minus its
// own; a fragment's last audio sample lasts one AAC frame, its last video sample until the keyframe that cut it.
// No B-frames are expected (the encoder is asked for none), so composition time is decode time.
//
// Pure C++, no I/O, one thread: whoever calls it. Tested by core/tests/fmp4_writer_test.cpp.
class Fmp4Writer {
public:
    using Sink = std::function<void(const uint8_t* data, size_t size)>;

    struct Tracks {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t audioRateHz = 0;
        uint16_t channels = 0;
        uint32_t audioBitRate = 0;
    };

    static constexpr uint32_t kVideoTimescale = 90'000;
    static constexpr uint32_t kAacFrameSamples = 1024;
    static constexpr size_t kMaxHeld = 512;
    // The most samples of one track an open fragment holds (about 40 s of either): past it, a fragment with no
    // keyframe in sight is written out anyway, and audio with no picture is dropped.
    static constexpr size_t kMaxQueued = 2048;
    // Sample flags (ISO 14496-12 8.8.3.1): a sync sample, and one that depends on others.
    static constexpr uint32_t kSyncFlags = 0x02000000;
    static constexpr uint32_t kNonSyncFlags = 0x01010000;

    Fmp4Writer(const Tracks& tracks, int64_t fragmentUs, Sink sink);

    // The H.264 codec config (Annex B SPS and PPS). False when it holds no SPS or no PPS.
    bool videoConfig(const uint8_t* data, size_t size);
    // The AAC AudioSpecificConfig. False when it is empty or longer than an esds can say in one length byte.
    bool audioConfig(const uint8_t* data, size_t size);

    void video(const uint8_t* data, size_t size, int64_t ptsUs, bool key);
    void audio(const uint8_t* data, size_t size, int64_t ptsUs);

    // Writes the open fragment out (at the end of a stream).
    void flush();

    bool started() const { return started_; }
    int64_t fragments() const { return sequence_; }
    int64_t dropped() const { return dropped_; }

    // ---- The pieces, for the test. ----
    // The NAL units of an Annex B buffer (no start codes); one unit when there is no start code at all.
    static std::vector<std::pair<const uint8_t*, size_t>> nalUnits(const uint8_t* data, size_t size);
    // An Annex B access unit as 4-byte length-prefixed NAL units, parameter sets and delimiters left out.
    static std::vector<uint8_t> lengthPrefixed(const uint8_t* data, size_t size);

private:
    struct Sample {
        std::vector<uint8_t> bytes;
        int64_t ptsUs = 0;
        bool key = false;
        bool video = false;
    };
    struct Timed {
        std::vector<uint8_t> bytes;
        int64_t decode = 0;
        bool key = false;
    };

    void start();
    void accept(Sample&& sample);
    int64_t decodeTime(int64_t ptsUs, uint32_t timescale, int64_t step, int64_t& last) const;
    void writeFragment(size_t videoCount);
    std::vector<uint8_t> initSegment() const;

    Tracks tracks_;
    int64_t fragmentUs_;
    Sink sink_;
    std::vector<uint8_t> sps_;
    std::vector<std::vector<uint8_t>> pps_;
    std::vector<uint8_t> asc_;
    bool haveVideoConfig_ = false;
    bool haveAudioConfig_ = false;
    bool started_ = false;
    bool sawKey_ = false;
    std::vector<Sample> held_;
    bool haveOrigin_ = false;
    int64_t originUs_ = 0;
    int64_t lastVideo_ = -1;
    int64_t lastAudio_ = -1;
    int64_t openedUs_ = 0;
    int64_t lastVideoDuration_ = 0;
    std::vector<Timed> videoQ_;
    std::vector<Timed> audioQ_;
    int64_t sequence_ = 0;
    int64_t dropped_ = 0;
};

}  // namespace geode::cast
