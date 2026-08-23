package dev.geode.engine.audioandroid

import androidx.annotation.OptIn
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.audio.TeeAudioProcessor
import dev.geode.engine.audio.PcmSink
import java.nio.ByteBuffer
import java.nio.ByteOrder

@OptIn(UnstableApi::class)
class PcmTap(
    private val sink: PcmSink,
    private val onFormat: (PcmTapFormat) -> Unit = {},
) : TeeAudioProcessor.AudioBufferSink {
    @Volatile
    var format: PcmTapFormat? = null
        private set

    @Volatile
    var boundaryListener: TapBoundaryListener? = null

    @Volatile
    var boundaryFailures: Long = 0L
        private set

    @Volatile
    var framesWritten: Long = 0L
        private set

    private var staging: FloatArray = FloatArray(0)

    override fun flush(
        sampleRateHz: Int,
        channelCount: Int,
        encoding: Int,
    ) {
        val ended = format
        val endedFrames = framesWritten
        val next =
            PcmTapFormat(
                sampleRateHz = sampleRateHz,
                channelCount = channelCount,
                encoding = encoding,
                generation = (ended?.generation ?: 0) + 1,
            )
        if (channelCount > 0) {
            val floats = STAGING_FRAMES * channelCount
            if (staging.size != floats) staging = FloatArray(floats)
        }
        framesWritten = 0L
        format = next
        try {
            boundaryListener?.onTapBoundary(ended, endedFrames, next)
        } catch (survivable: RuntimeException) {
            boundaryFailures++
        }
        onFormat(next)
    }

    override fun handleBuffer(buffer: ByteBuffer) {
        val active = format ?: return
        val width = active.sampleWidth ?: return
        val channels = active.channelCount
        val perChunk = if (channels > 0) staging.size / channels else 0
        if (perChunk == 0) return

        buffer.order(ByteOrder.LITTLE_ENDIAN)

        val frameBytes = width.bytes * channels
        var at = buffer.position()
        val end = buffer.limit()
        var written = framesWritten
        while (end - at >= frameBytes) {
            val frames = minOf(perChunk, (end - at) / frameBytes)
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
