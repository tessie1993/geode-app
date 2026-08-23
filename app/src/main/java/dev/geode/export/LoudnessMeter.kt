package dev.geode.export

import android.content.Context
import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import dev.geode.util.bestEffort
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.log10
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.tan

/**
 * Integrated loudness of a whole programme.
 *
 * A file whose every 400 ms block sits under the -70 LUFS absolute gate has no defined integrated
 * loudness — the spec's answer is minus infinity. That is a different fact from "it measured -70",
 * so it gets its own case instead of a sentinel number a caller could accidentally do arithmetic on.
 */
sealed interface IntegratedLoudness {
    data class Lufs(
        val value: Double,
    ) : IntegratedLoudness

    data object BelowGate : IntegratedLoudness
}

/**
 * What ITU-R BS.1770-4 says about one finished file.
 *
 * [truePeakDbtp] is the inter-sample peak from the 4x oversampled signal, which is what a lossy
 * encoder and a consumer DAC actually have to reconstruct; [samplePeakDbfs] is the plain maximum
 * of the stored samples and is always the smaller of the two. Both are reported because a file
 * that reads 0.0 dBFS but +1.4 dBTP is a file that will distort on playback, and only the second
 * number says so.
 */
data class LoudnessReport(
    val integrated: IntegratedLoudness,
    val truePeakDbtp: Double,
    val samplePeakDbfs: Double,
    val sampleRate: Int,
    val channelCount: Int,
    val measuredMs: Long,
    val gatedBlockCount: Int,
)

/** Outcome of measuring a file. Every case a caller has to cope with is a value, not a throw. */
sealed interface LoudnessResult {
    data class Measured(
        val report: LoudnessReport,
    ) : LoudnessResult

    /** The container has no audio stream at all — a muted export, for instance. */
    data object NoAudioTrack : LoudnessResult

    /** Under one 400 ms block of audio, so BS.1770 has nothing to gate. */
    data object TooShort : LoudnessResult

    /** The file could not be decoded, or decoded into something BS.1770 has no channel weights for. */
    data class Unreadable(
        val message: String,
    ) : LoudnessResult

    data object Cancelled : LoudnessResult
}

/**
 * Measures the loudness of an *exported* file — the thing that will actually be uploaded, after
 * every effect, the encoder and any normalising gain. Measuring the source instead would answer a
 * question nobody asked.
 *
 * The work is a full decode plus per-sample filtering, so it is IO-dispatched and reports progress;
 * on a phone a three-minute stereo file lands in a couple of seconds, dominated by the true-peak
 * oversampler rather than the decode.
 */
class LoudnessMeter(
    private val context: Context,
) {
    suspend fun measure(
        uri: Uri,
        isCancelled: () -> Boolean = { false },
        onProgress: (Float) -> Unit = {},
    ): LoudnessResult =
        withContext(Dispatchers.IO) {
            decode(uri, { !isActive || isCancelled() }, onProgress)
        }

    suspend fun measure(
        file: File,
        isCancelled: () -> Boolean = { false },
        onProgress: (Float) -> Unit = {},
    ): LoudnessResult = measure(Uri.fromFile(file), isCancelled, onProgress)

    @Suppress("ReturnCount")
    private fun decode(
        uri: Uri,
        isCancelled: () -> Boolean,
        onProgress: (Float) -> Unit,
    ): LoudnessResult {
        val extractor = MediaExtractor()
        var decoder: MediaCodec? = null
        try {
            runCatching { extractor.setDataSource(context, uri, null) }
                .onFailure { return LoudnessResult.Unreadable("That file could not be opened: ${it.reason()}") }
            val trackIndex =
                (0 until extractor.trackCount).firstOrNull {
                    extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
                } ?: return LoudnessResult.NoAudioTrack
            val sourceFormat = extractor.getTrackFormat(trackIndex)
            val mime = sourceFormat.getString(MediaFormat.KEY_MIME) ?: return LoudnessResult.NoAudioTrack
            extractor.selectTrack(trackIndex)
            val started =
                runCatching {
                    MediaCodec.createDecoderByType(mime).also {
                        // Published before configure/start, not after: a codec that fails to
                        // configure still exists, and if `decoder` is only assigned once this
                        // block returns, the finally below has nothing to release. MediaCodec
                        // instances come from a device-global pool, so one leaked here fails the
                        // next unrelated export rather than this one.
                        decoder = it
                        it.configure(sourceFormat, null, null, 0)
                        it.start()
                    }
                }.getOrElse { return LoudnessResult.Unreadable("This device has no decoder for $mime.") }
            return drain(started, extractor, sourceFormat, isCancelled, onProgress)
        } finally {
            bestEffort(TAG, "decoder?.stop()") { decoder?.stop() }
            bestEffort(TAG, "decoder?.release()") { decoder?.release() }
            bestEffort(TAG, "extractor.release()") { extractor.release() }
        }
    }

    @Suppress("ReturnCount", "NestedBlockDepth", "LongMethod")
    private fun drain(
        decoder: MediaCodec,
        extractor: MediaExtractor,
        sourceFormat: MediaFormat,
        isCancelled: () -> Boolean,
        onProgress: (Float) -> Unit,
    ): LoudnessResult {
        val info = MediaCodec.BufferInfo()
        val totalUs = if (sourceFormat.containsKey(MediaFormat.KEY_DURATION)) sourceFormat.getLong(MediaFormat.KEY_DURATION) else 0L
        val scratch = Scratch()
        var analyser: LoudnessAnalyser? = null
        var extractorDone = false
        var decoderDone = false
        var stalled = 0
        while (!decoderDone) {
            if (isCancelled()) return LoudnessResult.Cancelled
            var progressed = false
            if (!extractorDone) {
                val inIndex = decoder.dequeueInputBuffer(DEQUEUE_TIMEOUT_US)
                if (inIndex >= 0) {
                    val input = decoder.getInputBuffer(inIndex) ?: return LoudnessResult.Unreadable(CODEC_STATE)
                    val size = extractor.readSampleData(input, 0)
                    if (size < 0) {
                        decoder.queueInputBuffer(inIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                        extractorDone = true
                    } else {
                        decoder.queueInputBuffer(inIndex, 0, size, extractor.sampleTime, 0)
                        if (totalUs > 0) onProgress((extractor.sampleTime.toFloat() / totalUs).coerceIn(0f, 1f))
                        extractor.advance()
                    }
                    progressed = true
                }
            }
            val outIndex = decoder.dequeueOutputBuffer(info, DEQUEUE_TIMEOUT_US)
            if (outIndex >= 0) {
                progressed = true
                if (info.size > 0) {
                    val format = decoder.outputFormat
                    val rate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                    val channels = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                    val existing = analyser
                    val target: LoudnessAnalyser
                    if (existing == null) {
                        if (!LoudnessAnalyser.supports(channels)) {
                            decoder.releaseOutputBuffer(outIndex, false)
                            return LoudnessResult.Unreadable(unsupportedLayout(channels))
                        }
                        target = LoudnessAnalyser(rate, channels)
                        analyser = target
                    } else {
                        // Splicing blocks measured at two different rates would quietly bias the gated
                        // average, and a wrong loudness number is worse than no number at all.
                        if (existing.sampleRate != rate || existing.channelCount != channels) {
                            decoder.releaseOutputBuffer(outIndex, false)
                            return LoudnessResult.Unreadable(FORMAT_CHANGED)
                        }
                        target = existing
                    }
                    val output = decoder.getOutputBuffer(outIndex) ?: return LoudnessResult.Unreadable(CODEC_STATE)
                    output.position(info.offset)
                    output.limit(info.offset + info.size)
                    when (val layout = pcmLayoutOf(format)) {
                        PcmLayout.Signed16 -> {
                            val values = scratch.ensure(info.size / Short.SIZE_BYTES)
                            target.feed(values, readSigned16(output, values))
                        }
                        PcmLayout.Float32 -> {
                            val values = scratch.ensure(info.size / Float.SIZE_BYTES)
                            target.feed(values, readFloat32(output, values))
                        }
                        is PcmLayout.Unsupported -> {
                            decoder.releaseOutputBuffer(outIndex, false)
                            return LoudnessResult.Unreadable(unsupportedPcm(layout.encoding))
                        }
                    }
                }
                decoder.releaseOutputBuffer(outIndex, false)
                if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) decoderDone = true
            } else if (outIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                progressed = true
            }
            if (progressed) {
                stalled = 0
            } else if (++stalled > STALL_LIMIT) {
                return LoudnessResult.Unreadable("The audio decoder stopped making progress on this file.")
            }
        }
        onProgress(1f)
        val finished = analyser ?: return LoudnessResult.Unreadable("The audio track decoded to nothing.")
        if (!finished.hasCompleteBlock) return LoudnessResult.TooShort
        return LoudnessResult.Measured(finished.finish())
    }

    /**
     * One growable PCM staging array for the whole decode. Reallocating per output buffer would
     * churn tens of thousands of arrays on a long file; this is the hot path the immutability rule
     * deliberately exempts.
     */
    private class Scratch {
        private var buffer = FloatArray(INITIAL_FLOATS)

        fun ensure(floats: Int): FloatArray {
            if (buffer.size < floats) buffer = FloatArray(floats)
            return buffer
        }

        private companion object {
            const val INITIAL_FLOATS = 16_384
        }
    }

    private sealed interface PcmLayout {
        data object Signed16 : PcmLayout

        data object Float32 : PcmLayout

        data class Unsupported(
            val encoding: Int,
        ) : PcmLayout
    }

    private companion object {
        const val DEQUEUE_TIMEOUT_US = 10_000L
        const val STALL_LIMIT = 1_000
        const val SHORT_SCALE = 32_768f
        const val CODEC_STATE = "The audio decoder returned no buffer, which means it has failed."
        const val FORMAT_CHANGED =
            "This file changes sample rate or channel count part-way through, so it cannot be measured in one pass."

        /**
         * Decoders are free to hand back 16-bit or float PCM, and newer ones may offer packed 24-bit.
         * Parse the raw constant into a closed set once, here, so the sample loop only ever sees a
         * layout it knows how to read rather than guessing at an unfamiliar integer.
         */
        fun pcmLayoutOf(format: MediaFormat): PcmLayout {
            val encoding =
                if (format.containsKey(MediaFormat.KEY_PCM_ENCODING)) {
                    format.getInteger(MediaFormat.KEY_PCM_ENCODING)
                } else {
                    AudioFormat.ENCODING_PCM_16BIT
                }
            return when (encoding) {
                AudioFormat.ENCODING_PCM_16BIT -> PcmLayout.Signed16
                AudioFormat.ENCODING_PCM_FLOAT -> PcmLayout.Float32
                else -> PcmLayout.Unsupported(encoding)
            }
        }

        fun readSigned16(
            buffer: ByteBuffer,
            into: FloatArray,
        ): Int {
            val shorts = buffer.order(ByteOrder.nativeOrder()).asShortBuffer()
            val count = minOf(shorts.remaining(), into.size)
            for (i in 0 until count) into[i] = shorts.get(i) / SHORT_SCALE
            return count
        }

        fun readFloat32(
            buffer: ByteBuffer,
            into: FloatArray,
        ): Int {
            val floats = buffer.order(ByteOrder.nativeOrder()).asFloatBuffer()
            val count = minOf(floats.remaining(), into.size)
            for (i in 0 until count) into[i] = floats.get(i)
            return count
        }

        fun unsupportedPcm(encoding: Int): String = "This device's decoder returned PCM Geode cannot measure (encoding $encoding)."

        fun unsupportedLayout(channels: Int): String =
            "BS.1770 defines channel weights for mono, stereo and 5.1; this file has $channels channels."

        fun Throwable.reason(): String = message ?: this::class.java.simpleName
    }
}

/**
 * ITU-R BS.1770-4 integrated loudness and true peak, fed a stream of interleaved float samples.
 *
 * Kept separate from [LoudnessMeter] so the measurement is pure arithmetic with no Android
 * dependency: it can be driven from a decoder, a render pass, or a synthetic signal.
 *
 * The chain per BS.1770:
 *  1. K-weighting — a high-shelf pre-filter then a high-pass, both derived here for the file's own
 *     sample rate rather than hard-coded at 48 kHz.
 *  2. 400 ms blocks with 75% overlap (a new block every 100 ms).
 *  3. Absolute gate at -70 LUFS, then a relative gate 10 LU under the mean of what survived.
 *
 * True peak is measured on the *unweighted* signal through a 4x oversampler, per Annex 2.
 */
class LoudnessAnalyser(
    val sampleRate: Int,
    val channelCount: Int,
) {
    init {
        require(sampleRate > 0) { "sampleRate must be positive, was $sampleRate" }
        require(supports(channelCount)) { "no BS.1770 channel weights for $channelCount channels" }
    }

    private val weights: DoubleArray = channelWeights(channelCount)

    /**
     * 100 ms of samples. At 44.1 kHz this rounds to 4410, so a "400 ms" block is exact there and
     * within one sample of exact at every other broadcast rate — far below the resolution of a
     * gated mean.
     */
    private val stepSamples: Int = (sampleRate / 10.0).roundToInt().coerceAtLeast(1)
    private val blockSamples: Int = stepSamples * STEPS_PER_BLOCK

    private val shelf: Biquad = shelfFilter(sampleRate)
    private val highPass: Biquad = highPassFilter(sampleRate)

    // Direct-form-I state, four doubles per channel: x[n-1], x[n-2], y[n-1], y[n-2].
    private val shelfState = DoubleArray(channelCount * BIQUAD_STATE)
    private val highPassState = DoubleArray(channelCount * BIQUAD_STATE)

    private val stepSumSquares = DoubleArray(channelCount)
    private var stepFilled = 0

    // The last four 100 ms sums per channel; a 400 ms block is their total, so the 75% overlap
    // costs one addition per block instead of re-summing 400 ms of samples four times over.
    private val stepRing = DoubleArray(channelCount * STEPS_PER_BLOCK)
    private var ringSlot = 0
    private var stepsClosed = 0

    private var blockLoudness = DoubleArray(INITIAL_BLOCKS)
    private var blockPower = DoubleArray(INITIAL_BLOCKS)
    private var blockCount = 0

    private val oversampler = TruePeakOversampler(channelCount)
    private var samplePeak = 0.0
    private var frames = 0L

    /** True once at least one full 400 ms block exists, i.e. once there is anything to gate. */
    val hasCompleteBlock: Boolean
        get() = blockCount > 0

    /**
     * Consumes [count] interleaved float samples (not frames) from [interleaved], each in -1..1.
     * The array is read, never retained.
     */
    fun feed(
        interleaved: FloatArray,
        count: Int,
    ) {
        val available = count / channelCount
        var offset = 0
        repeat(available) {
            for (channel in 0 until channelCount) {
                val x = interleaved[offset + channel].toDouble()
                val magnitude = abs(x)
                if (magnitude > samplePeak) samplePeak = magnitude
                val base = channel * BIQUAD_STATE
                val weighted = highPass.step(highPassState, base, shelf.step(shelfState, base, x))
                stepSumSquares[channel] += weighted * weighted
            }
            oversampler.push(interleaved, offset)
            offset += channelCount
            if (++stepFilled == stepSamples) closeStep()
        }
        frames += available
    }

    fun finish(): LoudnessReport {
        val gated = integrate()
        return LoudnessReport(
            integrated = gated.integrated,
            // The oversampler reconstructs between samples, but a stored sample is itself a peak the
            // reconstruction must clear, so the honest answer is whichever is larger.
            truePeakDbtp = decibels(maxOf(oversampler.peak, samplePeak)),
            samplePeakDbfs = decibels(samplePeak),
            sampleRate = sampleRate,
            channelCount = channelCount,
            measuredMs = frames * 1000L / sampleRate,
            gatedBlockCount = gated.blocks,
        )
    }

    private fun closeStep() {
        for (channel in 0 until channelCount) {
            stepRing[channel * STEPS_PER_BLOCK + ringSlot] = stepSumSquares[channel]
            stepSumSquares[channel] = 0.0
        }
        ringSlot = if (ringSlot + 1 == STEPS_PER_BLOCK) 0 else ringSlot + 1
        stepFilled = 0
        // A block only exists once four consecutive 100 ms steps are in the ring; the tail of the
        // file that cannot fill one is dropped, as the spec requires.
        if (++stepsClosed >= STEPS_PER_BLOCK) closeBlock()
    }

    private fun closeBlock() {
        var power = 0.0
        for (channel in 0 until channelCount) {
            val weight = weights[channel]
            if (weight == 0.0) continue
            var sum = 0.0
            val base = channel * STEPS_PER_BLOCK
            for (slot in 0 until STEPS_PER_BLOCK) sum += stepRing[base + slot]
            power += weight * (sum / blockSamples)
        }
        // Digital silence is minus infinity, which no gate can admit; nothing to record.
        if (power <= 0.0) return
        if (blockCount == blockLoudness.size) grow()
        blockLoudness[blockCount] = loudnessOf(power)
        blockPower[blockCount] = power
        blockCount++
    }

    private fun grow() {
        blockLoudness = blockLoudness.copyOf(blockLoudness.size * 2)
        blockPower = blockPower.copyOf(blockPower.size * 2)
    }

    @Suppress("ReturnCount")
    private fun integrate(): Gated {
        var absoluteSum = 0.0
        var absoluteCount = 0
        for (i in 0 until blockCount) {
            if (blockLoudness[i] > ABSOLUTE_GATE_LUFS) {
                absoluteSum += blockPower[i]
                absoluteCount++
            }
        }
        if (absoluteCount == 0) return Gated(IntegratedLoudness.BelowGate, 0)
        // The relative gate is set from the mean power of everything that cleared -70 LUFS, then
        // dropped 10 LU; that is what stops long fades and room tone from dragging the answer down.
        val relativeGate = loudnessOf(absoluteSum / absoluteCount) - RELATIVE_GATE_LU
        var sum = 0.0
        var count = 0
        for (i in 0 until blockCount) {
            val loudness = blockLoudness[i]
            if (loudness > ABSOLUTE_GATE_LUFS && loudness > relativeGate) {
                sum += blockPower[i]
                count++
            }
        }
        if (count == 0) return Gated(IntegratedLoudness.BelowGate, 0)
        return Gated(IntegratedLoudness.Lufs(loudnessOf(sum / count)), count)
    }

    private class Gated(
        val integrated: IntegratedLoudness,
        val blocks: Int,
    )

    private class Biquad(
        private val b0: Double,
        private val b1: Double,
        private val b2: Double,
        private val a1: Double,
        private val a2: Double,
    ) {
        fun step(
            state: DoubleArray,
            base: Int,
            x: Double,
        ): Double {
            val y = b0 * x + b1 * state[base] + b2 * state[base + 1] - a1 * state[base + 2] - a2 * state[base + 3]
            state[base + 1] = state[base]
            state[base] = x
            state[base + 3] = state[base + 2]
            state[base + 2] = y
            return y
        }
    }

    /**
     * 4x polyphase oversampler for true-peak metering.
     *
     * BS.1770-4 Annex 2 prints a 48-tap coefficient table; rather than copy it, the phase filters
     * are generated from the design it describes — a sinc low-pass at fs/4, 49 taps, Hann windowed,
     * split into four polyphase branches. That construction (the same one libebur128 uses) matches
     * the table's response and stays auditable instead of being a wall of magic numbers.
     *
     * The spec's 12.04 dB pre-attenuation exists so a fixed-point implementation cannot overflow
     * inside the interpolator; in double precision it would only be undone again, so it is omitted.
     */
    private class TruePeakOversampler(
        private val channelCount: Int,
    ) {
        var peak: Double = 0.0
            private set

        private val phases: Array<DoubleArray> = buildPhases()

        /**
         * Each sample is stored twice, [DELAY] apart, so the tap loop can walk straight backwards
         * from the write cursor without a wrap test per tap.
         */
        private val history = DoubleArray(channelCount * DELAY * 2)
        private var cursor = 0

        fun push(
            interleaved: FloatArray,
            offset: Int,
        ) {
            val slot = cursor
            for (channel in 0 until channelCount) {
                val base = channel * DELAY * 2
                val x = interleaved[offset + channel].toDouble()
                history[base + slot] = x
                history[base + slot + DELAY] = x
                val newest = base + slot + DELAY
                for (phase in phases) {
                    var acc = 0.0
                    for (tap in phase.indices) acc += phase[tap] * history[newest - tap]
                    val magnitude = abs(acc)
                    if (magnitude > peak) peak = magnitude
                }
            }
            cursor = if (slot + 1 == DELAY) 0 else slot + 1
        }

        private companion object {
            const val TAPS = 49
            const val FACTOR = 4
            const val DELAY = (TAPS + FACTOR - 1) / FACTOR
            const val NEAR_ZERO = 1e-12

            fun buildPhases(): Array<DoubleArray> {
                val phases = Array(FACTOR) { DoubleArray(DELAY) }
                for (tap in 0 until TAPS) {
                    val centred = tap - (TAPS - 1) / 2.0
                    val x = centred * PI / FACTOR
                    val sinc = if (abs(x) < NEAR_ZERO) 1.0 else sin(x) / x
                    val hann = 0.5 * (1.0 - cos(2.0 * PI * tap / (TAPS - 1)))
                    phases[tap % FACTOR][tap / FACTOR] = sinc * hann
                }
                return phases
            }
        }
    }

    companion object {
        /** BS.1770 defines channel weights for mono, stereo and 5.1 only. */
        fun supports(channelCount: Int): Boolean = channelCount == 1 || channelCount == 2 || channelCount == SURROUND_CHANNELS

        private const val STEPS_PER_BLOCK = 4
        private const val BIQUAD_STATE = 4
        private const val INITIAL_BLOCKS = 1_024
        private const val SURROUND_CHANNELS = 6

        /** The -0.691 dB offset that puts a 1 kHz sine at -3.01 dBFS on 0 LKFS. */
        private const val LOUDNESS_OFFSET = -0.691

        private const val ABSOLUTE_GATE_LUFS = -70.0
        private const val RELATIVE_GATE_LU = 10.0

        /** +1.5 dB on the surrounds, i.e. 10^(1.5/20). */
        private const val SURROUND_WEIGHT = 1.41

        private const val SILENCE_DB = -120.0

        /**
         * Android interleaves in channel-mask order — FL, FR, FC, LFE, BL, BR — which is exactly the
         * order BS.1770 weights. The LFE is excluded outright, as the spec requires.
         */
        private fun channelWeights(channelCount: Int): DoubleArray =
            when (channelCount) {
                SURROUND_CHANNELS -> doubleArrayOf(1.0, 1.0, 1.0, 0.0, SURROUND_WEIGHT, SURROUND_WEIGHT)
                else -> DoubleArray(channelCount) { 1.0 }
            }

        private fun loudnessOf(power: Double): Double = LOUDNESS_OFFSET + 10.0 * log10(power)

        private fun decibels(linear: Double): Double = if (linear <= 0.0) SILENCE_DB else 20.0 * log10(linear)

        /**
         * Stage 1 of K-weighting: a +4 dB high shelf at ~1681 Hz standing in for the head's
         * acoustic effect. Designed from the analog prototype by bilinear transform so it is right
         * at 44.1 kHz too, not only at the 48 kHz the spec tabulates.
         */
        private fun shelfFilter(sampleRate: Int): Biquad {
            val k = tan(PI * SHELF_HZ / sampleRate)
            val vh = 10.0.pow(SHELF_GAIN_DB / 20.0)
            val vb = vh.pow(SHELF_BAND_EXPONENT)
            val kk = k * k
            val a0 = 1.0 + k / SHELF_Q + kk
            return Biquad(
                b0 = (vh + vb * k / SHELF_Q + kk) / a0,
                b1 = 2.0 * (kk - vh) / a0,
                b2 = (vh - vb * k / SHELF_Q + kk) / a0,
                a1 = 2.0 * (kk - 1.0) / a0,
                a2 = (1.0 - k / SHELF_Q + kk) / a0,
            )
        }

        /** Stage 2 of K-weighting: the RLB high-pass at ~38 Hz that discards inaudible rumble. */
        private fun highPassFilter(sampleRate: Int): Biquad {
            val k = tan(PI * HIGH_PASS_HZ / sampleRate)
            val kk = k * k
            val a0 = 1.0 + k / HIGH_PASS_Q + kk
            return Biquad(
                b0 = 1.0,
                b1 = -2.0,
                b2 = 1.0,
                a1 = 2.0 * (kk - 1.0) / a0,
                a2 = (1.0 - k / HIGH_PASS_Q + kk) / a0,
            )
        }

        private const val SHELF_HZ = 1681.974450955533
        private const val SHELF_GAIN_DB = 3.999843853973347
        private const val SHELF_Q = 0.7071752369554196
        private const val SHELF_BAND_EXPONENT = 0.4996667741545416
        private const val HIGH_PASS_HZ = 38.13547087602444
        private const val HIGH_PASS_Q = 0.5003270373238773
    }
}

private const val TAG = "LoudnessMeter"
