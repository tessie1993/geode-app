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

    /**
     * Opens the first stereo-first format of native's plan (`core/audio/ingest/CapturePlan`) that [open] gets
     * initialized, at the buffer native sizes, and keeps its rate. Returns the record and its channel count.
     */
    protected fun openFirst(open: (rate: Int, mask: Int, encoding: Int, bufferBytes: Int) -> AudioRecord): Pair<AudioRecord, Int>? {
        val plan = GeodeNative.captureFormats(0, true)
        for (at in plan.indices step GeodeNative.CAPTURE_FORMAT_INTS) {
            val rate = plan[at + GeodeNative.CAPTURE_FORMAT_RATE]
            val channels = plan[at + GeodeNative.CAPTURE_FORMAT_CHANNELS]
            val mask = if (channels == 2) AudioFormat.CHANNEL_IN_STEREO else AudioFormat.CHANNEL_IN_MONO
            val encoding =
                if (plan[at + GeodeNative.CAPTURE_FORMAT_FLOAT] != 0) AudioFormat.ENCODING_PCM_FLOAT else AudioFormat.ENCODING_PCM_16BIT
            val bytes = GeodeNative.captureBufferBytes(AudioRecord.getMinBufferSize(rate, mask, encoding), channels)
            val rec = if (bytes > 0) runCatching { open(rate, mask, encoding, bytes) }.getOrNull() else null
            if (rec != null && rec.state == AudioRecord.STATE_INITIALIZED) {
                sampleRateHz = rate
                return rec to channels
            }
            bestEffort(TAG, "rec?.release()") { rec?.release() }
        }
        return null
    }

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
                val samples = GeodeNative.captureReadFrames() * channels
                val floats = FloatArray(if (asFloat) samples else 0)
                val shorts = ShortArray(if (asFloat) 0 else samples)
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
}

private fun nowMs(): Long = android.os.SystemClock.elapsedRealtime()

private const val TAG = "AudioCapturePump"
