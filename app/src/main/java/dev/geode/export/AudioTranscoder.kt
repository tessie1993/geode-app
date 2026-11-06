package dev.geode.export

import android.content.Context
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer

class AudioTranscoder(
    private val context: Context,
) {
    class Result(
        val format: MediaFormat,
        val file: File,
        val sampleInfos: List<SampleInfo>,
    ) {
        val durationUs: Long =
            sampleInfos.lastOrNull()?.let { last ->
                // One AAC-LC frame past the last, at the encoder's actual output rate (core/audio/dsp/PcmTiming).
                GeodeNative.aacEndUs(last.presentationTimeUs, format.getInteger(MediaFormat.KEY_SAMPLE_RATE))
            } ?: 0L

        fun release() {
            bestEffort(TAG, "file.delete()") { file.delete() }
        }
    }

    class SampleInfo(
        val offset: Long,
        val size: Int,
        val presentationTimeUs: Long,
        val flags: Int,
    )

    /**
     * Interleaved s16 from [srcCh] down to [dstCh] channels: a 5.1 or 7.1 fold (the rest's mean, halved,
     * on each side) or a mono mean. Native (`core/audio/dsp/PcmConvert`, tested by
     * `core/tests/clip_media_test.cpp`); this only moves the samples in and out of the buffers.
     */
    private fun downmix(
        src: ByteBuffer,
        srcCh: Int,
        dstCh: Int,
    ): ByteBuffer {
        val sb = src.duplicate().order(java.nio.ByteOrder.nativeOrder()).asShortBuffer()
        val frames = sb.remaining() / srcCh
        val input = ShortArray(frames * srcCh).also { sb.get(it) }
        return shortsToBuffer(GeodeNative.pcmDownmixS16(input, frames, srcCh, dstCh))
    }

    private fun shortsToBuffer(samples: ShortArray): ByteBuffer {
        val out = ByteBuffer.allocate(samples.size * 2).order(java.nio.ByteOrder.nativeOrder())
        out.asShortBuffer().put(samples)
        out.limit(samples.size * 2)
        return out
    }

    /** Native-order 16-bit PCM through [resampler]; passthrough returns [pcm] untouched. */
    private fun resample(
        pcm: ByteBuffer,
        resampler: PcmResampler,
        channels: Int,
    ): ByteBuffer {
        if (resampler.isPassthrough) return pcm
        val sb = pcm.duplicate().order(java.nio.ByteOrder.nativeOrder()).asShortBuffer()
        val src = ShortArray(sb.remaining())
        sb.get(src)
        val converted = resampler.process(src, src.size / channels)
        val out = ByteBuffer.allocate(converted.size * 2).order(java.nio.ByteOrder.nativeOrder())
        out.asShortBuffer().put(converted)
        return out
    }

    private fun transcodeAiff(
        aiff: dev.geode.audio.AiffPcm,
        maxDurationMs: Long,
        startMs: Long,
        outputRateHz: Int?,
        isCancelled: () -> Boolean,
        onProgress: (Float) -> Unit,
    ): Result {
        val channels = aiff.channels.coerceAtMost(2)
        // Two rates from here on: the AIFF's own (source frames: the start skip) and the
        // encoder's (resampled bytes: every presentation timestamp handed to the encoder).
        val sourceRateHz = aiff.sampleRate
        val encoderRateHz = outputRateHz ?: GeodeNative.aacRateFor(sourceRateHz)
        val encFormat =
            MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, encoderRateHz, channels).apply {
                setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
                setInteger(MediaFormat.KEY_BIT_RATE, 192_000)
                setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 65536)
            }
        val encoder: MediaCodec
        val outFile: File
        val out: BufferedOutputStream
        var encoderRef: MediaCodec? = null
        var outFileRef: File? = null
        try {
            encoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC).also { encoderRef = it }
            encoder.configure(encFormat, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            encoder.start()
            outFile = File.createTempFile("geode_aac_", ".bin", context.cacheDir).also { outFileRef = it }
            out = BufferedOutputStream(FileOutputStream(outFile))
        } catch (t: Throwable) {
            bestEffort(TAG, "encoderRef?.release()") { encoderRef?.release() }
            bestEffort(TAG, "outFileRef?.delete()") { outFileRef?.delete() }
            bestEffort(TAG, "aiff.close()") { aiff.close() }
            throw t
        }
        var outBytes = 0L
        val infos = mutableListOf<SampleInfo>()
        var outFormat: MediaFormat? = null
        val maxUs = maxDurationMs * 1000
        val encInfo = MediaCodec.BufferInfo()
        val readBuf = ShortArray(16384 - (16384 % aiff.channels))
        var srcDone = false
        var eosSent = false
        var encoderDone = false
        var pcmCarry: ByteBuffer? = null
        var carryTimeUs = 0L
        var fedBytes = 0L
        var progressed = false
        var stallIterations = 0
        if (startMs > 0) {
            var toSkip = GeodeNative.pcmSamplesBeforeUs(startMs * 1000, sourceRateHz, aiff.channels)
            while (toSkip > 0) {
                val want = minOf(toSkip, readBuf.size.toLong()).toInt()
                val n = aiff.read(if (want == readBuf.size) readBuf else ShortArray(want))
                if (n <= 0) break
                toSkip -= n
            }
        }
        // Owns a native handle: created inside the try so every exit, cancellation included, closes it.
        var resamplerRef: PcmResampler? = null
        try {
            val resampler = PcmResampler(sourceRateHz, encoderRateHz, channels).also { resamplerRef = it }
            while (!encoderDone) {
                if (isCancelled()) throw kotlinx.coroutines.CancellationException("Export cancelled")
                progressed = false
                if (pcmCarry == null && !srcDone) {
                    val n = aiff.read(readBuf)
                    if (n <= 0 || (maxUs > 0 && carryTimeUs > maxUs)) {
                        srcDone = true
                    } else {
                        val frames = n / aiff.channels
                        val bb =
                            if (aiff.channels <= 2) {
                                shortsToBuffer(readBuf.copyOf(n))
                            } else {
                                shortsToBuffer(GeodeNative.pcmDownmixS16(readBuf, frames, aiff.channels, channels))
                            }
                        pcmCarry = resample(bb, resampler, channels)
                        onProgress(aiff.progress)
                    }
                    progressed = true
                }
                val carry = pcmCarry
                if (carry != null) {
                    val inIndex = encoder.dequeueInputBuffer(10_000)
                    if (inIndex >= 0) {
                        val inBuf = checkNotNull(encoder.getInputBuffer(inIndex)) { "encoder input buffer null (codec error state)" }
                        val toWrite = minOf(inBuf.remaining(), carry.remaining())
                        val slice = carry.duplicate().apply { limit(position() + toWrite) }
                        inBuf.put(slice)
                        encoder.queueInputBuffer(inIndex, 0, toWrite, carryTimeUs, 0)
                        fedBytes += toWrite
                        carryTimeUs = GeodeNative.pcmS16BytesToUs(fedBytes, encoderRateHz, channels)
                        carry.position(carry.position() + toWrite)
                        if (!carry.hasRemaining()) pcmCarry = null
                        progressed = true
                    }
                } else if (srcDone && !eosSent) {
                    val inIndex = encoder.dequeueInputBuffer(10_000)
                    if (inIndex >= 0) {
                        encoder.queueInputBuffer(inIndex, 0, 0, carryTimeUs, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                        eosSent = true
                        progressed = true
                    }
                }
                while (true) {
                    val outIndex = encoder.dequeueOutputBuffer(encInfo, if (eosSent) 10_000 else 0)
                    if (outIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                        outFormat = encoder.outputFormat
                        progressed = true
                        continue
                    }
                    if (outIndex >= 0) {
                        progressed = true
                        if (encInfo.size > 0 && encInfo.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG == 0) {
                            val buf = checkNotNull(encoder.getOutputBuffer(outIndex)) { "encoder output buffer null (codec error state)" }
                            buf.position(encInfo.offset)
                            buf.limit(encInfo.offset + encInfo.size)
                            val bytes = ByteArray(encInfo.size)
                            buf.get(bytes)
                            infos += SampleInfo(outBytes, encInfo.size, encInfo.presentationTimeUs, encInfo.flags)
                            out.write(bytes)
                            outBytes += encInfo.size
                        }
                        val eos = encInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0
                        encoder.releaseOutputBuffer(outIndex, false)
                        if (eos) {
                            encoderDone = true
                            break
                        }
                    } else {
                        break
                    }
                }
                if (progressed) {
                    stallIterations = 0
                } else if (++stallIterations > STALL_LIMIT) {
                    throw IllegalStateException("Audio transcode stalled (codec made no progress)")
                }
            }
            out.flush()
            return Result(requireNotNull(outFormat) { "AAC encoder produced no format" }, outFile, infos)
        } catch (t: Throwable) {
            bestEffort(TAG, "out.close()") { out.close() }
            bestEffort(TAG, "outFile.delete()") { outFile.delete() }
            throw t
        } finally {
            bestEffort(TAG, "out.close()") { out.close() }
            bestEffort(TAG, "encoder.stop()") { encoder.stop() }
            bestEffort(TAG, "encoder.release()") { encoder.release() }
            bestEffort(TAG, "aiff.close()") { aiff.close() }
            bestEffort(TAG, "resamplerRef?.close()") { resamplerRef?.close() }
        }
    }

    /**
     * [outputRateHz] forces the AAC rate (a mix needs every clip at one rate); null picks
     * [GeodeNative.aacRateFor] the source, which keeps it unless it is above what AAC encoders
     * accept. Hi-res sources used to be handed to the encoder at their own 88.2-192 kHz.
     */
    fun transcode(
        uri: Uri,
        maxDurationMs: Long,
        startMs: Long = 0L,
        outputRateHz: Int? = null,
        isCancelled: () -> Boolean = { false },
        onProgress: (Float) -> Unit = {},
    ): Result {
        dev.geode.audio.AiffPcm.open(context, uri)?.let { aiff ->
            return transcodeAiff(aiff, maxDurationMs, startMs, outputRateHz, isCancelled, onProgress)
        }
        val extractor = MediaExtractor()
        val srcFormat: MediaFormat
        var sourceRateHz: Int
        var srcChannels: Int
        var channels: Int
        val decoder: MediaCodec
        var encoder: MediaCodec? = null
        var resampler: PcmResampler? = null
        val outFile: File
        val out: BufferedOutputStream
        var decoderRef: MediaCodec? = null
        var encoderRef: MediaCodec? = null
        var outFileRef: File? = null
        try {
            extractor.setDataSource(context, uri, null)
            val trackIndex =
                (0 until extractor.trackCount).firstOrNull {
                    extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
                } ?: throw IllegalArgumentException("No audio track in source file")
            srcFormat = extractor.getTrackFormat(trackIndex)
            extractor.selectTrack(trackIndex)
            if (startMs > 0) extractor.seekTo(startMs * 1000, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
            val mime = requireNotNull(srcFormat.getString(MediaFormat.KEY_MIME))
            sourceRateHz = srcFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE)
            srcChannels = srcFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
            channels = srcChannels.coerceAtMost(2)

            decoder = MediaCodec.createDecoderByType(mime).also { decoderRef = it }
            decoder.configure(srcFormat, null, null, 0)
            decoder.start()

            outFile = File.createTempFile("geode_aac_", ".bin", context.cacheDir).also { outFileRef = it }
            out = BufferedOutputStream(FileOutputStream(outFile))
        } catch (t: Throwable) {
            bestEffort(TAG, "decoderRef?.release()") { decoderRef?.release() }
            bestEffort(TAG, "outFileRef?.delete()") { outFileRef?.delete() }
            bestEffort(TAG, "extractor.release()") { extractor.release() }
            throw t
        }

        fun ensureEncoder(decoderFormat: MediaFormat?) {
            if (encoder != null) return
            if (decoderFormat != null) {
                if (decoderFormat.containsKey(MediaFormat.KEY_SAMPLE_RATE)) {
                    sourceRateHz = decoderFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                }
                if (decoderFormat.containsKey(MediaFormat.KEY_CHANNEL_COUNT)) {
                    srcChannels = decoderFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                    channels = srcChannels.coerceAtMost(2)
                }
            }
            val converter = PcmResampler(sourceRateHz, outputRateHz ?: GeodeNative.aacRateFor(sourceRateHz), channels)
            resampler = converter
            val encFormat =
                MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, converter.outRateHz, channels).apply {
                    setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
                    setInteger(MediaFormat.KEY_BIT_RATE, 192_000)
                    setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 65536)
                }
            encoder =
                MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC).also {
                    encoderRef = it
                    it.configure(encFormat, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
                    it.start()
                }
        }
        var outBytes = 0L
        val infos = mutableListOf<SampleInfo>()
        var outFormat: MediaFormat? = null
        val maxUs = maxDurationMs * 1000
        val startUs = startMs * 1000
        val endUs = if (maxUs > 0) startUs + maxUs else 0L
        val estimatedUs =
            if (maxUs > 0) {
                maxUs
            } else if (srcFormat.containsKey(MediaFormat.KEY_DURATION)) {
                (srcFormat.getLong(MediaFormat.KEY_DURATION) - startUs).coerceAtLeast(0L)
            } else {
                0L
            }
        val decInfo = MediaCodec.BufferInfo()
        val encInfo = MediaCodec.BufferInfo()
        var extractorDone = false
        var decoderDone = false
        var eosSent = false
        var encoderDone = false
        var pcmCarry: ByteBuffer? = null
        var carryTimeUs = 0L
        val trimPtsUs = LongArray(1)
        var progressed = false
        var stallIterations = 0

        fun feedEncoder(): Boolean {
            val carry = pcmCarry ?: return true
            val enc = checkNotNull(encoder) { "PCM queued before the encoder existed" }
            val inIndex = enc.dequeueInputBuffer(10_000)
            if (inIndex < 0) return false
            val inBuf = checkNotNull(enc.getInputBuffer(inIndex)) { "encoder input buffer null (codec error state)" }
            val toWrite = minOf(inBuf.remaining(), carry.remaining())
            val slice = carry.duplicate().apply { limit(position() + toWrite) }
            inBuf.put(slice)
            // The carried PCM is already resampled, so its duration is at the encoder's rate.
            val encoderRateHz = resampler?.outRateHz ?: sourceRateHz
            enc.queueInputBuffer(inIndex, 0, toWrite, carryTimeUs, 0)
            carryTimeUs += GeodeNative.pcmS16BytesToUs(toWrite.toLong(), encoderRateHz, channels)
            carry.position(carry.position() + toWrite)
            if (!carry.hasRemaining()) pcmCarry = null
            progressed = true
            return pcmCarry == null
        }

        try {
            while (!encoderDone) {
                if (isCancelled()) throw kotlinx.coroutines.CancellationException("Export cancelled")
                progressed = false
                if (!extractorDone) {
                    val inIndex = decoder.dequeueInputBuffer(10_000)
                    if (inIndex >= 0) {
                        val buf = checkNotNull(decoder.getInputBuffer(inIndex)) { "decoder input buffer null (codec error state)" }
                        val size = extractor.readSampleData(buf, 0)
                        if (size < 0 || (endUs > 0 && extractor.sampleTime > endUs)) {
                            decoder.queueInputBuffer(inIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            extractorDone = true
                        } else {
                            decoder.queueInputBuffer(inIndex, 0, size, extractor.sampleTime, 0)
                            if (estimatedUs > 0) onProgress((extractor.sampleTime / estimatedUs.toFloat()).coerceIn(0f, 1f))
                            extractor.advance()
                        }
                        progressed = true
                    }
                }
                if (!decoderDone && pcmCarry == null) {
                    val outIndex = decoder.dequeueOutputBuffer(decInfo, 10_000)
                    if (outIndex >= 0) {
                        progressed = true
                        if (decInfo.size > 0) {
                            val buf = checkNotNull(decoder.getOutputBuffer(outIndex)) { "decoder output buffer null (codec error state)" }
                            buf.position(decInfo.offset)
                            buf.limit(decInfo.offset + decInfo.size)
                            val outFmt = decoder.outputFormat
                            ensureEncoder(outFmt)
                            val pcmEnc =
                                if (outFmt.containsKey(MediaFormat.KEY_PCM_ENCODING)) {
                                    outFmt.getInteger(MediaFormat.KEY_PCM_ENCODING)
                                } else {
                                    android.media.AudioFormat.ENCODING_PCM_16BIT
                                }
                            val copy: ByteBuffer
                            if (pcmEnc == android.media.AudioFormat.ENCODING_PCM_FLOAT) {
                                val fb = buf.order(java.nio.ByteOrder.nativeOrder()).asFloatBuffer()
                                val floats = FloatArray(fb.remaining()).also { fb.get(it) }
                                copy = shortsToBuffer(GeodeNative.pcmFloatToS16(floats, floats.size))
                            } else {
                                copy = ByteBuffer.allocate(decInfo.size)
                                copy.put(buf)
                                copy.flip()
                            }
                            val bufChannels =
                                if (outFmt.containsKey(MediaFormat.KEY_CHANNEL_COUNT)) {
                                    outFmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                                } else {
                                    srcChannels
                                }
                            val mixed =
                                if (bufChannels > channels) {
                                    downmix(copy, bufChannels, channels)
                                } else {
                                    copy
                                }
                            // The seek lands on the sync sample before the start: native trims the frames
                            // before it (a straddling buffer keeps the rest) and gives the kept part's pts.
                            val bytes = mixed.remaining().toLong()
                            val skip = GeodeNative.pcmTrimS16(startUs, decInfo.presentationTimeUs, bytes, sourceRateHz, channels, trimPtsUs)
                            if (skip == bytes) {
                                decoder.releaseOutputBuffer(outIndex, false)
                                if (decInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) decoderDone = true
                                continue
                            }
                            mixed.position(mixed.position() + skip.toInt())
                            pcmCarry = resample(mixed, checkNotNull(resampler), channels)
                            carryTimeUs = trimPtsUs[0]
                        }
                        decoder.releaseOutputBuffer(outIndex, false)
                        if (decInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) decoderDone = true
                    }
                }
                if (pcmCarry != null) {
                    feedEncoder()
                } else if (decoderDone && !eosSent) {
                    ensureEncoder(null)
                    val enc = checkNotNull(encoder)
                    val inIndex = enc.dequeueInputBuffer(10_000)
                    if (inIndex >= 0) {
                        enc.queueInputBuffer(inIndex, 0, 0, carryTimeUs, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                        eosSent = true
                        progressed = true
                    }
                }
                while (true) {
                    val enc = encoder ?: break
                    val outIndex = enc.dequeueOutputBuffer(encInfo, 0)
                    if (outIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                        outFormat = enc.outputFormat
                        progressed = true
                    } else if (outIndex >= 0) {
                        progressed = true
                        if (encInfo.size > 0 && encInfo.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG == 0) {
                            val buf = checkNotNull(enc.getOutputBuffer(outIndex)) { "encoder output buffer null (codec error state)" }
                            buf.position(encInfo.offset)
                            buf.limit(encInfo.offset + encInfo.size)
                            val bytes = ByteArray(encInfo.size)
                            buf.get(bytes)
                            infos += SampleInfo(outBytes, encInfo.size, encInfo.presentationTimeUs, encInfo.flags)
                            out.write(bytes)
                            outBytes += encInfo.size
                        }
                        val eos = encInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0
                        enc.releaseOutputBuffer(outIndex, false)
                        if (eos) {
                            encoderDone = true
                            break
                        }
                    } else {
                        break
                    }
                }
                if (progressed) {
                    stallIterations = 0
                } else if (++stallIterations > STALL_LIMIT) {
                    throw IllegalStateException("Audio transcode stalled (codec made no progress)")
                }
            }
            out.flush()
            return Result(requireNotNull(outFormat) { "AAC encoder produced no format" }, outFile, infos)
        } catch (t: Throwable) {
            bestEffort(TAG, "out.close()") { out.close() }
            bestEffort(TAG, "outFile.delete()") { outFile.delete() }
            throw t
        } finally {
            bestEffort(TAG, "out.close()") { out.close() }
            bestEffort(TAG, "decoder.stop()") { decoder.stop() }
            bestEffort(TAG, "decoder.release()") { decoder.release() }
            // encoderRef, not encoder: `encoder` is only assigned once configure() and start()
            // have both returned, so a codec that fails to configure would otherwise never be
            // released — and a leaked AAC encoder is a device-wide resource the next export needs.
            bestEffort(TAG, "encoderRef?.stop()") { encoderRef?.stop() }
            bestEffort(TAG, "encoderRef?.release()") { encoderRef?.release() }
            bestEffort(TAG, "extractor.release()") { extractor.release() }
            bestEffort(TAG, "resampler?.close()") { resampler?.close() }
        }
    }

    private companion object {
        const val STALL_LIMIT = 1_000
    }
}

private const val TAG = "AudioTranscoder"
