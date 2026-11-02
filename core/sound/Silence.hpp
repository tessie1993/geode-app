#pragma once

#include <cmath>
#include <cstdint>

// Whether a resting surface (the live wallpaper, the dream) is hearing anything: the rule behind
// their "rest when silent".
//
// Silence is judged on the samples, not on whether a player is running, because a playing track
// can be silent (a gap, a fade) and a capture can be fed zeros. A poll hands in the newest samples
// written since the last one (none at all when nothing was written); any above kPeak counts as
// sound. Once nothing has been heard for kRestAfterMs the surface may rest, and the next audible
// poll wakes it.
//
// Not the renderer's SoundEngine on purpose: a resting surface draws no frames, so the engine that
// rides the render loop hears nothing exactly when this has to listen. The caller polls on its own
// clock (a few times a second) and keeps one timestamp, so there is no state here to own or free.
namespace geode::sound::silence {

// About -42 dBFS: under the quietest music, above the output mix's 8-bit floor of zero.
inline constexpr float kPeak = 0.008f;

// Long enough that a breakdown or a track change does not stop the picture mid-song.
inline constexpr int64_t kRestAfterMs = 6000;

// True when any of frames samples in any of the planar channels is above kPeak.
inline bool audible(const float* const* channels, int channelCount, int frames) {
    if (!channels || channelCount <= 0 || frames <= 0) return false;
    for (int c = 0; c < channelCount; ++c) {
        const float* ch = channels[c];
        if (!ch) continue;
        for (int i = 0; i < frames; ++i) {
            if (std::fabs(ch[i]) > kPeak) return true;
        }
    }
    return false;
}

// The time sound was last heard, given this poll: now when it is audible, else unchanged.
inline int64_t heardAt(const float* const* channels, int channelCount, int frames, int64_t nowMs,
                       int64_t heardAtMs) {
    return audible(channels, channelCount, frames) ? nowMs : heardAtMs;
}

// Whether the surface may rest: nothing heard for longer than kRestAfterMs.
inline bool resting(int64_t nowMs, int64_t heardAtMs) { return nowMs - heardAtMs > kRestAfterMs; }

}  // namespace geode::sound::silence
