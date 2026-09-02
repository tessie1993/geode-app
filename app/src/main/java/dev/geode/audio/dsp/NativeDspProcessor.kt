package dev.geode.audio.dsp

import androidx.annotation.OptIn
import androidx.media3.common.C
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.audio.AudioProcessor.UnhandledAudioFormatException
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
    val gainDb: Float = 0f,
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

    fun update(next: DspSettings) {
        settings = next
        val h = handle
        if (h != 0L) apply(h, next)
    }

    override fun onConfigure(inputAudioFormat: AudioProcessor.AudioFormat): AudioProcessor.AudioFormat {
        if (inputAudioFormat.encoding != C.ENCODING_PCM_16BIT && inputAudioFormat.encoding != C.ENCODING_PCM_FLOAT) {
            throw UnhandledAudioFormatException(inputAudioFormat)
        }
        if (inputAudioFormat.channelCount !in 1..2) throw UnhandledAudioFormatException(inputAudioFormat)
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
        if (next != 0L) apply(next, settings)
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
                shorts.put(i, (v.coerceIn(-1f, 1f) * SHORT_FULL_SCALE).toInt().coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt()).toShort())
            }
        }
        output.position(samples * bytesPerSample)
        output.flip()
    }

    override fun onFlush() {
        val h = handle
        if (h != 0L) GeodeNative.dspReset(h)
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
    }

    private fun apply(
        h: Long,
        s: DspSettings,
    ) {
        GeodeNative.dspSetEnabled(h, s.enabled)
        s.bandsMb.take(BANDS).forEachIndexed { band, mb -> GeodeNative.dspSetBand(h, band, mb) }
        GeodeNative.dspSetBassBoost(h, s.bassBoost)
        GeodeNative.dspSetLoudnessMb(h, s.loudnessMb)
        GeodeNative.dspSetGainDb(h, s.gainDb)
        GeodeNative.dspSetCrossfeed(h, s.crossfeed)
        GeodeNative.dspSetLimiter(h, s.limiter)
    }

    companion object {
        const val BANDS = 10
        const val MIN_MB = -1500
        const val MAX_MB = 1500

        private const val SHORT_FULL_SCALE = 32768f

        /** Band centres in millihertz, as the platform equalizer reported them, from the native chain. */
        fun bandCenterMilliHz(band: Int): Int = (GeodeNative.dspBandCenterHz(band) * 1000f).toInt()
    }
}
