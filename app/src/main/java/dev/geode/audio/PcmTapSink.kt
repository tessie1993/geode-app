package dev.geode.audio

import androidx.annotation.OptIn
import androidx.media3.common.C
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.audio.TeeAudioProcessor
import java.nio.ByteBuffer
import java.nio.ByteOrder

@OptIn(UnstableApi::class)
class PcmTapSink(
    private val ring: PcmRingBuffer,
    private val onFormat: (sampleRateHz: Int, channelCount: Int, encoding: Int) -> Unit,
) : TeeAudioProcessor.AudioBufferSink {
    private var channelCount: Int = 2
    private var encoding: Int = C.ENCODING_PCM_16BIT
    private var scratch: FloatArray = FloatArray(0)

    override fun flush(
        sampleRateHz: Int,
        channelCount: Int,
        encoding: Int,
    ) {
        this.channelCount = channelCount
        this.encoding = encoding
        onFormat(sampleRateHz, channelCount, encoding)
    }

    override fun handleBuffer(buffer: ByteBuffer) {
        val b = buffer.duplicate().order(ByteOrder.LITTLE_ENDIAN)
        when (encoding) {
            C.ENCODING_PCM_16BIT -> {
                val sb = b.asShortBuffer()
                val n = sb.remaining()
                if (n == 0 || channelCount <= 0) return
                if (scratch.size < n) scratch = FloatArray(n)
                for (i in 0 until n) scratch[i] = sb.get(i) / 32768f
                ring.writeInterleaved(scratch, n / channelCount, channelCount)
            }
            C.ENCODING_PCM_FLOAT -> {
                val fb = b.asFloatBuffer()
                val n = fb.remaining()
                if (n == 0 || channelCount <= 0) return
                if (scratch.size < n) scratch = FloatArray(n)
                fb.get(scratch, 0, n)
                ring.writeInterleaved(scratch, n / channelCount, channelCount)
            }
            else -> Unit
        }
    }
}
