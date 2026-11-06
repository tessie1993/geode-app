package dev.geode.audio

import android.media.AudioFormat
import android.media.AudioRecord
import androidx.annotation.AnyThread
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.concurrent.thread

/**
 * Reads an [AudioRecord] on its own worker thread into this source's native ingest [ring].
 *
 * The record fills a direct [ByteBuffer] and the bytes go to `geode_ingest_write` as they are, int16 or
 * float, with the record's own encoding: the conversion to float, the downmix and the silence rule are
 * native (`core/audio/ingest`, ADR 0003), so no loop here touches a sample.
 *
 * Each run begins a new epoch on the worker, before its first write, carrying the rate and channels the
 * record actually opened with (#361: an epoch begun off the writer thread could pair new samples with
 * the old rate). A renderer reading this ring then restarts on the new stream instead of reading a seam.
 */
abstract class AudioCapturePump(
    /** This source's ring ([AudioBus]); the worker is its one writer. */
    val ring: Long,
    defaultRateHz: Int,
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

    protected abstract val threadName: String

    /**
     * Whether the ring has been silent (or unwritten) for the native silence hold: the one answer
     * `core/sound/Silence` gives every surface. The ring times the hold on its pollers' clock, so every
     * poller of a ring passes [android.os.SystemClock.uptimeMillis].
     */
    protected fun ringResting(): Boolean = GeodeNative.ingestResting(ring, android.os.SystemClock.uptimeMillis())

    @AnyThread
    @Synchronized
    protected fun startPump(
        rec: AudioRecord,
        channels: Int,
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
        val rate = sampleRateHz
        worker =
            thread(name = threadName, isDaemon = true) {
                // The encoding values are android.media.AudioFormat's, which geode_api.h's
                // GEODE_PCM_S16 and GEODE_PCM_F32 equal, so the record's own goes through as is.
                val encoding = rec.audioFormat
                val sampleBytes = if (encoding == AudioFormat.ENCODING_PCM_FLOAT) FLOAT_BYTES else SHORT_BYTES
                val bytes = ByteBuffer.allocateDirect(READ_FRAMES * sampleBytes * channels).order(ByteOrder.nativeOrder())
                val layout = if (channels == 1) AudioFormat.CHANNEL_OUT_MONO else AudioFormat.CHANNEL_OUT_STEREO
                GeodeNative.ingestBeginEpoch(ring, rate, channels, layout)
                while (running && runGeneration == generation) {
                    // Fills from index 0 and leaves the buffer's position alone; returns bytes.
                    val n = rec.read(bytes, bytes.capacity(), AudioRecord.READ_BLOCKING)
                    // stop() bumps runGeneration before it returns, so a read that was
                    // already blocked when stop() was called but only unblocks afterwards
                    // lands here with a stale generation — skip writing into a ring this run
                    // no longer owns.
                    if (runGeneration != generation) break
                    if (n > 0) {
                        // Sized in bytes: native takes the whole frames that fit.
                        GeodeNative.ingestWrite(ring, bytes, 0, n, encoding)
                    } else if (n < 0) {
                        android.util.Log.w(javaClass.simpleName, "AudioRecord.read error $n")
                        break
                    }
                }
                // This worker is the sole owner of `rec`; release it regardless of whether the
                // generation moved on, since nobody else will.
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
        // Invalidate the generation the worker captured at start. If join() below times out
        // with the worker still inside a blocking rec.read(), this lets it notice on return
        // and exit without writing into the ring or touching a record a later run now owns.
        runGeneration++
        record?.let { runCatching { it.stop() } }
        worker?.let { runCatching { it.join(500) } }
        worker = null
        record = null
    }

    protected companion object {
        const val READ_FRAMES = 1024

        const val BUFFER_MULTIPLIER = 4

        const val SHORT_BYTES = 2

        const val FLOAT_BYTES = 4
    }
}

private const val TAG = "AudioCapturePump"
