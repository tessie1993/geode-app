package dev.geode.audio

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjection

enum class CaptureFailure {
    PERMISSION,

    CONSENT,

    UNAVAILABLE,
}

class PlaybackCapture(
    ring: Long,
) : AudioCapturePump(ring, DEFAULT_RATE) {
    @Volatile
    private var channelCount: Int = 1

    override val threadName = "geode-playback-capture"

    @Synchronized
    fun start(projection: MediaProjection): CaptureFailure? {
        if (active) return null
        val rec = openRecord(projection) ?: return CaptureFailure.UNAVAILABLE
        if (!startPump(rec, channelCount)) return CaptureFailure.UNAVAILABLE
        return null
    }

    /**
     * Every usage another app can let a capture hear (media, game, unknown: the platform captures no other),
     * minus this app's own uid. Its own playing is heard through the tap, first and on time, so a capture
     * that also took it would hand the visuals the same music a second time, later by the capture's path.
     * Matching usages and excluding a uid go together (only usage with usage, uid with uid exclude each other).
     */
    private fun openRecord(projection: MediaProjection): AudioRecord? {
        val config =
            AudioPlaybackCaptureConfiguration
                .Builder(projection)
                .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                .addMatchingUsage(AudioAttributes.USAGE_GAME)
                .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
                .excludeUid(android.os.Process.myUid())
                .build()
        val (rec, channels) =
            openFirst { rate, mask, encoding, bytes ->
                @Suppress("MissingPermission")
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
            } ?: return null
        channelCount = channels
        return rec
    }

    private companion object {
        const val DEFAULT_RATE = 48_000
    }
}
