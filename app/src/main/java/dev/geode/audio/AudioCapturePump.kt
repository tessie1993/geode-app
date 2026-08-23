package dev.geode.audio

import android.media.AudioFormat
import android.media.AudioRecord
import androidx.annotation.AnyThread
import dev.geode.util.bestEffort
import kotlin.concurrent.thread

abstract class AudioCapturePump(
    private val sink: dev.geode.engine.audio.PcmSink,
    defaultRateHz: Int,
    protected val nowMs: () -> Long = { android.os.SystemClock.elapsedRealtime() },
) {
    private var record: AudioRecord? = null
    private var worker: Thread? = null

    @Volatile
    private var running = false

    @Volatile
    private var runGeneration = 0

    val active: Boolean get() = running

    @Volatile
    var sampleRateHz: Int = defaultRateHz
        protected set

    @Volatile
    protected var lastAudibleAtMs: Long = 0L

    protected abstract val threadName: String

    protected abstract fun noteLevel(
        buffer: FloatArray,
        count: Int,
        startedAtMs: Long,
    )

    protected abstract fun resetLevel()

    @AnyThread
    @Synchronized
    protected fun startPump(
        rec: AudioRecord,
        channels: Int,
        onSampleRate: (Int) -> Unit,
    ): Boolean {
        val recording =
            runCatching { rec.startRecording() }.isSuccess &&
                rec.recordingState == AudioRecord.RECORDSTATE_RECORDING
        if (!recording) {
            android.util.Log.w(javaClass.simpleName, "startRecording refused")
            bestEffort(TAG, "rec.stop()") { rec.stop() }
            bestEffort(TAG, "rec.release()") { rec.release() }
            return false
        }
        record = rec
        val generation = ++runGeneration
        running = true
        lastAudibleAtMs = 0L
        resetLevel()
        onSampleRate(sampleRateHz)
        worker =
            thread(name = threadName, isDaemon = true) {
                val floats = FloatArray(READ_FRAMES * channels)
                val shorts = ShortArray(READ_FRAMES * channels)
                val asFloat = rec.audioFormat == AudioFormat.ENCODING_PCM_FLOAT
                val startedAt = nowMs()
                while (running && runGeneration == generation) {
                    val n =
                        if (asFloat) {
                            rec.read(floats, 0, floats.size, AudioRecord.READ_BLOCKING)
                        } else {
                            val read = rec.read(shorts, 0, shorts.size)
                            if (read > 0) {
                                for (i in 0 until read) floats[i] = shorts[i] / 32768f
                            }
                            read
                        }
                    if (runGeneration != generation) break
                    if (n > 0) {
                        val frames = n / channels
                        if (frames > 0) {
                            sink.write(floats, frames, channels)
                            noteLevel(floats, n, startedAt)
                        }
                    } else if (n < 0) {
                        android.util.Log.w(javaClass.simpleName, "AudioRecord.read error $n")
                        break
                    }
                }
                bestEffort(TAG, "rec.stop()") { rec.stop() }
                bestEffort(TAG, "rec.release()") { rec.release() }
                if (runGeneration == generation) running = false
            }
        return true
    }

    @AnyThread
    @Synchronized
    fun stop() {
        running = false
        record?.let { runCatching { it.stop() } }
        worker?.let { runCatching { it.join(500) } }
        worker = null
        record = null
        resetLevel()
    }

    protected companion object {
        const val READ_FRAMES = 1024

        const val BUFFER_MULTIPLIER = 4

        const val SILENCE_EPSILON = 1e-6f

        const val SILENCE_GRACE_MS = 4_000L
    }
}

private const val TAG = "AudioCapturePump"
