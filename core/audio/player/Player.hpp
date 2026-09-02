#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio/player/Decoder.hpp"
#include "audio/player/Deck.hpp"
#include "audio/player/Mixer.hpp"
#include "audio/player/Output.hpp"
#include "audio/player/Resampler.hpp"

namespace geode::audio::player {

enum class PlayerState : int { Idle = 0, Buffering, Ready, Ended, Error };

// The engine thread owns the decoders, the decks and the stream; the public calls queue commands for it
// and read atomics it publishes every loop, so nothing here waits on the audio thread.
class Player {
public:
    Player();
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    void open(int fd, int64_t offset, int64_t length, int64_t token);
    void setNext(int fd, int64_t offset, int64_t length, int64_t token);
    void play();
    void pause();
    void stop();
    void seek(int64_t positionUs);
    void setCrossfade(int durationMs, int curve);
    void setDsp(geode_dsp* dsp) { mixer_.setDsp(dsp, output_.running()); }
    void setVolume(float volume) { mixer_.setVolume(volume); }

    PlayerState state() const { return state_.load(std::memory_order_acquire); }
    bool playWhenReady() const { return playWhenReady_.load(std::memory_order_acquire); }
    int64_t positionUs() const { return positionUs_.load(std::memory_order_acquire); }
    int64_t durationUs() const { return durationUs_.load(std::memory_order_acquire); }
    int64_t currentToken() const { return token_.load(std::memory_order_acquire); }
    int outputSampleRate() const { return outRate_.load(std::memory_order_acquire); }
    std::string lastError() const;
    size_t readTap(float* stereo, size_t frames) { return mixer_.readTap(stereo, frames); }

private:
    struct Command {
        enum class Kind { Open, SetNext, Play, Pause, Stop, Seek, Crossfade };
        Kind kind;
        int fd = -1;
        int64_t offset = 0;
        int64_t length = 0;
        int64_t token = 0;
        int64_t positionUs = 0;
        int durationMs = 0;
        int curve = 0;
    };

    struct Track;

    void enqueue(Command command);
    void run();
    void handle(const Command& command);
    void doOpen(const Command& command);
    void doSetNext(const Command& command);
    void doSeek(int64_t positionUs);
    void doStop();
    std::unique_ptr<Track> loadTrack(int fd, int64_t offset, int64_t length, int64_t token);
    bool ensureOutput();
    void reopenOutput();
    void reconcile();
    void pump();
    void fill(Track& track);
    Deck* newDeck(Track& track, int64_t startFrame);
    void postCommand(DeckCommand command);
    void updateState();
    void noteError(const std::string& message);
    void fail(const std::string& message);
    int64_t crossfadeFrames() const;

    Mixer mixer_;
    Output output_{mixer_};
    std::mutex lock_;
    std::condition_variable wake_;
    std::deque<Command> queue_;
    bool quit_ = false;
    std::unique_ptr<Track> current_;
    std::unique_ptr<Track> next_;
    std::vector<std::unique_ptr<Deck>> decks_;
    int crossfadeMs_ = 0;
    int curve_ = 0;
    std::atomic<PlayerState> state_{PlayerState::Idle};
    std::atomic<bool> playWhenReady_{false};
    std::atomic<int64_t> positionUs_{0};
    std::atomic<int64_t> durationUs_{0};
    std::atomic<int64_t> token_{-1};
    std::atomic<int> outRate_{0};
    mutable std::mutex errorLock_;
    std::string lastError_;
    std::thread thread_;
};

}  // namespace geode::audio::player
