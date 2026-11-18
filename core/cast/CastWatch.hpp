#pragma once
#include <cstdint>

namespace geode::cast::watch {

// When a cast stream is given up on, and when the cast's own renderer is given back: the rules CastService and
// CastStream polled in Kotlin. A stream gets kStartTimeoutMs to list enough segments for a receiver to start on;
// once it has, a cast ends when no segment was cut for kFrozenMs (the encoder or the renderer stalled: a frozen
// picture on the TV with a muted phone is worse than ending) or when the phone no longer holds the address the
// stream is served from (the link would name nothing). The shell polls every kWaitPollMs while it waits and
// every kPlayPollMs after. The cast's fallback renderer is freed once the stage has led for kFallbackIdleNs:
// it costs scenes and a sound engine. Pure; tested by core/tests/cast_watch_test.cpp.

constexpr int64_t kStartTimeoutMs = 15'000;   // the three segments a receiver starts on, and a slow first encode
constexpr int64_t kFrozenMs = 5'000;          // segments come every second
constexpr int64_t kWaitPollMs = 250;
constexpr int64_t kPlayPollMs = 1'000;
constexpr int64_t kFallbackIdleNs = 30'000'000'000;

enum Verdict : int {
    kWait = 0,           // not ready yet: poll again
    kReady = 1,          // ready: hand the stream to whoever wants it
    kStartTimedOut = 2,  // never ready: end the cast
    kPlaying = 3,        // still running: check the receiver
    kFrozen = 4,         // no segment for too long: end the cast
    kNetworkGone = 5,    // the serving address is gone: end the cast
};

// announced: the stream was handed over already. waitingSinceMs: when it started (same clock as nowMs).
// playlistReady: the playlist lists enough to start on. segmentAgeMs: since the last segment was cut (0 before
// the first). addressHere: the phone still holds the address it serves from (read only once announced).
inline int verdict(bool announced, int64_t nowMs, int64_t waitingSinceMs, bool playlistReady, int64_t segmentAgeMs,
                   bool addressHere) {
    if (!announced) {
        if (playlistReady) return kReady;
        return nowMs - waitingSinceMs > kStartTimeoutMs ? kStartTimedOut : kWait;
    }
    if (segmentAgeMs > kFrozenMs) return kFrozen;
    if (!addressHere) return kNetworkGone;
    return kPlaying;
}

inline int64_t nextPollMs(bool announced) { return announced ? kPlayPollMs : kWaitPollMs; }

// The fallback renderer last drew at drewNs and the stage has drawn since: free it now.
inline bool releaseFallback(int64_t nowNs, int64_t drewNs) { return nowNs - drewNs > kFallbackIdleNs; }

}  // namespace geode::cast::watch
