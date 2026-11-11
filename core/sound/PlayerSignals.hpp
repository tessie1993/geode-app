#pragma once
#include <atomic>
#include <cstdint>

namespace geode::sound::player {

// What the player tells the sound engines that the tap cannot hear (#460).
//
// Track boundary: a gapless or crossfaded next track reaches the ring with no flush and no silence, so the
// tap's epoch does not move and the 2 s silence rule never fires; the engines would carry the last song's
// tempo, key and sections into the new one. The player names the boundary instead, with the new play's
// seed, and every live renderer's sound engine forgets its musical memory at its next frame
// (SoundEngine::trackBoundary). The seed travels with it so whatever reads the boundary knows which play
// it starts; the director already takes the same seed through director::setGlobal.
//
// Fade gain: the tap sits before the player's volume (docs/AUDIO_CHAIN.md), so the frame never hears a
// pause fade or a crossfade. The player hands the gain over as a number and the frame carries it as
// fadeGain, a source a reaction route can follow.
//
// Player is source: whether the player writes the app's ring, or the mic or another app's audio has taken it
// (the capture glue's source switch). A pause holds the song's tempo, key and sections, so while the player
// is the source the engines' 2 s silence re-warm is off and only a boundary or a seek (the ring's new
// epoch) starts fresh; for the mic and capture, which have no player to name their boundaries, the silence
// rule stays, and fadeGain reads 1 (SoundEngine::setPlayerSource).
//
// Process-wide and latched like the macro bank and the director: written on the main thread, read once per
// frame on each GL thread. An export renderer reads none of them (Renderer::beginFrame): a render has its
// own track, no player fades, and counts as the player. One flag for the process: a wallpaper hearing the
// output mix (wallpaper/OutputMixCapture, its own ring) still reads the app ring's source.
//
// Group C's native ring (core/audio/ingest) carries an epoch with every write; once it lands, the ring can
// stamp the first sample of the new track with the epoch trackBoundary() returns, so the reset lands on
// that sample instead of on the next frame.

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

inline void setPlayerIsSource(bool on) { detail::gPlayerIsSource.store(on, std::memory_order_relaxed); }
inline bool playerIsSource() { return detail::gPlayerIsSource.load(std::memory_order_relaxed); }

}  // namespace geode::sound::player
