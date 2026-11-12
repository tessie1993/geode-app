#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace geode::output::sink {

// One encode-and-mux loop for every file or stream the app writes (core/output): a video encoder fed through
// its input surface, an audio encoder fed PCM, and a muxer. It owns the parts every writer needs and gets
// wrong in its own way when each has a copy: the encoder ladder (try each size until a codec accepts one),
// the drain, holding encoded samples until every track's format is known and the muxer has started (MP4
// names its tracks before the first sample), codec-config buffers, the end of stream, and the stop on any
// codec or muxer error.
//
// It decides nothing about time. A sample's PTS is whoever stamped its input: the frame clock that stamps
// the surface (Record's core/output/Recorder through viz/frame/RecordTap) and the PCM source (Host::takePcm).
// The sink never rewrites one, so a track's PTS go into the muxer in the order its encoder put them out.
//
// The codecs are behind a small seam (Encoder, Muxer, Codecs below): MediaCodec and MediaMuxer on a phone
// (output/android/MediaCodecSink.cpp), fakes on the host (core/tests/encode_sink_test.cpp). No Android
// header here. One thread drives a sink: step() until done(), then finish().
//
// Who uses which hook:
// - Record (output/RecordSession): Codecs + Muxer as below, Host on the recorder's clock. Built.
// - Cast: the same sink with a Muxer whose writeSample hands each sample out one call at a time, for the
//   shell's fragmented-MP4 muxer to poll; AMediaMuxer has no fragmented output (MPEG_4, WEBM, THREE_GPP, and
//   from API 34 HEIF and OGG), so the cast's container stays the shell's until there is a native one.
// - Export: the video side as Record's (its rungs from output/EncoderLadder), the audio track already
//   encoded (the transcoder's scratch file), so it comes in as an EncodedSource interleaved by PTS instead
//   of an audio Encoder.
// - Loop render: video only; its two PTS rules (FrameClock::ptsNs exact, LoopSeam::ptsNs the truncated step)
//   are the frame stamping's, before the surface, so the sink is unchanged for it.

// Per-buffer flags, the values MediaCodec uses (AMEDIACODEC_BUFFER_FLAG_*).
inline constexpr uint32_t kFlagKeyFrame = 1;
inline constexpr uint32_t kFlagCodecConfig = 2;
inline constexpr uint32_t kFlagEndOfStream = 4;

// One sample: `size` bytes from the pointer it comes with. There is no offset: whoever hands a sample over
// points at its first byte, so no layer has to agree with another on where a codec's buffer starts.
struct BufferInfo {
    int32_t size = 0;
    int64_t ptsUs = 0;
    uint32_t flags = 0;
};

inline constexpr char kMimeAvc[] = "video/avc";
inline constexpr char kMimeAac[] = "audio/mp4a-latm";
// MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface: the encoder reads its input surface.
inline constexpr int kColorFormatSurface = 0x7F000789;
// MediaCodecInfo.CodecProfileLevel.AACObjectLC.
inline constexpr int kAacProfileLc = 2;

// What a video encoder is configured with. Every key is set.
struct VideoFormat {
    const char* mime = kMimeAvc;
    int width = 0;
    int height = 0;
    int colorFormat = kColorFormatSurface;
    int bitRate = 0;
    int frameRate = 0;
    int iFrameIntervalSec = 1;
};

// What an audio encoder is configured with. Every key is set; the PCM fed to it is s16 interleaved.
struct AudioFormat {
    const char* mime = kMimeAac;
    int sampleRateHz = 0;
    int channels = 0;
    int aacProfile = kAacProfileLc;
    int bitRate = 0;
};

// An encoder's output format, handed from the encoder to the muxer untouched.
class TrackFormat {
public:
    virtual ~TrackFormat() = default;
};

// What one non-blocking output dequeue gave.
enum class Out {
    None,           // nothing yet (or the obsolete buffers-changed notice)
    Sample,         // a buffer: *index and *info are set
    FormatChanged,  // outputFormat() is new
    Error,
};

// A codec in encoder mode. Every call is non-blocking. The destructor releases it.
class Encoder {
public:
    // dequeueInput's answers besides an index.
    static constexpr int kNoInput = -1;
    static constexpr int kInputError = -2;

    virtual ~Encoder() = default;
    virtual bool configure(const VideoFormat& format) = 0;
    virtual bool configure(const AudioFormat& format) = 0;
    // After configure, before start: the encoder reads a surface (inputWindow) instead of buffers.
    virtual bool useSurfaceInput() = 0;
    // The surface's ANativeWindow on a phone, owned by the encoder; null without one.
    virtual void* inputWindow() const = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    // A surface's input has ended.
    virtual bool endSurfaceInput() = 0;
    // An input buffer's index, kNoInput or kInputError.
    virtual int dequeueInput() = 0;
    // That buffer's bytes and capacity; null when the codec gives none.
    virtual uint8_t* inputBuffer(int index, size_t* capacity) = 0;
    virtual bool queueInput(int index, size_t bytes, int64_t ptsUs, uint32_t flags) = 0;
    virtual Out dequeueOutput(int* index, BufferInfo* info) = 0;
    // The sample's first byte, and in *capacity how many bytes the codec's buffer holds from there; null when
    // the codec gives none.
    virtual const uint8_t* outputBuffer(int index, size_t* capacity) = 0;
    virtual bool releaseOutput(int index) = 0;
    virtual std::unique_ptr<TrackFormat> outputFormat() = 0;
};

class Muxer {
public:
    virtual ~Muxer() = default;
    // The track's index, or < 0 when refused.
    virtual int addTrack(const TrackFormat& format) = 0;
    virtual bool start() = 0;
    // data is the sample's first byte, info.size bytes long, and only valid during the call.
    virtual bool writeSample(int track, const uint8_t* data, const BufferInfo& info) = 0;
    virtual bool stop() = 0;
};

// Makes the codecs and the muxer: MediaCodec by MIME type and an MP4 muxer on one file on a phone.
class Codecs {
public:
    virtual ~Codecs() = default;
    virtual std::unique_ptr<Encoder> createEncoder(const char* mime) = 0;
    virtual std::unique_ptr<Muxer> createMuxer() = 0;
};

// Export's later audio track: samples already encoded, read in PTS order (not used by Record).
class EncodedSource {
public:
    virtual ~EncodedSource() = default;
    // The next sample's info without taking it; false at the end.
    virtual bool peek(BufferInfo* info) = 0;
    // Takes it into out (info.size bytes).
    virtual bool read(uint8_t* out) = 0;
    virtual std::unique_ptr<TrackFormat> format() = 0;
};

// What the sink asks its owner, on the sink's thread.
class Host {
public:
    virtual ~Host() = default;
    // The owner wants the stream to end (Record: the recorder stopped).
    virtual bool ending() = 0;
    // Returns once nothing draws into the input surface any more (Record: the stage's tee closed, or a wait
    // ran out). Called once on the way to the end and again after a failure, so it must be idempotent.
    virtual void inputClosing() = 0;
    // Up to capacityFrames of interleaved s16 into out, stamped *ptsUs (left as it was when nothing came);
    // 0 when nothing is due, and once ending, 0 means the audio is complete.
    virtual int takePcm(int16_t* out, int capacityFrames, int64_t* ptsUs) = 0;
    // One video sample came out of the encoder.
    virtual void videoEncoded() = 0;
    // The muxer took this many bytes more.
    virtual void wrote(int64_t bytes) = 0;
    // A codec or the muxer failed: the stream ends now.
    virtual void failed() = 0;
};

// Video picks the rung, attempt by attempt (0 first): false past the last one.
using Rungs = std::function<bool(int attempt, VideoFormat* out)>;

class EncodeSink {
public:
    // The most PCM frames one audio input buffer is given.
    static constexpr int kPcmFrames = 4096;
    // The most encoded bytes kept while the muxer waits for both formats. They arrive within the first few
    // buffers, so this is seconds of the highest bit rate; a codec that never names its format fails the
    // stream here instead of growing until the process dies.
    static constexpr size_t kHeldLimitBytes = size_t{32} << 20;

    EncodeSink(Codecs& codecs, Muxer& muxer, Host& host) : codecs_(codecs), muxer_(muxer), host_(host) {}
    ~EncodeSink();
    EncodeSink(const EncodeSink&) = delete;
    EncodeSink& operator=(const EncodeSink&) = delete;

    // The ladder: each rung's encoder is made, configured and given its input surface; the first that takes
    // all three is kept, a refused one is released and the next is tried. Then the audio encoder, and both
    // start (before anything may draw into the surface). False when no rung or the audio would not start.
    bool open(const Rungs& rungs, const AudioFormat& audio);

    // The rung the video encoder took.
    const VideoFormat& video() const { return chosen_; }
    void* inputWindow() const { return video_ ? video_->inputWindow() : nullptr; }

    // One pass: the end once the host is ending, one audio input buffer, one output buffer from each
    // encoder. True when anything moved; the caller waits a little when nothing did.
    bool step();
    // Both streams ended, or a failure.
    bool done() const { return failed_ || (ended_[kVideo] && ended_[kAudio]); }
    bool failed() const { return failed_; }

    // Finishes the file (only one that holds a video sample: an MP4 with none is not a video) and stops
    // both encoders. True when the file is a finished video.
    bool finish();

    // What the muxer was handed, for the owner's records.
    int64_t samplesWritten() const { return written_; }

private:
    static constexpr int kVideo = 0;
    static constexpr int kAudio = 1;

    // A sample that came out before the muxer started: info.size bytes at heldBytes_[at].
    struct Held {
        int track = 0;
        size_t at = 0;
        BufferInfo info;
    };

    // Each: -1 failed, 0 nothing moved, 1 something did.
    int feedAudio(bool ending);
    int drain(int track);
    bool write(int track, const uint8_t* data, const BufferInfo& info);
    bool startMuxerWhenReady();
    bool fail();

    Codecs& codecs_;
    Muxer& muxer_;
    Host& host_;
    std::unique_ptr<Encoder> video_;
    std::unique_ptr<Encoder> audio_;
    VideoFormat chosen_;
    int frameBytes_ = 0;
    std::unique_ptr<TrackFormat> formats_[2];
    int tracks_[2] = {-1, -1};
    bool muxing_ = false;
    bool wroteVideo_ = false;
    bool ended_[2] = {false, false};
    bool ending_ = false;
    bool failed_ = false;
    bool audioEnded_ = false;
    int audioInput_ = Encoder::kNoInput;
    int64_t ptsUs_ = 0;
    int64_t written_ = 0;
    std::vector<Held> held_;
    std::vector<uint8_t> heldBytes_;  // every held sample back to back: one block, not one per sample
    std::vector<int16_t> pcm_;  // only when a codec's input buffer is not aligned for s16
};

}  // namespace geode::output::sink
