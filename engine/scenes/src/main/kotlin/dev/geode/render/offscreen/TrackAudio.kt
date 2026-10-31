package dev.geode.render.offscreen

import android.content.Context
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import dev.geode.audio.AiffPcm
import dev.geode.util.bestEffort
import java.nio.ByteOrder

/**
 * The exported range of a track as interleaved float PCM, decoded as the export asks for it, for the
 * export renderer's own sound engine. This is what makes export equal live: the same `core/sound`
 * code hears the same audio, frame by frame, stepped at 1/fps instead of the screen's frame time.
 *
 * Streaming, not decoded up front: a long hi-res track would be hundreds of megabytes of floats.
 * [deliverUntil] hands out exactly the frames up to the one asked for (counted from [startMs]) and
 * keeps the rest of a decoded chunk for the next call, so video frame n's engine has heard the audio
 * up to frame n and no further. Samples before [startMs] are dropped after the seek, so frame 0 is the
 * range's first sample whatever keyframe the extractor landed on. AIFF goes through [AiffPcm], as the
 * analyser does, because MediaExtractor does not read it.
 *
 * One thread, the export's. [close] releases the codec and the extractor.
 */
class TrackAudio(
    context: Context,
    uri: Uri,
    private val startMs: Long,
) : AutoCloseable {
    private val aiff: AiffPcm? = AiffPcm.open(context, uri)
    private val extractor: MediaExtractor? = if (aiff == null) MediaExtractor() else null
    private var codec: MediaCodec? = null
    private val info = MediaCodec.BufferInfo()
    private var inputDone = false
    private var ended = false
    private var delivered = 0L
    private val aiffBuf = ShortArray(AIFF_CHUNK)
    private var aiffSkip = 0L

    // The decoded chunk not yet handed out: interleaved, `pendingFrames` frames from `pendingAt`.
    private var pending = FloatArray(0)
    private var pendingAt = 0
    private var pendingFrames = 0
    private var pendingChannels = 0
    private var pendingRate = 0

    init {
        if (aiff != null) {
            aiffSkip = startMs * aiff.sampleRate / 1000L
        } else {
            val ex = requireNotNull(extractor)
            ex.setDataSource(context, uri, null)
            val track =
                (0 until ex.trackCount).firstOrNull { i ->
                    ex.getTrackFormat(i).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
                } ?: throw IllegalArgumentException("No audio track in file")
            val format = ex.getTrackFormat(track)
            ex.selectTrack(track)
            if (startMs > 0) ex.seekTo(startMs * 1000L, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
            codec =
                MediaCodec.createDecoderByType(requireNotNull(format.getString(MediaFormat.KEY_MIME))).apply {
                    configure(format, null, null, 0)
                    start()
                }
        }
    }

    /**
     * Hands [sink] (interleaved, frameCount, channels, sampleRateHz) everything from the range start up
     * to frame [frames], decoding as needed. Frames are counted at the track's own rate. Returns false
     * once the track has run out.
     */
    fun deliverUntil(
        frames: Long,
        sink: (FloatArray, Int, Int, Int) -> Unit,
    ): Boolean {
        while (delivered < frames) {
            if (pendingFrames == 0) {
                if (ended) return false
                if (aiff != null) readAiff() else decodeStep()
                continue
            }
            val n = minOf(pendingFrames.toLong(), frames - delivered).toInt()
            if (pendingAt > 0) {
                System.arraycopy(pending, pendingAt * pendingChannels, pending, 0, pendingFrames * pendingChannels)
                pendingAt = 0
            }
            sink(pending, n, pendingChannels, pendingRate)
            pendingAt = n
            pendingFrames -= n
            delivered += n
        }
        return true
    }

    /** [deliverUntil] for everything up to [endMs] into the range, at whatever rate the track decodes to. */
    fun deliverUntilMs(
        endMs: Long,
        sink: (FloatArray, Int, Int, Int) -> Unit,
    ): Boolean {
        // The rate is only known once the decoder has produced something.
        while (pendingRate == 0 && !ended) {
            if (aiff != null) readAiff() else decodeStep()
        }
        if (pendingRate == 0) return false
        return deliverUntil(endMs * pendingRate / 1000L, sink)
    }

    private fun readAiff() {
        val a = requireNotNull(aiff)
        val n = a.read(aiffBuf)
        if (n <= 0) {
            ended = true
            return
        }
        var count = n / a.channels
        var from = 0
        if (aiffSkip > 0) {
            val drop = minOf(aiffSkip, count.toLong()).toInt()
            aiffSkip -= drop
            from = drop
            count -= drop
        }
        if (count <= 0) return
        val out = pendingFor(count * a.channels)
        for (i in 0 until count * a.channels) out[i] = aiffBuf[from * a.channels + i] / PCM16
        setPending(count, a.channels, a.sampleRate)
    }

    private fun decodeStep() {
        val c = requireNotNull(codec)
        val ex = requireNotNull(extractor)
        if (!inputDone) {
            val inIndex = c.dequeueInputBuffer(TIMEOUT_US)
            if (inIndex >= 0) {
                val buf = checkNotNull(c.getInputBuffer(inIndex)) { "decoder input buffer null (codec error state)" }
                val size = ex.readSampleData(buf, 0)
                if (size < 0) {
                    c.queueInputBuffer(inIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                    inputDone = true
                } else {
                    c.queueInputBuffer(inIndex, 0, size, ex.sampleTime, 0)
                    ex.advance()
                }
            }
        }
        val outIndex = c.dequeueOutputBuffer(info, TIMEOUT_US)
        if (outIndex < 0) return
        if (info.size > 0) {
            val fmt = c.outputFormat
            val rate = fmt.getInteger(MediaFormat.KEY_SAMPLE_RATE)
            val ch = fmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
            val isFloat =
                fmt.containsKey(MediaFormat.KEY_PCM_ENCODING) &&
                    fmt.getInteger(MediaFormat.KEY_PCM_ENCODING) == android.media.AudioFormat.ENCODING_PCM_FLOAT
            val buf = checkNotNull(c.getOutputBuffer(outIndex)) { "decoder output buffer null (codec error state)" }
            buf.position(info.offset)
            buf.limit(info.offset + info.size)
            val samples = if (isFloat) info.size / 4 else info.size / 2
            val out = pendingFor(samples)
            if (isFloat) {
                buf.order(ByteOrder.nativeOrder()).asFloatBuffer().get(out, 0, samples)
            } else {
                val shorts = buf.order(ByteOrder.LITTLE_ENDIAN).asShortBuffer()
                for (i in 0 until samples) out[i] = shorts.get(i) / PCM16
            }
            var count = samples / ch
            // The extractor seeks to the keyframe at or before the start; drop what precedes it.
            val startUs = startMs * 1000L
            var from = 0
            if (info.presentationTimeUs < startUs) {
                from = ((startUs - info.presentationTimeUs) * rate / 1_000_000L).toInt().coerceAtMost(count)
                count -= from
            }
            if (count > 0) {
                if (from > 0) System.arraycopy(out, from * ch, out, 0, count * ch)
                setPending(count, ch, rate)
            }
        }
        c.releaseOutputBuffer(outIndex, false)
        if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) ended = true
    }

    private fun pendingFor(samples: Int): FloatArray {
        if (pending.size < samples) pending = FloatArray(samples)
        return pending
    }

    private fun setPending(
        frames: Int,
        ch: Int,
        rate: Int,
    ) {
        pendingAt = 0
        pendingFrames = frames
        pendingChannels = ch
        pendingRate = rate
    }

    override fun close() {
        codec?.let {
            bestEffort(TAG, "codec.stop()") { it.stop() }
            it.release()
        }
        codec = null
        extractor?.release()
        aiff?.close()
    }

    private companion object {
        const val TAG = "TrackAudio"
        const val TIMEOUT_US = 10_000L
        const val AIFF_CHUNK = 8192
        const val PCM16 = 32768f
    }
}
