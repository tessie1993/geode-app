package dev.geode.analysis

import android.content.Context
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import dev.geode.engine.audio.ReactiveAnalyzer
import dev.geode.util.bestEffort
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import java.nio.ByteOrder

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
        val info = MediaCodec.BufferInfo()
        var inputDone = false
        var outputDone = false
        var lastProgress = 0f
        try {
            extractor.setDataSource(context, uri, null)
            val trackIndex =
                (0 until extractor.trackCount).firstOrNull {
                    extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
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
        return pipeline.use { it.finish() }
    }

    internal class StreamingPipeline(
        sigma: Float,
        minIntervalMs: Float,
    ) : AutoCloseable {
        private val analyzer =
            ReactiveAnalyzer(
                bandCount = AnalysisEngine.DEFAULT_BAND_COUNT,
                fftSize = AnalysisEngine.DEFAULT_FFT_SIZE,
                hopRateHz = HOP_RATE_HZ,
            ).also {
                it.sensitivity = BeatTuning.clampSensitivity(sigma)
                it.refractoryMs = BeatTuning.clampIntervalMs(minIntervalMs)
                it.attackSeconds = BeatTuning.envelopeSeconds(AnalysisEngine.DEFAULT_ATTACK)
                it.releaseSeconds = BeatTuning.envelopeSeconds(AnalysisEngine.DEFAULT_DECAY)
            }

        private val frames = FrameAccumulator()
        private var scratch = FloatArray(AnalysisEngine.DEFAULT_FFT_SIZE * 4)
        private var sampleRate = 44100
        private var hopSamples = sampleRate / 60

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
            analyzer.push(scratch, n / channels, channels)
            drain()
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
            analyzer.push(scratch, n / channels, channels)
            drain()
        }

        private fun adoptSampleRate(sampleRateHz: Int) {
            if (sampleRateHz != sampleRate) {
                sampleRate = sampleRateHz
                hopSamples = (sampleRate / 60).coerceAtLeast(1)
            }
            analyzer.sampleRateHz = sampleRate
        }

        private fun drain() {
            while (analyzer.pull()) {
                val timeMs = absSample * 1000L / sampleRate
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
            )

        fun finish(): FeatureTimeline {
            val out = frames.finish()
            val group = frames.groupSize
            return FeatureTimeline(
                out,
                hopMs = group * 1000L / 60,
                key = analyzer.key(),
                hopRateHz = HOP_RATE_HZ / group,
            )
        }

        override fun close() {
            analyzer.close()
        }

        private companion object {
            const val HOP_RATE_HZ = OFFLINE_HOP_RATE_HZ
        }
    }

    companion object {
        const val OFFLINE_HOP_RATE_HZ = 60f
    }
}

private const val TAG = "OfflineAnalyzer"
