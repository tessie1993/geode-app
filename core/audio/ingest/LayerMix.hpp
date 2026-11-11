#pragma once

#include <cstdint>
#include <vector>

#include "audio/ingest/PcmRing.hpp"

// A second source layered over a reader's own ring: the mic over the player's tap, so the visuals hear
// the music and the room at once while each source keeps its own ring, writer and rate.
//
// The mix is at the reader, not in a ring: a ring has one writer and one rate per block, the two sources
// run on two clocks, and the player's ring is read behind the sink (up to the sample the speaker plays)
// while the mic is heard as it arrives. So each renderer keeps one LayerMix beside its cursor: the layer's
// frames wait in a short queue, are resampled (linear) to the rate of the block they join and added to it.
//
// The primary's samples are the clock. While its blocks arrive, the layer only adds to them, so the
// engine's sample count stays the song's. Once the layer has run kAloneSeconds with no primary block
// (the player paused or stopped), it is handed out on its own, at the primary's last rate so the engine
// is not rebuilt, and with no queue between the mic and the picture.
//
// The queue is the jitter buffer between two bursty sources: the layer joins once kPrimeSeconds wait in
// it, starts over after an underrun, and drops its oldest audio above kMaxSeconds (clock drift, a stall).
//
// One thread (the reader's). The layer ring must outlive this.
namespace geode::audio::ingest {

class LayerMix {
public:
    static constexpr double kPrimeSeconds = 0.10;
    static constexpr double kMaxSeconds = 0.25;
    static constexpr double kAloneSeconds = 0.30;

    explicit LayerMix(const PcmRing& layer);

    // A primary block of `frames` interleaved frames at rateHz, with the layer added: the block itself
    // when the layer has nothing to add, else a copy that holds until the next call.
    const float* mixed(const float* interleaved, int frames, int channels, int rateHz);

    struct Alone {
        const float* interleaved = nullptr;
        int frames = 0;
        int channels = 0;
        int sampleRateHz = 0;
    };

    // Once per feed, after the primary's blocks: the layer on its own while no primary flows, else nothing.
    Alone takeAlone();

    // The layer is the only thing heard: it was handed out alone since the last primary block and its
    // stream has not ended (a new epoch on its ring ends it).
    bool heardAlone() const { return heardAlone_; }

private:
    void pull();
    void clear();
    int64_t waiting() const { return static_cast<int64_t>(queue_.size()) / channels_; }
    // Up to maxFrames frames at outRate_ from the queue, added onto or stored into dest.
    int render(float* dest, int maxFrames, int destChannels, bool add);
    void trim();

    Cursor cursor_;
    const int channels_;
    std::vector<float> queue_;  // the layer's frames not yet heard, interleaved at channels_, at rate_
    std::vector<float> block_;  // the last mixed or alone block
    double phase_ = 0.0;        // where the next output frame reads, in queue frames
    int rate_ = 0;              // the layer's rate; 0 until it writes
    int outRate_ = 0;           // the primary's last rate; the layer's own until there is one
    int64_t sincePrimary_ = 0;  // layer frames pulled since the last primary block
    bool alone_ = true;
    bool primed_ = false;
    bool heardAlone_ = false;
};

}  // namespace geode::audio::ingest
