package dev.geode.audio

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.MediaRecorder
import androidx.core.content.ContextCompat
import dev.geode.util.bestEffort

class MicCapture(
    private val context: Context,
    ring: dev.geode.engine.audio.SampleRing,
    nowMs: () -> Long = { android.os.SystemClock.elapsedRealtime() },
) : AudioCapturePump(ring, DEFAULT_RATE, nowMs) {
    enum class Failure {
        PERMISSION,

        UNAVAILABLE,
    }

    override val threadName = "geode-mic"

    fun hasPermission(): Boolean =
        ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED

    @Synchronized
    fun start(): Failure? {
        if (active) return null
        if (!hasPermission()) return Failure.PERMISSION
        val rec = openRecord() ?: return Failure.UNAVAILABLE
        if (!startPump(rec, 1)) return Failure.UNAVAILABLE
        return null
    }

    private fun openRecord(): AudioRecord? {
        val source = preferredSource()
        // The device's own rate first: any other rate is resampled by the audio HAL, which costs
        // latency and, on some devices, the UNPROCESSED path. 44.1 kHz was tried first before.
        val rates = (listOfNotNull(deviceRateHz()) + FALLBACK_RATES).distinct()
        for (rate in rates) {
            for (encoding in intArrayOf(AudioFormat.ENCODING_PCM_FLOAT, AudioFormat.ENCODING_PCM_16BIT)) {
                val min = AudioRecord.getMinBufferSize(rate, AudioFormat.CHANNEL_IN_MONO, encoding)
                if (min <= 0) continue
                val rec =
                    runCatching {
                        @Suppress("MissingPermission")
                        AudioRecord(
                            source,
                            rate,
                            AudioFormat.CHANNEL_IN_MONO,
                            encoding,
                            (min * BUFFER_MULTIPLIER).coerceAtLeast(READ_FRAMES * 4),
                        )
                    }.getOrNull()
                if (rec != null && rec.state == AudioRecord.STATE_INITIALIZED) {
                    sampleRateHz = rate
                    return rec
                }
                bestEffort(TAG, "rec?.release()") { rec?.release() }
            }
        }
        return null
    }

    private fun deviceRateHz(): Int? =
        runCatching {
            (context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager)
                ?.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)
                ?.toIntOrNull()
                ?.takeIf { it > 0 }
        }.getOrNull()

    private fun preferredSource(): Int {
        val supported =
            runCatching {
                (context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager)
                    ?.getProperty(AudioManager.PROPERTY_SUPPORT_AUDIO_SOURCE_UNPROCESSED)
            }.getOrNull()
        return if (supported == "true") {
            MediaRecorder.AudioSource.UNPROCESSED
        } else {
            MediaRecorder.AudioSource.VOICE_RECOGNITION
        }
    }

    private companion object {
        /** Reported until [openRecord] has opened a stream and set the rate it actually got. */
        const val DEFAULT_RATE = 48_000

        /** Tried after the device's rate, most common first; 22.05 kHz for very old hardware. */
        val FALLBACK_RATES = listOf(48_000, 44_100, 22_050)
    }
}

private const val TAG = "MicCapture"
