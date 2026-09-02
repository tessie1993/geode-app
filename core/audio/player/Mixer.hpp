#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "api/geode_api.h"
#include "audio/player/Deck.hpp"
#include "audio/player/SpscRing.hpp"

namespace geode::audio::player {

struct DeckCommand {
    enum class Op : uint8_t { SetCurrent, SetNext, ClearNext };
    Op op;
    Deck* deck;
};

// Drains the current deck, joins the next one gaplessly or through a crossfade, then applies volume, the
// DSP chain and the analysis tap. render() runs on the audio thread and never blocks or allocates; the
// engine thread reaches it through the command ring and takes retired decks back from the retired ring.
class Mixer {
public:
    static constexpr int kChannels = 2;
    static constexpr size_t kMaxRenderFrames = 4096;

    Mixer();

    // Engine thread.
    bool post(DeckCommand command) { return commands_.push(&command, 1) == 1; }
    Deck* takeRetired();
    // Only while no audio callback can run (stream paused, stopped or closed).
    void drainCommands() { applyCommands(); }
    void setCrossfade(int64_t frames, int curve);
    // Waits for one callback to pass when the stream runs, so the previous chain is never in use afterwards.
    void setDsp(geode_dsp* dsp, bool streamRunning);
    void setVolume(float volume) { volume_.store(volume, std::memory_order_relaxed); }

    // Audio thread: frames <= kMaxRenderFrames.
    void render(float* stereo, size_t frames);

    // Any thread.
    Deck* currentDeck() const { return current_.load(std::memory_order_acquire); }
    bool ended() const { return ended_.load(std::memory_order_acquire); }
    size_t readTap(float* stereo, size_t frames) { return tap_.pop(stereo, frames * kChannels) / kChannels; }

private:
    void applyCommands();
    void retire(Deck* deck);
    void beginTransition(Deck* from, Deck* to);
    static size_t pull(Deck& deck, float* stereo, size_t frames);
    void fade(float* out, const float* incoming, size_t frames, int64_t firstFrame, int64_t fadeStart,
              int64_t fadeFrames) const;

    SpscRing<DeckCommand> commands_{64};
    SpscRing<Deck*> retired_{64};
    SpscRing<float> tap_;
    std::vector<float> scratch_;
    std::atomic<Deck*> current_{nullptr};
    std::atomic<Deck*> next_{nullptr};
    std::atomic<bool> ended_{false};
    std::atomic<int64_t> crossfadeFrames_{0};
    std::atomic<int> curve_{0};
    std::atomic<geode_dsp*> dsp_{nullptr};
    std::atomic<float> volume_{1.0f};
    std::atomic<uint64_t> callbacks_{0};
};

}  // namespace geode::audio::player
