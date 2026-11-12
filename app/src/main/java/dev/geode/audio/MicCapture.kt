package dev.geode.audio

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioManager
import android.media.AudioRecord
import android.media.MediaRecorder
import androidx.core.content.ContextCompat

class MicCapture(
    private val context: Context,
    ring: Long,
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

    // The device's own rate first (native's plan): any other rate is resampled by the audio HAL.
    private fun openRecord(): AudioRecord? {
        val source = preferredSource()
        return openFirst(deviceRateHz() ?: 0, stereo = false) { rate, mask, encoding, bytes ->
            @Suppress("MissingPermission")
            AudioRecord(source, rate, mask, encoding, bytes)
        }?.first
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
    }
}
