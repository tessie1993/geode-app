package dev.geode.export

import android.content.Context
import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

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
        try {
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
        } finally {
            analyser?.close()
        }
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
 * The measurement is native (`core/audio/dsp/Loudness`, tested by `core/tests/loudness_test.cpp`):
 * K-weighting, 400 ms blocks with 75% overlap, the -70 LUFS absolute and -10 LU relative gates, and
 * a 4x oversampled true peak. This class owns the native handle, so [close] it when done.
 */
class LoudnessAnalyser(
    val sampleRate: Int,
    val channelCount: Int,
) : AutoCloseable {
    init {
        require(sampleRate > 0) { "sampleRate must be positive, was $sampleRate" }
        require(supports(channelCount)) { "no BS.1770 channel weights for $channelCount channels" }
    }

    private var handle: Long =
        GeodeNative.loudnessCreate(sampleRate, channelCount).also {
            check(it != 0L) { "native loudness meter refused $sampleRate Hz, $channelCount channels" }
        }

    /** True once at least one full 400 ms block exists, i.e. once there is anything to gate. */
    val hasCompleteBlock: Boolean
        get() = GeodeNative.loudnessHasBlock(live())

    /**
     * Consumes [count] interleaved float samples (not frames) from [interleaved], each in -1..1.
     * The array is read, never retained.
     */
    fun feed(
        interleaved: FloatArray,
        count: Int,
    ) = GeodeNative.loudnessFeed(live(), interleaved, count)

    fun finish(): LoudnessReport {
        val out = DoubleArray(REPORT_DOUBLES)
        val gated = GeodeNative.loudnessFinish(live(), out)
        return LoudnessReport(
            integrated = if (gated) IntegratedLoudness.Lufs(out[0]) else IntegratedLoudness.BelowGate,
            truePeakDbtp = out[1],
            samplePeakDbfs = out[2],
            sampleRate = sampleRate,
            channelCount = channelCount,
            measuredMs = out[3].toLong(),
            gatedBlockCount = out[4].toInt(),
        )
    }

    /** Idempotent. */
    override fun close() {
        val owned = handle
        handle = 0L
        if (owned != 0L) GeodeNative.loudnessDestroy(owned)
    }

    private fun live(): Long = handle.also { check(it != 0L) { "LoudnessAnalyser used after close()" } }

    companion object {
        /** BS.1770 defines channel weights for mono, stereo and 5.1 only. */
        fun supports(channelCount: Int): Boolean = GeodeNative.loudnessSupports(channelCount)

        private const val REPORT_DOUBLES = 5
    }
}

private const val TAG = "LoudnessMeter"
