package dev.geode.audio

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.projection.MediaProjection
import android.os.Build
import androidx.annotation.RequiresApi
import dev.geode.util.bestEffort
import kotlin.math.abs

enum class CaptureFailure {
    UNSUPPORTED,

    PERMISSION,

    CONSENT,

    UNAVAILABLE,
}

val playbackCaptureSupported: Boolean get() = Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q

class PlaybackCapture(
    sink: dev.geode.engine.audio.PcmSink,
) : AudioCapturePump(sink, DEFAULT_RATE) {
    @Volatile
    var blockedLikely: Boolean = false
        private set

    @Volatile
    private var channelCount: Int = 1

    override val threadName = "geode-playback-capture"

    @RequiresApi(Build.VERSION_CODES.Q)
    @Synchronized
    fun start(
        projection: MediaProjection,
        onSampleRate: (Int) -> Unit = {},
    ): CaptureFailure? {
        if (active) return null
        val rec = openRecord(projection) ?: return CaptureFailure.UNAVAILABLE
        if (!startPump(rec, channelCount, onSampleRate)) return CaptureFailure.UNAVAILABLE
        return null
    }

    override fun noteLevel(
        buffer: FloatArray,
        count: Int,
        startedAtMs: Long,
    ) {
        var peak = 0f
        for (i in 0 until count) {
            val v = abs(buffer[i])
            if (v > peak) peak = v
        }
        val now = nowMs()
        if (peak > SILENCE_EPSILON) {
            lastAudibleAtMs = now
            blockedLikely = false
        } else {
            val quietSince = if (lastAudibleAtMs == 0L) startedAtMs else lastAudibleAtMs
            if (now - quietSince > SILENCE_GRACE_MS) blockedLikely = true
        }
    }

    override fun resetLevel() {
        blockedLikely = false
    }

    @RequiresApi(Build.VERSION_CODES.Q)
    private fun openRecord(projection: MediaProjection): AudioRecord? {
        val config =
            AudioPlaybackCaptureConfiguration
                .Builder(projection)
                .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                .addMatchingUsage(AudioAttributes.USAGE_GAME)
                .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
                .build()
        for (rate in intArrayOf(DEFAULT_RATE, 44_100, 22_050)) {
            for (mask in intArrayOf(AudioFormat.CHANNEL_IN_STEREO, AudioFormat.CHANNEL_IN_MONO)) {
                for (encoding in intArrayOf(AudioFormat.ENCODING_PCM_FLOAT, AudioFormat.ENCODING_PCM_16BIT)) {
                    val min = AudioRecord.getMinBufferSize(rate, mask, encoding)
                    if (min <= 0) continue
                    val channels = if (mask == AudioFormat.CHANNEL_IN_STEREO) 2 else 1
                    val rec =
                        runCatching {
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
                                ).setBufferSizeInBytes(
                                    (min * BUFFER_MULTIPLIER).coerceAtLeast(READ_FRAMES * channels * 4),
                                ).build()
                        }.getOrNull()
                    if (rec != null && rec.state == AudioRecord.STATE_INITIALIZED) {
                        sampleRateHz = rate
                        channelCount = channels
                        return rec
                    }
                    bestEffort(TAG, "rec?.release()") { rec?.release() }
                }
            }
        }
        return null
    }

    private companion object {
        const val DEFAULT_RATE = 48_000
    }
}

private const val TAG = "PlaybackCapture"
