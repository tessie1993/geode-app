package dev.geode.audio

import android.media.projection.MediaProjection
import dev.geode.engine.bridge.IngestNative

enum class CaptureFailure {
    PERMISSION,

    CONSENT,

    UNAVAILABLE,
}

/** Why Android ended a capture the person did not stop from the app or its notification. */
enum class CaptureEnd {
    /** Android 15 QPR1+ stops every projection when the screen locks. */
    SCREEN_LOCKED,

    /** The status-bar chip's Stop, another app's projection, or anything else Android decided. */
    SYSTEM,
}

/**
 * Another app's audio into [ring]. Native builds the `AudioRecord` over the consent token, reads it on its own
 * thread and releases it (`core/audio/ingest/CapturePump`); the shell only hands down the [MediaProjection].
 */
class PlaybackCapture(
    private val ring: Long,
) {
    private var handle = 0L

    /** The projection the running capture reads; a new consent replaces it, and the system stops the old one. */
    private var bound: MediaProjection? = null

    val active: Boolean
        @Synchronized get() = IngestNative.javaCaptureActive(handle)

    /** Only digital zero for a while since the start (native `core/sound/Silence`): a refused capture. */
    val quiet: Boolean
        @Synchronized get() = IngestNative.javaCaptureQuiet(handle)

    @Synchronized
    fun start(projection: MediaProjection): CaptureFailure? {
        if (active && projection === bound) return null
        stop()
        handle = IngestNative.javaCapturePlayback(projection, ring)
        bound = projection.takeIf { handle != 0L }
        return if (handle != 0L) null else CaptureFailure.UNAVAILABLE
    }

    /** No write follows once this returns. */
    @Synchronized
    fun stop() {
        IngestNative.javaCaptureStop(handle)
        handle = 0L
        bound = null
    }
}
