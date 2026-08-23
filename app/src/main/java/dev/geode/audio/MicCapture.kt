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
import kotlin.math.abs

class MicCapture(
    private val context: Context,
    sink: dev.geode.engine.audio.PcmSink,
    nowMs: () -> Long = { android.os.SystemClock.elapsedRealtime() },
) : AudioCapturePump(sink, DEFAULT_RATE, nowMs) {
    enum class Failure {
        PERMISSION,

        UNAVAILABLE,
    }

    @Volatile
    var peakLevel: Float = 0f
        private set

    @Volatile
    var silenceLikely: Boolean = false
        private set

    override val threadName = "geode-mic"

    fun hasPermission(): Boolean =
        ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED

    @Synchronized
    fun start(onSampleRate: (Int) -> Unit = {}): Failure? {
        if (active) return null
        if (!hasPermission()) return Failure.PERMISSION
        val rec = openRecord() ?: return Failure.UNAVAILABLE
        if (!startPump(rec, 1, onSampleRate)) return Failure.UNAVAILABLE
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
        peakLevel = peak
        val now = nowMs()
        if (peak > SILENCE_EPSILON) {
            lastAudibleAtMs = now
            silenceLikely = false
        } else {
            val quietSince = if (lastAudibleAtMs == 0L) startedAtMs else lastAudibleAtMs
            if (now - quietSince > SILENCE_GRACE_MS) silenceLikely = true
        }
    }

    override fun resetLevel() {
        peakLevel = 0f
        silenceLikely = false
    }

    private fun openRecord(): AudioRecord? {
        val source = preferredSource()
        for (rate in intArrayOf(DEFAULT_RATE, 48_000, 22_050)) {
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
        const val DEFAULT_RATE = 44_100
    }
}

private const val TAG = "MicCapture"
