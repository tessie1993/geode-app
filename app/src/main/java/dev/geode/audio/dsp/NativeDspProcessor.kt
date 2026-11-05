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
 * The block is copied once into the output buffer and processed there in place, 16-bit or float as
 * it came; native converts 16-bit itself, so no per-sample loop runs here. This class is only the
 * Media3 shell: it forwards configure, flush and each block to one native `geode_dsp`
 * (`core/audio/dsp/DspProcessor`), which owns a chain per format and the switch between them. That
 * switch follows the [AudioProcessor] contract: [onConfigure] describes the format queued after the
 * next flush, and the old format may still drain through [queueInput] until then (issue #356), so
 * native only stages the next chain at configure and makes it live at [onFlush]. Each block goes down with the channel count it was queued in and the buffer's
 * capacity, and native passes a block it cannot fit through untouched. Native frees its chains only in
 * [release], after the player is gone, so a setter racing a format change never reaches a freed one.
 *
 * A format the chain cannot run (more than two channels, a 5.1 or 7.1 file; an encoding other than
 * 16-bit or float) is not an error: [onConfigure] answers [AudioProcessor.AudioFormat.NOT_SET], which
 * makes the processor inactive, and Media3 passes that track's PCM straight through it. Throwing
 * instead, as this used to, fails the sink's configure, and ExoPlayer reports the whole file as
 * unplayable. The equalizer, crossfeed and the playback gain do not apply to such a track; the tap
 * before this processor still hears it, so the visuals do.
 *
 * The playback gain (ReplayGain, fades, crossfade, sleep) is native too (`core/audio/dsp/GainStage`), and
 * needs to know which item its samples belong to: [onFlush] names the stream that starts at the next
 * sample, in stream order, which is the only point that knows (#351), and [onQueueEndOfStream] says one
 * played out to its end. The shell's setters reach native through [nativeHandle].
 */
@OptIn(UnstableApi::class)
class NativeDspProcessor : BaseAudioProcessor() {
    @Volatile
    private var handle = 0L

    @Volatile
    var settings: DspSettings = DspSettings()
        private set

    private val period = Timeline.Period()
    private val window = Timeline.Window()

    private var released = false

    /**
     * The native DSP, made on first use (a setter or the first configure), so JVM tests never load it; 0
     * after [release], which every native call takes as "nothing to do".
     */
    @Synchronized
    fun nativeHandle(): Long {
        if (handle == 0L && !released) handle = GeodeNative.dspCreate().also { if (it != 0L) apply(it, settings) }
        return handle
    }

    fun update(next: DspSettings) {
        settings = next
        val h = handle
        if (h != 0L) apply(h, next)
    }

    override fun onConfigure(inputAudioFormat: AudioProcessor.AudioFormat): AudioProcessor.AudioFormat {
        if (!runs(inputAudioFormat.encoding, inputAudioFormat.channelCount)) {
            // Stage pass-through, so the next flush never leaves the old format's chain live.
            val h = handle
            if (h != 0L) GeodeNative.dspConfigure(h, 0, 0)
            return AudioProcessor.AudioFormat.NOT_SET
        }
        val h = nativeHandle()
        if (h == 0L || !GeodeNative.dspConfigure(h, inputAudioFormat.sampleRate, inputAudioFormat.channelCount)) {
            return AudioProcessor.AudioFormat.NOT_SET
        }
        return inputAudioFormat
    }

    override fun queueInput(inputBuffer: ByteBuffer) {
        val format = inputAudioFormat
        val floatSamples = format.encoding == C.ENCODING_PCM_FLOAT
        val frameBytes = format.channelCount * (if (floatSamples) 4 else 2)
        val frames = inputBuffer.remaining() / frameBytes
        val whole = inputBuffer.duplicate().apply { limit(position() + frames * frameBytes) }
        inputBuffer.position(inputBuffer.limit())
        val output = replaceOutputBuffer(frames * frameBytes)
        output.put(whole)
        output.flip()
        val h = handle
        if (h != 0L && frames > 0) GeodeNative.dspProcess(h, output, frames, format.channelCount, floatSamples)
    }

    override fun onFlush(streamMetadata: AudioProcessor.StreamMetadata) {
        val h = handle
        if (h == 0L) return
        GeodeNative.dspFlush(h)
        val item = streamWindow(streamMetadata)
        GeodeNative.dspBeginStream(h, streamName(item?.mediaItem), streamMetadata.positionOffsetUs, item?.durationUs ?: C.TIME_UNSET)
    }

    override fun onQueueEndOfStream() {
        val h = handle
        if (h != 0L) GeodeNative.dspEndStream(h)
    }

    /** The item a flush's stream belongs to; null when the flush carries no playlist. */
    private fun streamWindow(meta: AudioProcessor.StreamMetadata): Timeline.Window? {
        val index = meta.periodUid?.let(meta.timeline::getIndexOfPeriod) ?: return null
        if (index == C.INDEX_UNSET) return null
        return meta.timeline.getWindow(meta.timeline.getPeriod(index, period).windowIndex, window)
    }

    /** Frees every chain; only after the player that owned the playback thread is released. */
    @Synchronized
    fun release() {
        released = true
        val h = handle
        handle = 0L
        if (h != 0L) GeodeNative.dspDestroy(h)
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

        /** The native chain is built for mono or stereo; anything else steps aside (see the class KDoc). */
        const val MAX_CHANNELS = 2

        /** Whether the chain runs on this format; when false the processor stays inactive for the track. */
        fun runs(
            encoding: Int,
            channelCount: Int,
        ): Boolean = (encoding == C.ENCODING_PCM_16BIT || encoding == C.ENCODING_PCM_FLOAT) && channelCount in 1..MAX_CHANNELS

        /** The name the native gain keys a stream by: its item's URI, as `playback/ReplayGain` names it too. */
        fun streamName(item: MediaItem?): String? = item?.localConfiguration?.uri?.toString()

        /** Band centres in millihertz, as the platform equalizer reported them, from the native chain. */
        fun bandCenterMilliHz(band: Int): Int = (GeodeNative.dspBandCenterHz(band) * 1000f).toInt()
    }
}
