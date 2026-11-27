#pragma once
#include <atomic>
#include <cstdint>

namespace geode::sound::player {

// What the player tells the sound engines that the tap cannot hear (#460).
//
// Track boundary: a gapless or crossfaded next track reaches the ring with no silence, so the 2 s silence
// rule never fires. Media3 drains the sink's processors at every stream change, so the tap's flush begins
// a ring epoch on the new track's first sample and the analysis starts over there (AnalysisThread). The
// player also names the boundary, with the new play's seed, but only once the speaker plays it, a sink's
// lead later: the analysis takes it as that same join when it just started over (else, with no epoch at
// the join, it is what starts the song over: SoundEngine::trackBoundary), and the frame fires its section
// change. The seed travels with it so whatever reads the boundary knows which play it starts; the
// director already takes the same seed through director::setGlobal.
//
// Fade gain: the tap sits before the player's volume (docs/AUDIO_CHAIN.md), so the frame never hears a
// pause fade or a crossfade. The player hands the gain over as a number and the frame carries it as
// fadeGain, a source a reaction route can follow.
//
// Player is source: whether the player writes the app's ring, or another app's audio has taken it (the
// capture glue's source switch). The mic never takes the ring (audio/ingest/LayerMix): a renderer that
// hears only the mic, the player stopped, reads this as false for itself (Renderer::setLayerAlone). A pause holds the song's tempo, key and sections, so while the player
// is the source the engines' 2 s silence re-warm is off and only a boundary or a seek (the ring's new
// epoch) starts fresh; for the mic and capture, which have no player to name their boundaries, the silence
// rule stays, and fadeGain reads 1 (SoundEngine::setPlayerSource).
//
// Process-wide and latched like the macro bank and the director: written on the main thread, read once per
// frame on each GL thread. An export renderer reads none of them (Renderer::beginFrame): a render has its
// own track, no player fades, and counts as the player. The flags are the app ring's: a renderer fed another
// ring (the dream's output mix, audio/OutputMixCapture) hears no player at all (heardAsPlayer).

struct Boundary {
    uint64_t epoch = 0;  // 0 before the first boundary; each boundary adds one
    uint64_t seed = 0;   // the play the boundary starts
};

namespace detail {
inline std::atomic<uint64_t> gEpoch{0};
inline std::atomic<uint64_t> gSeed{0};
inline std::atomic<float> gFadeGain{1.0f};
inline std::atomic<bool> gPlayerIsSource{true};  // the player owns the ring until a capture takes it
}  // namespace detail

// A new track (or the same one again) begins a new play with `seed`. Returns the boundary's epoch.
inline uint64_t trackBoundary(uint64_t seed) {
    detail::gSeed.store(seed, std::memory_order_relaxed);
    // Release after the seed, so a reader that sees the new epoch also sees its seed.
    return detail::gEpoch.fetch_add(1, std::memory_order_release) + 1;
}

inline Boundary boundary() {
    Boundary b;
    b.epoch = detail::gEpoch.load(std::memory_order_acquire);
    b.seed = detail::gSeed.load(std::memory_order_relaxed);
    return b;
}

// The player's fade gain as it pushes it; SoundEngine::setFadeGain clamps it into 0..1.
inline void setFadeGain(float gain) { detail::gFadeGain.store(gain, std::memory_order_relaxed); }
inline float fadeGain() { return detail::gFadeGain.load(std::memory_order_relaxed); }

// Whether a live renderer's engine hears the player: only on the player's own ring (the one its sink plays,
// so a playout clock is attached), while the player writes it, and not while the renderer hears its layer
// (the mic) alone. Else the engine's source is the mic or another app: silence re-warms, no fade, no boundary.
inline bool heardAsPlayer(bool playersRing, bool playerWrites, bool layerAlone) {
    return playersRing && playerWrites && !layerAlone;
}

// Release and acquire: a reader that sees the flag also sees the ring epoch begun before it was set.
inline void setPlayerIsSource(bool on) { detail::gPlayerIsSource.store(on, std::memory_order_release); }
inline bool playerIsSource() { return detail::gPlayerIsSource.load(std::memory_order_acquire); }

}  // namespace geode::sound::player
