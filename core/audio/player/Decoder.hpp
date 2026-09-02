#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace geode::audio::player {

// AMediaExtractor + AMediaCodec on one file descriptor, delivering interleaved stereo float PCM.
// Every call runs on the engine thread; reads may return 0 frames while the codec is still working.
class Decoder {
public:
    static std::unique_ptr<Decoder> open(int fd, int64_t offset, int64_t length, std::string& error);
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    int sampleRate() const { return sampleRate_; }
    int channels() const { return channels_; }
    int64_t durationUs() const { return durationUs_; }
    size_t readStereo(float* out, size_t maxFrames);
    bool seek(int64_t positionUs);
    bool ended() const { return outputDone_ && pendingFrames_ == 0; }
    bool failed() const { return !error_.empty(); }
    const std::string& error() const { return error_; }
    int64_t nextPtsUs() const { return nextPtsUs_; }

private:
    struct Native;

    Decoder(int fd, std::unique_ptr<Native> native, int sampleRate, int channels, int64_t durationUs);
    bool feedInput();
    bool fetchOutput();
    void applyOutputFormat();
    void releasePending();
    size_t frameBytes() const;
    size_t skipPending(size_t frames);
    size_t convertPending(float* out, size_t maxFrames);

    std::unique_ptr<Native> native_;
    int fd_;
    int sampleRate_;
    int channels_;
    int64_t durationUs_;
    bool floatSamples_ = false;
    bool inputDone_ = false;
    bool outputDone_ = false;
    ssize_t pendingIndex_ = -1;
    const uint8_t* pendingData_ = nullptr;
    size_t pendingFrames_ = 0;
    int64_t nextPtsUs_ = 0;
    int64_t skipUntilUs_ = 0;
    std::string error_;
};

}  // namespace geode::audio::player
