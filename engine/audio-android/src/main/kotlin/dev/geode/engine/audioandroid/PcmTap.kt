package dev.geode.engine.audioandroid

import androidx.annotation.OptIn
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.audio.AudioProcessor.AudioFormat
import androidx.media3.common.audio.BaseAudioProcessor
import androidx.media3.common.util.UnstableApi
import dev.geode.engine.bridge.GeodeNative
import java.nio.ByteBuffer

/**
 * The player's PCM tap, first in `MvzAudioProcessorChain` (owner, 2026-11-05): every buffer the player
 * decodes, before Media3's skip-silence and Sonic and before any DSP, so the ring holds source-time samples.
 * Following the speaker at a speed other than 1x is the playout clock's job (#482, #488), not the tap's.
 *
 * It touches no sample (#481, ADR 0003). [queueInput] hands the buffer straight to the native ingest ring
 * ([ring], `core/audio/ingest`), which converts int16, u8 or float, keeps every channel with its layout and
 * gives each renderer the stereo downmix through its own cursor, then passes the same bytes on unchanged.
 *
 * Not a `TeeAudioProcessor`: a tee drops the flush's [AudioProcessor.StreamMetadata] and reports
 * end-of-stream as a flush, and those are the facts that tell a seek from a speed change. [onFlush] reports
 * them on the playback thread, before the new stream's first sample is written, and native's
 * `core/audio/ingest/FlushJudge` decides whether this is a new epoch or the same one in a new format (#358,
 * #361). Kotlin decides nothing.
 */
@OptIn(UnstableApi::class)
class PcmTap(
    /** The native ingest ring (`geode_ingest*`); it outlives the player (see `PlaybackEngine.playerIngest`). */
    private val ring: Long,
    private val onFormat: (PcmTapFormat) -> Unit = {},
) : BaseAudioProcessor() {
    @Volatile
    var format: PcmTapFormat? = null
        private set

    private var reapplying = false

    /** Only for a heap buffer, which JNI cannot address; Media3's sink buffers are direct. One bulk copy. */
    private var bounce: ByteBuffer? = null

    /** The sink re-applies speed, pitch or skip-silence: its next flush is not a seek. Playback thread. */
    fun onParametersReapplied() {
        reapplying = true
    }

    override fun onConfigure(inputAudioFormat: AudioFormat): AudioFormat = inputAudioFormat

    override fun queueInput(inputBuffer: ByteBuffer) {
        val length = inputBuffer.remaining()
        if (length <= 0) return
        format?.let { active ->
            val bytes = if (inputBuffer.isDirect) inputBuffer else bounced(inputBuffer)
            GeodeNative.ingestWrite(ring, bytes, bytes.position(), length, active.encoding)
        }
        replaceOutputBuffer(length).put(inputBuffer).flip()
    }

    override fun onFlush(streamMetadata: AudioProcessor.StreamMetadata) {
        val input = inputAudioFormat
        val periodId = streamMetadata.periodUid?.hashCode()?.toLong() ?: 0L
        // Mask 0: the default layout for the count (Media3's format carries no mask).
        val newEpoch =
            GeodeNative.ingestFlush(
                ring,
                input.sampleRate,
                input.channelCount,
                0,
                reapplying,
                streamMetadata.positionOffsetUs,
                periodId,
            )
        reapplying = false
        val next = PcmTapFormat(input.sampleRate, input.channelCount, input.encoding, newEpoch)
        format = next
        onFormat(next)
    }

    private fun bounced(buffer: ByteBuffer): ByteBuffer {
        val length = buffer.remaining()
        val direct = bounce?.takeIf { it.capacity() >= length } ?: ByteBuffer.allocateDirect(length).also { bounce = it }
        direct.clear()
        direct.put(buffer.duplicate())
        direct.flip()
        return direct
    }
}
