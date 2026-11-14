#pragma once

#include "sound/interp/SpeakerSync.hpp"

// The rings the app keeps, one per audio source, and how each is made (geode_pcm_ring_shared,
// geode_pcm_ring_create_kind). The player's and the mic's live for the process, like the playout clock,
// so a writer, a renderer or a stage that still holds one after its session ends never reaches a freed
// ring. The output mix's is one per capture (a wallpaper or a dream each run their own), freed by it.
namespace geode::audio::ingest {

enum RingKind : int {
    kPlayerRing = 0,     // the tap, or a capture of another app in its place
    kMicRing = 1,        // layered over the player's (LayerMix)
    kOutputMixRing = 2,  // the wallpaper's and dream's Visualizer snapshots (SnapshotStitch)
};
inline constexpr int kRingKinds = 3;

struct RingSpec {
    int capacityFrames;
    int channels;
    int maxWriteFrames;  // 0: PcmRing's default runway, a quarter of the ring
    bool snapshots;      // the writes are a capture's snapshots, stitched onto the capture clock
    bool perProcess;     // one for the process, never freed; else one per owner, freed by it
};

inline constexpr RingSpec kRingSpecs[kRingKinds] = {
    // Sized so a reader at the speaker is not overrun (sound/interp/SpeakerSync).
    {sound::interp::kPlayerRingFrames, 2, sound::interp::kPlayerRingRunway, false, true},
    {1 << 15, 1, 0, false, true},
    // Its runway (a quarter) is the longest gap between captures the stitch fills: 170 ms at 48 kHz.
    {1 << 15, 2, 0, true, false},
};

inline const RingSpec* ringSpec(int kind) { return kind >= 0 && kind < kRingKinds ? &kRingSpecs[kind] : nullptr; }

}  // namespace geode::audio::ingest
