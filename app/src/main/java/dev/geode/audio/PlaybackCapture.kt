package dev.geode.audio

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjection
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort

enum class CaptureFailure {
    PERMISSION,

    CONSENT,

    UNAVAILABLE,
}

/**
 * Another app's audio, written to [ring] by a native thread (`core/audio/ingest/CapturePump`); no sample
 * reaches Kotlin. The one exception to native capture: Android offers playback capture only as a Java
 * [AudioRecord] over a MediaProjection, so the shell builds it here and hands it down, and native starts,
 * reads and releases it.
 */
class PlaybackCapture(
    private val ring: Long,
) {
    private var handle = 0L

    val active: Boolean
        @Synchronized get() = GeodeNative.javaCaptureActive(handle)

    /** Only digital zero for a while since the start (native `core/sound/Silence`): a refused capture. */
    val quiet: Boolean
        @Synchronized get() = GeodeNative.javaCaptureQuiet(handle)

    /** Opens the first format of native's plan (`core/audio/ingest/CapturePlan`) that initializes, and hands it down. */
    @Synchronized
    fun start(projection: MediaProjection): CaptureFailure? {
        if (active) return null
        stop()
        val config =
            AudioPlaybackCaptureConfiguration
                .Builder(projection)
                .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                .addMatchingUsage(AudioAttributes.USAGE_GAME)
                .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
                .build()
        val plan = GeodeNative.captureFormats(0, true)
        for (at in plan.indices step GeodeNative.CAPTURE_FORMAT_INTS) {
            val rate = plan[at + GeodeNative.CAPTURE_FORMAT_RATE]
            val channels = plan[at + GeodeNative.CAPTURE_FORMAT_CHANNELS]
            val asFloat = plan[at + GeodeNative.CAPTURE_FORMAT_FLOAT] != 0
            val mask = if (channels == 2) AudioFormat.CHANNEL_IN_STEREO else AudioFormat.CHANNEL_IN_MONO
            val encoding = if (asFloat) AudioFormat.ENCODING_PCM_FLOAT else AudioFormat.ENCODING_PCM_16BIT
            val bytes = GeodeNative.captureBufferBytes(AudioRecord.getMinBufferSize(rate, mask, encoding), channels)
            val rec = if (bytes > 0) runCatching { open(config, rate, mask, encoding, bytes) }.getOrNull() else null
            if (rec != null && rec.state == AudioRecord.STATE_INITIALIZED) {
                // Owned by native from here on, released there even when it will not record.
                handle = GeodeNative.javaCaptureRecord(rec, ring, rate, channels, asFloat)
                return if (handle != 0L) null else CaptureFailure.UNAVAILABLE
            }
            bestEffort(TAG, "rec?.release()") { rec?.release() }
        }
        return CaptureFailure.UNAVAILABLE
    }

    /** No write follows once this returns. */
    @Synchronized
    fun stop() {
        GeodeNative.javaCaptureStop(handle)
        handle = 0L
    }

    @Suppress("MissingPermission")
    private fun open(
        config: AudioPlaybackCaptureConfiguration,
        rate: Int,
        mask: Int,
        encoding: Int,
        bytes: Int,
    ): AudioRecord =
        AudioRecord
            .Builder()
            .setAudioPlaybackCaptureConfig(config)
            .setAudioFormat(
                AudioFormat
                    .Builder()
                    .setEncoding(encoding)
                    .setSampleRate(rate)
                    .setChannelMask(mask)
                    .build(),
            ).setBufferSizeInBytes(bytes)
            .build()

    private companion object {
        const val TAG = "PlaybackCapture"
    }
}
