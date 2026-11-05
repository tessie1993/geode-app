package dev.geode.audio

import dev.geode.engine.bridge.GeodeNative

/**
 * Which native ingest ring (`core/audio/ingest`, behind `geode_api.h`) the app's visuals hear, and the
 * capture sources' own rings.
 *
 * One ring per source (#478): the player's tap writes the playback session's, the mic and another app's
 * audio write their own here, and the wallpaper's output-mix fallback writes [outputMixRing]. Each write
 * carries its rate, channels and epoch in-band, so switching the visuals from one source to another is a
 * handle swap: the renderer's own cursor restarts on the new ring at that ring's rate, and can never read
 * mic samples at the player's rate or the other way round (the old shared Kotlin ring could, for a frame).
 *
 * The capture rings live as long as the process. A renderer on another thread (the wallpaper's, the
 * dream's, a cast) may hold any of these handles while the UI's player session comes and goes, so
 * a ring is never destroyed under it; three small rings are cheaper than a lifetime protocol.
 */
object AudioBus {
    /**
     * The ring the session's visuals hear now: the player's, or the mic's or capture's while one of them
     * owns the visuals (the UI's `CaptureController`). 0 when no playback session exists. Read on
     * every GL thread (the app's renderer, the wallpaper and dream through `SoundSource`, a cast).
     */
    @Volatile
    var soundSource: Long = 0L

    /** The mic's ring; [MicCapture]'s worker is its one writer. */
    val micRing: Long by lazy { GeodeNative.ingestCreate(CAPTURE_RING_FRAMES, CAPTURE_MAX_CHANNELS) }

    /** Other apps' audio (MediaProjection); [PlaybackCapture]'s worker is its one writer. */
    val captureRing: Long by lazy { GeodeNative.ingestCreate(CAPTURE_RING_FRAMES, CAPTURE_MAX_CHANNELS) }

    /** The wallpaper's and dream's output-mix capture; written on the main thread (`OutputMixCapture`). */
    val outputMixRing: Long by lazy { GeodeNative.ingestCreate(CAPTURE_RING_FRAMES, 1) }

    /** About 1.4 s at 48 kHz: a renderer reads every frame, so this is only the stall it can ride out. */
    private const val CAPTURE_RING_FRAMES = 1 shl 16

    /** Mic is mono, other apps' audio at most stereo. */
    private const val CAPTURE_MAX_CHANNELS = 2
}
