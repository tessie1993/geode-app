package dev.geode.analysis

import android.content.Context
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import dev.geode.engine.audio.AnalysisFft
import dev.geode.engine.audio.ReactiveAnalyzer
import dev.geode.util.bestEffort
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import java.nio.ByteOrder
import kotlin.math.roundToLong

class OfflineAnalyzer(
    private val context: Context,
) {
    suspend fun analyze(
        uri: Uri,
        beatSensitivity: Float = BeatTuning.SENSITIVITY_DEFAULT,
        beatMinIntervalMs: Float = BeatTuning.INTERVAL_MS_DEFAULT,
        onProgress: (Float) -> Unit = {},
    ): FeatureTimeline =
        withContext(Dispatchers.Default) {
            // The blocking body has no suspension point of its own, so without this
            // the decode runs to completion after the caller is cancelled - holding a
            // hardware MediaCodec the whole time.
            analyzeBlocking(uri, beatSensitivity, beatMinIntervalMs, onProgress) { isActive }
        }

    /**
     * [stillWanted] is polled once per decoded buffer; returning false aborts the run with a
     * [CancellationException], so the finally blocks below still release the codec and the
     * extractor. It defaults to "always", which is what a caller outside a coroutine wants.
     */
    fun analyzeBlocking(
        uri: Uri,
        beatSensitivity: Float = BeatTuning.SENSITIVITY_DEFAULT,
        beatMinIntervalMs: Float = BeatTuning.INTERVAL_MS_DEFAULT,
        onProgress: (Float) -> Unit = {},
        stillWanted: () -> Boolean = { true },
    ): FeatureTimeline {
        dev.geode.audio.AiffPcm.open(context, uri)?.let { aiff ->
            val pipeline = StreamingPipeline(beatSensitivity, beatMinIntervalMs)
            try {
                val buf = ShortArray(16384)
                var last = 0f
                while (true) {
                    if (!stillWanted()) throw CancellationException("analysis cancelled")
                    val n = aiff.read(buf)
                    if (n <= 0) break
                    pipeline.feed(java.nio.ShortBuffer.wrap(buf, 0, n), aiff.channels, aiff.sampleRate)
                    if (aiff.progress - last > 0.01f) {
                        last = aiff.progress
                        onProgress(last)
                    }
                }
                return pipeline.finish()
            } finally {
                pipeline.close()
                aiff.close()
            }
        }
        val extractor = MediaExtractor()
        var codecRef: MediaCodec? = null
        val pipeline = StreamingPipeline(beatSensitivity, beatMinIntervalMs)
        // pipeline.use ensures the native analysis handle is released on every exit path
        // (normal completion, "No audio track in file", a codec error, or cancellation),
        // not only on the success path.
        return pipeline.use { streaming ->
            val info = MediaCodec.BufferInfo()
            var inputDone = false
            var outputDone = false
            var lastProgress = 0f
            try {
                extractor.setDataSource(context, uri, null)
                val trackIndex =
                    (0 until extractor.trackCount).firstOrNull { idx ->
                        extractor.getTrackFormat(idx).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
                    } ?: throw IllegalArgumentException("No audio track in file")
                val format = extractor.getTrackFormat(trackIndex)
                extractor.selectTrack(trackIndex)
                val durationUs =
                    if (format.containsKey(MediaFormat.KEY_DURATION)) format.getLong(MediaFormat.KEY_DURATION) else 0L
                val mime = requireNotNull(format.getString(MediaFormat.KEY_MIME))
                val codec = MediaCodec.createDecoderByType(mime).also { codecRef = it }
                codec.configure(format, null, null, 0)
                codec.start()
                while (!outputDone) {
                    if (!stillWanted()) throw CancellationException("analysis cancelled")
                    if (!inputDone) {
                        val inIndex = codec.dequeueInputBuffer(10_000)
                        if (inIndex >= 0) {
                            val buf = checkNotNull(codec.getInputBuffer(inIndex)) { "decoder input buffer null (codec error state)" }
                            val size = extractor.readSampleData(buf, 0)
                            if (size < 0) {
                                codec.queueInputBuffer(inIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                                inputDone = true
                            } else {
                                codec.queueInputBuffer(inIndex, 0, size, extractor.sampleTime, 0)
                                if (durationUs > 0) {
                                    val p = (extractor.sampleTime / durationUs.toFloat()).coerceIn(0f, 1f)
                                    if (p - lastProgress > 0.01f) {
                                        lastProgress = p
                                        onProgress(p)
                                    }
                                }
                                extractor.advance()
                            }
                        }
                    }
                    val outIndex = codec.dequeueOutputBuffer(info, 10_000)
                    if (outIndex >= 0) {
                        if (info.size > 0) {
                            val outFormat = codec.outputFormat
                            val sampleRate = outFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                            val channels = outFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                            val buf = checkNotNull(codec.getOutputBuffer(outIndex)) { "decoder output buffer null (codec error state)" }
                            buf.position(info.offset)
                            buf.limit(info.offset + info.size)
                            val pcmEncoding =
                                if (outFormat.containsKey(MediaFormat.KEY_PCM_ENCODING)) {
                                    outFormat.getInteger(MediaFormat.KEY_PCM_ENCODING)
                                } else {
                                    android.media.AudioFormat.ENCODING_PCM_16BIT
                                }
                            if (pcmEncoding == android.media.AudioFormat.ENCODING_PCM_FLOAT) {
                                pipeline.feedFloat(buf.order(ByteOrder.nativeOrder()).asFloatBuffer(), channels, sampleRate)
                            } else {
                                pipeline.feed(buf.order(ByteOrder.LITTLE_ENDIAN).asShortBuffer(), channels, sampleRate)
                            }
                        }
                        codec.releaseOutputBuffer(outIndex, false)
                        if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) outputDone = true
                    }
                }
            } finally {
                codecRef?.let {
                    bestEffort(TAG, "it.stop()") { it.stop() }
                    it.release()
                }
                extractor.release()
            }
            onProgress(1f)
            streaming.finish()
        }
    }

    internal class StreamingPipeline(
        private val sigma: Float,
        private val minIntervalMs: Float,
    ) : AutoCloseable {
        // Built on the first decoded buffer, once the stream's rate is known: the FFT size follows
        // the rate (AnalysisFft.sizeFor), so a 96 kHz file gets the same band edges as a 48 kHz one,
        // and the hop follows the FFT, so the frame rate is the stream's too.
        private var analyzerOrNull: ReactiveAnalyzer? = null
        private val analyzer: ReactiveAnalyzer get() = checkNotNull(analyzerOrNull) { "no audio fed yet" }

        private val frames = FrameAccumulator()
        private var scratch = FloatArray(AnalysisFft.REFERENCE_FFT_SIZE * 4)
        private var sampleRate = 0
        private var hopSamples = 0

        // The first rate's frame rate. A timeline has one; a mid-stream rate change (rare: chained
        // streams) keeps its timestamps right but its frames come a little faster or slower.
        private var hopRateHz = 0f

        // Frame times are originMs plus the samples pulled since the last rate change, so a stream
        // that changes rate mid-way keeps one continuous clock.
        private var originMs = 0L
        private var absSample = 0L

        fun feed(
            pcm: java.nio.ShortBuffer,
            channels: Int,
            sampleRateHz: Int,
        ) {
            if (channels <= 0 || sampleRateHz <= 0) return
            adoptSampleRate(sampleRateHz)
            val n = pcm.remaining()
            if (scratch.size < n) scratch = FloatArray(n.coerceAtLeast(scratch.size * 2))
            for (i in 0 until n) scratch[i] = pcm.get(pcm.position() + i) / 32768f
            pushAndDrain(n / channels, channels)
        }

        fun feedFloat(
            pcm: java.nio.FloatBuffer,
            channels: Int,
            sampleRateHz: Int,
        ) {
            if (channels <= 0 || sampleRateHz <= 0) return
            adoptSampleRate(sampleRateHz)
            val n = pcm.remaining()
            if (scratch.size < n) scratch = FloatArray(n.coerceAtLeast(scratch.size * 2))
            pcm.duplicate().get(scratch, 0, n)
            pushAndDrain(n / channels, channels)
        }

        /**
         * The native session buffers at most a few FFT windows and drops the oldest samples past
         * that, which would slip every later frame's timestamp. A decoder buffer larger than one
         * window (big FLAC blocks) is therefore pushed a window at a time, draining in between.
         */
        private fun pushAndDrain(
            frameCount: Int,
            channels: Int,
        ) {
            val window = analyzer.fftSize
            if (frameCount <= window) {
                analyzer.push(scratch, frameCount, channels)
                drain()
                return
            }
            var at = 0
            while (at < frameCount) {
                val n = minOf(window, frameCount - at)
                analyzer.push(scratch.copyOfRange(at * channels, (at + n) * channels), n, channels)
                drain()
                at += n
            }
        }

        private fun adoptSampleRate(sampleRateHz: Int) {
            if (sampleRateHz == sampleRate) return
            // A stream that changes rate mid-way keeps one continuous clock, but gets a fresh
            // analyser: the FFT, the hop and every time constant inside follow the rate.
            if (sampleRate > 0) originMs += AnalysisFft.sampleToMs(absSample, sampleRate)
            absSample = 0L
            analyzerOrNull?.close()
            val fresh =
                ReactiveAnalyzer(sampleRateHz = sampleRateHz, bandCount = AnalysisEngine.DEFAULT_BAND_COUNT).also {
                    it.sensitivity = BeatTuning.clampSensitivity(sigma)
                    it.refractoryMs = BeatTuning.clampIntervalMs(minIntervalMs)
                    it.attackSeconds = BeatTuning.envelopeSeconds(AnalysisEngine.DEFAULT_ATTACK)
                    it.releaseSeconds = BeatTuning.envelopeSeconds(AnalysisEngine.DEFAULT_DECAY)
                }
            analyzerOrNull = fresh
            if (hopRateHz == 0f) hopRateHz = fresh.hopRateHz
            sampleRate = sampleRateHz
            hopSamples = fresh.hopSamples
        }

        private fun drain() {
            while (analyzer.pull()) {
                // Stamped at the window's end, which is when live analysis has the same frame: live
                // cannot see a window until its last sample has arrived. Stamping the start made
                // exports react one window (43 ms) ahead of the screen.
                val timeMs = originMs + AnalysisFft.sampleToMs(absSample + analyzer.fftSize, sampleRate)
                frames.add(TimelineFrame(timeMs, snapshot()))
                absSample += hopSamples
            }
        }

        private fun snapshot(): AudioFeatures =
            AudioFeatures(
                bands = analyzer.bands.copyOf(),
                waveform = analyzer.waveform.copyOf(),
                rms = analyzer.rms,
                bass = analyzer.bass,
                mid = analyzer.mid,
                treble = analyzer.treble,
                onset = analyzer.onset,
                beat = analyzer.beat,
                bpm = analyzer.bpm,
                centroid = analyzer.centroid,
                flux = analyzer.fluxValue,
                beatStrength = analyzer.beatStrength,
                transient = analyzer.transient,
                beatPhase = analyzer.beatPhase,
                pulseConfidence = analyzer.pulseConfidence,
                macroEnergy = analyzer.macroEnergy,
                kick = analyzer.kick,
                snare = analyzer.snare,
                hat = analyzer.hat,
                chroma = analyzer.chroma.copyOf(),
                chromaConfidence = analyzer.chromaConfidence,
                stereoWidth = analyzer.stereoWidth,
                stereoCorrelation = analyzer.stereoCorrelation,
                stereoPan = analyzer.stereoPan,
                tempoStability = analyzer.tempoStability,
                barPhase = analyzer.barPhase,
                beatInBar = analyzer.beatInBar,
                downbeat = analyzer.downbeat,
                downbeatConfidence = analyzer.downbeatConfidence,
                novelty = analyzer.novelty,
                sectionBoundary = analyzer.sectionBoundary,
                buildup = analyzer.buildup,
                drop = analyzer.drop,
                arrival = analyzer.arrival,
                harmonicity = analyzer.harmonicity,
                warmup = analyzer.warmup,
            )

        fun finish(): FeatureTimeline {
            val out = frames.finish()
            val group = frames.groupSize
            val frameRate = if (hopRateHz > 0f) hopRateHz / group else 0f
            return FeatureTimeline(
                out,
                hopMs = if (frameRate > 0f) (1000f / frameRate).roundToLong() else 0L,
                key = analyzerOrNull?.key().orEmpty(),
                hopRateHz = frameRate,
                sampleRateHz = if (sampleRate > 0) sampleRate else FeatureTimeline.DEFAULT_SAMPLE_RATE_HZ,
            )
        }

        override fun close() {
            analyzerOrNull?.close()
        }
    }
}

private const val TAG = "OfflineAnalyzer"
