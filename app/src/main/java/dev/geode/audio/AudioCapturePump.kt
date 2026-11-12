package dev.geode.audio

import android.media.AudioFormat
import android.media.AudioRecord
import androidx.annotation.AnyThread
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import kotlin.concurrent.thread

abstract class AudioCapturePump(
    private val ring: dev.geode.engine.audio.SampleRing,
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
    private var heardAtMs: Long = 0L

    /** Only digital zero for a while since the start (native `core/sound/Silence`): a refused capture. */
    val quiet: Boolean get() = running && GeodeNative.captureQuiet(nowMs(), heardAtMs)

    protected abstract val threadName: String

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
        heardAtMs = nowMs()
        running = true
        // The rate this record was opened at travels with every block it writes.
        val rate = sampleRateHz
        worker =
            thread(name = threadName, isDaemon = true) {
                val asFloat = rec.audioFormat == AudioFormat.ENCODING_PCM_FLOAT
                val floats = FloatArray(if (asFloat) READ_FRAMES * channels else 0)
                val shorts = ShortArray(if (asFloat) 0 else READ_FRAMES * channels)
                while (running && runGeneration == generation) {
                    val n =
                        if (asFloat) {
                            rec.read(floats, 0, floats.size, AudioRecord.READ_BLOCKING)
                        } else {
                            rec.read(shorts, 0, shorts.size)
                        }
                    // stop() bumps runGeneration before it returns, so a read that was
                    // already blocked when stop() was called but only unblocks afterwards
                    // lands here with a stale generation — skip writing into a ring this run
                    // no longer owns.
                    if (runGeneration != generation) break
                    val frames = if (n > 0) n / channels else 0
                    if (frames > 0) {
                        // The ring converts 16-bit to float and measures the block: no sample is read here.
                        val peak =
                            if (asFloat) {
                                ring.write(floats, frames, channels, rate)
                            } else {
                                ring.write(shorts, frames, channels, rate)
                            }
                        heardAtMs = GeodeNative.captureHeardAt(peak, nowMs(), heardAtMs)
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
    }
}

private const val TAG = "AudioCapturePump"
