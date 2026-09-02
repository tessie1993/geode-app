#pragma once
#include <oboe/Oboe.h>

#include <atomic>
#include <memory>
#include <string>

#include "audio/player/Mixer.hpp"

namespace geode::audio::player {

// The Oboe output stream: stereo float at the device rate, exclusive when the device grants it.
// Every control call runs on the engine thread; the callbacks come from Oboe's own threads.
class Output : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    explicit Output(Mixer& mixer) : mixer_(mixer) {}
    ~Output() override { close(); }
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;

    bool open(std::string& error);
    void close();
    bool start();
    bool pause();
    bool isOpen() const { return stream_ != nullptr; }
    bool running() const { return running_.load(std::memory_order_acquire); }
    bool disconnected() const { return disconnected_.load(std::memory_order_acquire); }
    int sampleRate() const { return sampleRate_; }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) override;
    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

private:
    bool openWith(oboe::SharingMode mode);
    bool settle(oboe::StreamState leaving, oboe::StreamState wanted);

    Mixer& mixer_;
    std::shared_ptr<oboe::AudioStream> stream_;
    int sampleRate_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<bool> disconnected_{false};
};

}  // namespace geode::audio::player
