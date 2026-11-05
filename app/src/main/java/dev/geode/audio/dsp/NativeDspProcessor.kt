package dev.geode.audio.dsp

import androidx.annotation.OptIn
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.Timeline
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.audio.BaseAudioProcessor
import androidx.media3.common.util.UnstableApi
import dev.geode.engine.bridge.GeodeNative
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Everything the native chain is told; re-applied whenever a format change builds a new chain. */
data class DspSettings(
    val enabled: Boolean = false,
    val bandsMb: List<Int> = List(NativeDspProcessor.BANDS) { 0 },
    val bassBoost: Int = 0,
    val loudnessMb: Int = 0,
    val crossfeed: Boolean = false,
    val limiter: Boolean = true,
)

/**
 * The Media3 processor that runs `geode_dsp_process` over the sink's PCM, in place.
 *
 * 16-bit and float input both pass through a direct float buffer the native chain works on; the
 * output keeps the input encoding. The chain itself is created on the playback thread in
 * [onConfigure] and retired chains are only freed in [release], after the player is gone, so a
 * setter racing a format change can never reach a destroyed chain.
 *
 * A format the chain cannot run (more than two channels, a 5.1 or 7.1 file; an encoding other than
 * 16-bit or float) is not an error: [onConfigure] answers [AudioProcessor.AudioFormat.NOT_SET], which
 * makes the processor inactive, and Media3 passes that track's PCM straight through it. Throwing
 * instead, as this used to, fails the sink's configure, and ExoPlayer reports the whole file as
 * unplayable. The equalizer, crossfeed and ReplayGain gain do not apply to such a track; the tap
 * before this processor still hears it, so the visuals do.
 *
 * ReplayGain is not a setting pushed at the chain: the chain takes its gain from a native table of the
 * tags of the tracks around the one playing ([setReplayGainTags], filled ahead by `playback/ReplayGain`),
 * by the stream it is processing. [onFlush] names that stream: Media3 flushes the processors with the new
 * item's timeline and period in stream order, after the last sample of the one before, which is the
 * only point that knows which track a buffer belongs to. A gain set from the player's
 * `onMediaItemTransition` instead lands a sink buffer late, on the head of the next track (#351).
 */
@OptIn(UnstableApi::class)
class NativeDspProcessor : BaseAudioProcessor() {
    @Volatile
    private var handle = 0L
    private var handleSampleRate = 0
    private var handleChannels = 0
    private val retired = ArrayList<Long>()

    @Volatile
    var settings: DspSettings = DspSettings()
        private set

    private var scratch: ByteBuffer = ByteBuffer.allocateDirect(0).order(ByteOrder.nativeOrder())

    private val tableLock = Any()

    @Volatile
    private var table = 0L

    /** The stream the chain is in, so a chain built mid-stream (a format change) carries on with its gain. */
    private var streamName: String? = null

    private val period = Timeline.Period()
    private val window = Timeline.Window()

    /** Made on first use, so a processor that never meets native code (JVM tests) never loads libgeode. */
    private fun replayGainTable(): Long =
        synchronized(tableLock) {
            if (table == 0L) table = GeodeNative.replayGainCreate()
            table
        }

    /** ReplayGain mode (0 off, 1 track, 2 album), preamp and clip guard; any thread. */
    fun configureReplayGain(
        mode: Int,
        preampDb: Float,
        clipGuard: Boolean,
    ) = GeodeNative.replayGainConfigure(replayGainTable(), mode, preampDb, clipGuard)

    /**
     * One track's tags as the file carries them, by its stream name ([streamName] of its item); any
     * thread. [values] is track gain, track peak, album gain, album peak; [mask] the bits of those present.
     */
    fun setReplayGainTags(
        stream: String,
        mask: Int,
        values: FloatArray,
    ) = GeodeNative.replayGainSetTags(replayGainTable(), stream, mask, values)

    fun update(next: DspSettings) {
        settings = next
        val h = handle
        if (h != 0L) apply(h, next)
    }

    override fun onConfigure(inputAudioFormat: AudioProcessor.AudioFormat): AudioProcessor.AudioFormat {
        if (!runs(inputAudioFormat.encoding, inputAudioFormat.channelCount)) return AudioProcessor.AudioFormat.NOT_SET
        ensureChain(inputAudioFormat.sampleRate, inputAudioFormat.channelCount)
        return inputAudioFormat
    }

    private fun ensureChain(
        sampleRate: Int,
        channels: Int,
    ) {
        if (handle != 0L && handleSampleRate == sampleRate && handleChannels == channels) return
        val previous = handle
        val next = GeodeNative.dspCreate(sampleRate, channels)
        if (next != 0L) {
            apply(next, settings)
            GeodeNative.dspSetReplayGain(next, replayGainTable())
            GeodeNative.dspBeginStream(next, streamName)
        }
        handle = next
        handleSampleRate = sampleRate
        handleChannels = channels
        if (previous != 0L) retired.add(previous)
    }

    override fun queueInput(inputBuffer: ByteBuffer) {
        val format = inputAudioFormat
        val channels = format.channelCount
        val bytesPerSample = if (format.encoding == C.ENCODING_PCM_FLOAT) 4 else 2
        val samples = inputBuffer.remaining() / bytesPerSample
        val frames = samples / channels
        if (frames <= 0) {
            inputBuffer.position(inputBuffer.limit())
            return
        }
        val floatBytes = samples * 4
        if (scratch.capacity() < floatBytes) scratch = ByteBuffer.allocateDirect(floatBytes).order(ByteOrder.nativeOrder())
        scratch.clear()
        val floats = scratch.asFloatBuffer()
        if (format.encoding == C.ENCODING_PCM_FLOAT) {
            floats.put(inputBuffer.asFloatBuffer().also { it.limit(samples) })
        } else {
            val shorts = inputBuffer.asShortBuffer()
            for (i in 0 until samples) floats.put(shorts.get(i) / SHORT_FULL_SCALE)
        }
        inputBuffer.position(inputBuffer.limit())
        val h = handle
        if (h != 0L) GeodeNative.dspProcess(h, scratch, frames)
        val output = replaceOutputBuffer(samples * bytesPerSample)
        floats.position(0)
        if (format.encoding == C.ENCODING_PCM_FLOAT) {
            output.asFloatBuffer().put(floats)
        } else {
            val shorts = output.asShortBuffer()
            for (i in 0 until samples) {
                val v = floats.get(i)
                shorts.put(
                    i,
                    (v.coerceIn(-1f, 1f) * SHORT_FULL_SCALE).toInt().coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt()).toShort(),
                )
            }
        }
        output.position(samples * bytesPerSample)
        output.flip()
    }

    override fun onFlush(streamMetadata: AudioProcessor.StreamMetadata) {
        val name = streamName(streamMetadata, period, window)
        // A flush with no playlist context (a bare flush()) keeps the stream it was in.
        if (name != null) streamName = name
        val h = handle
        if (h != 0L) {
            GeodeNative.dspReset(h)
            GeodeNative.dspBeginStream(h, streamName)
        }
    }

    /** Frees every chain; only after the player that owned the playback thread is released. */
    fun release() {
        val h = handle
        handle = 0L
        handleSampleRate = 0
        handleChannels = 0
        if (h != 0L) GeodeNative.dspDestroy(h)
        retired.forEach { GeodeNative.dspDestroy(it) }
        retired.clear()
        // After the chains, which point at it.
        synchronized(tableLock) {
            if (table != 0L) GeodeNative.replayGainDestroy(table)
            table = 0L
        }
    }

    internal fun apply(
        h: Long,
        s: DspSettings,
    ) {
        GeodeNative.dspSetEnabled(h, s.enabled)
        s.bandsMb.take(BANDS).forEachIndexed { band, mb -> GeodeNative.dspSetBand(h, band, mb) }
        GeodeNative.dspSetBassBoost(h, s.bassBoost)
        GeodeNative.dspSetLoudnessMb(h, s.loudnessMb)
        GeodeNative.dspSetCrossfeed(h, s.crossfeed)
        GeodeNative.dspSetLimiter(h, s.limiter)
    }

    companion object {
        const val BANDS = 10
        const val MIN_MB = -1500
        const val MAX_MB = 1500

        private const val SHORT_FULL_SCALE = 32768f

        /** The native chain is built for mono or stereo; anything else steps aside (see the class KDoc). */
        const val MAX_CHANNELS = 2

        /** Whether the chain runs on this format; when false the processor stays inactive for the track. */
        fun runs(
            encoding: Int,
            channelCount: Int,
        ): Boolean = (encoding == C.ENCODING_PCM_16BIT || encoding == C.ENCODING_PCM_FLOAT) && channelCount in 1..MAX_CHANNELS

        /**
         * The name ReplayGain keys a stream by: its item's URI, as `playback/ReplayGain` names the same
         * item from the player's timeline. Null when the flush carries no playlist context.
         */
        fun streamName(
            meta: AudioProcessor.StreamMetadata,
            period: Timeline.Period = Timeline.Period(),
            window: Timeline.Window = Timeline.Window(),
        ): String? {
            val uid = meta.periodUid ?: return null
            val timeline = meta.timeline
            if (timeline.isEmpty) return null
            val index = timeline.getIndexOfPeriod(uid)
            if (index == C.INDEX_UNSET) return null
            timeline.getPeriod(index, period)
            return streamName(timeline.getWindow(period.windowIndex, window).mediaItem)
        }

        /** [streamName] of one item. */
        fun streamName(item: MediaItem?): String? =
            item
                ?.localConfiguration
                ?.uri
                ?.toString()

        /** Band centres in millihertz, as the platform equalizer reported them, from the native chain. */
        fun bandCenterMilliHz(band: Int): Int = (GeodeNative.dspBandCenterHz(band) * 1000f).toInt()
    }
}
