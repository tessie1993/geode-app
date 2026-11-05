package dev.geode.engine.audioandroid

import androidx.annotation.OptIn
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.audio.AudioProcessor.AudioFormat
import androidx.media3.common.audio.BaseAudioProcessor
import androidx.media3.common.util.UnstableApi
import dev.geode.engine.audio.PcmSink
import dev.geode.engine.bridge.GeodeNative
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * First in the player's chain (owner, 2026-11-05), so the ring holds source-time samples. Not a tee: a
 * tee drops the flush's [AudioProcessor.StreamMetadata] and reports end-of-stream as a flush. Each flush's
 * facts go to native, whose `core/audio/ingest/FlushJudge` decides the epoch (#358).
 */
@OptIn(UnstableApi::class)
class PcmTap(
    private val sink: PcmSink,
    private val ring: Long,
    private val boundaryListener: TapBoundaryListener? = null,
    private val onFormat: (PcmTapFormat) -> Unit = {},
) : BaseAudioProcessor() {
    private var format: PcmTapFormat? = null
    private var framesWritten = 0L
    private var staging: FloatArray = FloatArray(0)
    private var reapplying = false

    /** The sink re-applies speed, pitch or skip-silence: its next flush is not a seek. Playback thread. */
    fun onParametersReapplied() {
        reapplying = true
    }

    override fun onConfigure(inputAudioFormat: AudioFormat): AudioFormat = inputAudioFormat

    override fun queueInput(inputBuffer: ByteBuffer) {
        if (!inputBuffer.hasRemaining()) return
        handleBuffer(inputBuffer)
        replaceOutputBuffer(inputBuffer.remaining()).put(inputBuffer).flip()
    }

    override fun onFlush(streamMetadata: AudioProcessor.StreamMetadata) {
        val input = inputAudioFormat
        val meta = streamMetadata
        val periodId = meta.periodUid?.hashCode()?.toLong() ?: 0L
        val newEpoch = GeodeNative.ingestFlush(ring, input.sampleRate, input.channelCount, 0, reapplying, meta.positionOffsetUs, periodId)
        reapplying = false
        val next = PcmTapFormat(input.sampleRate, input.channelCount, input.encoding, (format?.generation ?: 0) + 1, newEpoch)
        boundaryListener?.onTapBoundary(format, framesWritten, next)
        framesWritten = 0L
        format = next
        onFormat(next)
    }

    private fun handleBuffer(buffer: ByteBuffer) {
        val active = format ?: return
        val width = active.sampleWidth ?: return
        val channels = active.channelCount
        if (channels <= 0) return
        if (staging.size != STAGING_FRAMES * channels) staging = FloatArray(STAGING_FRAMES * channels)

        buffer.order(ByteOrder.LITTLE_ENDIAN)

        val frameBytes = width.bytes * channels
        var at = buffer.position()
        val end = buffer.limit()
        var written = framesWritten
        while (end - at >= frameBytes) {
            val frames = minOf(STAGING_FRAMES, (end - at) / frameBytes)
            val samples = frames * channels
            when (width) {
                PcmSampleWidth.SIGNED_16 -> fill16(buffer, at, samples)
                PcmSampleWidth.FLOAT_32 -> fillFloat(buffer, at, samples)
            }
            sink.write(staging, frames, channels)
            at += samples * width.bytes
            written += frames
        }
        framesWritten = written
    }

    private fun fill16(
        buffer: ByteBuffer,
        from: Int,
        samples: Int,
    ) {
        for (i in 0 until samples) staging[i] = buffer.getShort(from + (i shl 1)) / SHORT_FULL_SCALE
    }

    private fun fillFloat(
        buffer: ByteBuffer,
        from: Int,
        samples: Int,
    ) {
        for (i in 0 until samples) staging[i] = buffer.getFloat(from + (i shl 2))
    }

    private companion object {
        const val STAGING_FRAMES = 4096

        const val SHORT_FULL_SCALE = 32768f
    }
}
