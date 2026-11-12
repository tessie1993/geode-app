#include "output/EncodeSink.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace geode::output::sink {

EncodeSink::~EncodeSink() = default;

bool EncodeSink::open(const Rungs& rungs, const AudioFormat& audio) {
    if (!rungs || audio.channels <= 0) return false;
    VideoFormat ask;
    for (int attempt = 0; rungs(attempt, &ask); ++attempt) {
        std::unique_ptr<Encoder> codec = codecs_.createEncoder(ask.mime);
        // A refused rung's codec is released here, before the next one is made.
        if (codec && codec->configure(ask) && codec->useSurfaceInput()) {
            video_ = std::move(codec);
            chosen_ = ask;
            break;
        }
    }
    if (!video_) return false;
    audio_ = codecs_.createEncoder(audio.mime);
    if (!audio_ || !audio_->configure(audio)) return false;
    // Both run before the surface may be drawn into.
    if (!video_->start() || !audio_->start()) return false;
    frameBytes_ = audio.channels * static_cast<int>(sizeof(int16_t));
    return true;
}

bool EncodeSink::fail() {
    if (!failed_) {
        failed_ = true;
        host_.failed();
        // The surface may still be drawn into: its end waits for the drawing to stop, as the normal end does.
        host_.inputClosing();
    }
    return false;
}

bool EncodeSink::step() {
    if (done() || !video_ || !audio_) return false;
    const bool ending = host_.ending();
    if (ending && !ending_) {
        ending_ = true;
        // The encoder's surface must not be drawn into after its end of stream.
        host_.inputClosing();
        if (!video_->endSurfaceInput()) return fail();
    }
    const int fed = feedAudio(ending);
    if (fed < 0) return fail();
    const int video = drain(kVideo);
    if (video < 0) return fail();
    const int sound = drain(kAudio);
    if (sound < 0) return fail();
    return fed > 0 || video > 0 || sound > 0;
}

int EncodeSink::feedAudio(bool ending) {
    if (audioEnded_) return 0;
    if (audioInput_ < 0) {
        audioInput_ = audio_->dequeueInput();
        if (audioInput_ == Encoder::kInputError) return -1;
    }
    if (audioInput_ < 0) return 0;
    size_t capacity = 0;
    uint8_t* buffer = audio_->inputBuffer(audioInput_, &capacity);
    if (!buffer) return 0;
    const int room = static_cast<int>(std::min<size_t>(kPcmFrames, capacity / static_cast<size_t>(frameBytes_)));
    // The PCM goes straight into the codec's buffer; through a copy only if that buffer is not s16-aligned.
    const bool aligned = reinterpret_cast<uintptr_t>(buffer) % alignof(int16_t) == 0;
    int16_t* out = nullptr;
    if (aligned) {
        out = reinterpret_cast<int16_t*>(buffer);
    } else {
        pcm_.resize(static_cast<size_t>(kPcmFrames) * static_cast<size_t>(frameBytes_) / sizeof(int16_t));
        out = pcm_.data();
    }
    const int frames = host_.takePcm(out, room, &ptsUs_);
    if (frames <= 0 && !ending) return 0;
    const size_t bytes = static_cast<size_t>(std::max(frames, 0)) * static_cast<size_t>(frameBytes_);
    if (!aligned && bytes > 0) std::memcpy(buffer, out, bytes);
    // After the end the host hands over what is left; its first empty answer is the end of the audio.
    audioEnded_ = frames <= 0;
    const uint32_t flags = audioEnded_ ? kFlagEndOfStream : 0;
    if (!audio_->queueInput(audioInput_, bytes, ptsUs_, flags)) return -1;
    audioInput_ = Encoder::kNoInput;
    return 1;
}

int EncodeSink::drain(int track) {
    if (ended_[track]) return 0;
    Encoder& codec = track == kVideo ? *video_ : *audio_;
    int index = -1;
    BufferInfo info;
    switch (codec.dequeueOutput(&index, &info)) {
        case Out::None:
            return 0;
        case Out::Error:
            return -1;
        case Out::FormatChanged:
            // A change after the muxer started keeps the track it has: MP4 names its tracks once.
            formats_[track] = codec.outputFormat();
            if (!formats_[track]) return -1;
            return startMuxerWhenReady() ? 1 : -1;
        case Out::Sample:
            break;
    }
    const uint8_t* data = codec.outputBuffer(index);
    // A codec-config buffer (SPS/PPS, the AAC header) is already in the output format the muxer was given.
    const bool config = (info.flags & kFlagCodecConfig) != 0;
    if (data && info.size > 0 && !config) {
        if (track == kVideo) host_.videoEncoded();
        if (!write(track, data, info)) return -1;
    }
    if ((info.flags & kFlagEndOfStream) != 0) ended_[track] = true;
    return codec.releaseOutput(index) ? 1 : -1;
}

bool EncodeSink::write(int track, const uint8_t* data, const BufferInfo& info) {
    if (!muxing_) {
        // Out before every format is known: kept as a copy, since the codec takes its buffer back.
        Held held;
        held.track = track;
        held.bytes.assign(data + info.offset, data + info.offset + info.size);
        held.info = {0, info.size, info.ptsUs, info.flags};
        held_.push_back(std::move(held));
        return true;
    }
    if (!muxer_.writeSample(tracks_[track], data, info)) return false;
    ++written_;
    wroteVideo_ = wroteVideo_ || track == kVideo;
    host_.wrote(info.size);
    return true;
}

bool EncodeSink::startMuxerWhenReady() {
    if (!formats_[kVideo] || !formats_[kAudio] || muxing_) return true;
    // Video first, then audio.
    tracks_[kVideo] = muxer_.addTrack(*formats_[kVideo]);
    if (tracks_[kVideo] < 0) return false;
    tracks_[kAudio] = muxer_.addTrack(*formats_[kAudio]);
    if (tracks_[kAudio] < 0) return false;
    if (!muxer_.start()) return false;
    muxing_ = true;
    // What waited goes in as it came out, both tracks interleaved in arrival order.
    for (const Held& held : held_) {
        if (!write(held.track, held.bytes.data(), held.info)) return false;
    }
    std::vector<Held>().swap(held_);
    return true;
}

bool EncodeSink::finish() {
    // A muxer with no sample cannot finish its file, and would not be a video anyway.
    const bool finished = muxing_ && wroteVideo_ && muxer_.stop();
    if (video_) video_->stop();
    if (audio_) audio_->stop();
    return finished && !failed_;
}

}  // namespace geode::output::sink
