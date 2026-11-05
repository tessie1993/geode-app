package dev.geode.render.offscreen

import android.content.Context
import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import dev.geode.audio.AiffPcm
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.bridge.NativeViz
import dev.geode.util.bestEffort

/**
 * The exported range of a track, decoded as the export asks for it, for the export renderer's own sound
 * engine. This is what makes export equal live: the same `core/sound` code hears the same audio, frame by
 * frame, stepped at 1/fps instead of the screen's frame time.
 *
 * Only the Android half is here: MediaExtractor and MediaCodec (or [AiffPcm], which MediaExtractor cannot
 * read). The decoder's bytes go straight into a native file ring (`core/audio/ingest/FileFeed`, #490),
 * which drops what precedes the range start, converts, and hands video frame n exactly the samples
 * [n * rate / fps, (n + 1) * rate / fps) through the ingest's cursor, so the count never drifts and the
 * same file renders the same audio at any frame rate and any decoder chunk size
 * (`core/tests/file_feed_test.cpp`). Streaming, not decoded up front: a long hi-res track would be
 * hundreds of megabytes of floats.
 *
 * One thread, the export's. [close] releases the codec, the extractor and the native feed.
 */
class TrackAudio(
    context: Context,
    uri: Uri,
    startMs: Long,
) : AutoCloseable {
    private val aiff: AiffPcm? = AiffPcm.open(context, uri)
    private val extractor: MediaExtractor? = if (aiff == null) MediaExtractor() else null
    private var codec: MediaCodec? = null
    private val info = MediaCodec.BufferInfo()
    private var inputDone = false
    private var ended = false
    private val aiffBuf = ShortArray(AIFF_CHUNK)
    private var feed: Long = GeodeNative.fileFeedCreate(startMs * MICROS_PER_MS)

    init {
        if (aiff == null) {
            val ex = requireNotNull(extractor)
            ex.setDataSource(context, uri, null)
            val track =
                (0 until ex.trackCount).firstOrNull { i ->
                    ex.getTrackFormat(i).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
                } ?: throw IllegalArgumentException("No audio track in file")
            val format = ex.getTrackFormat(track)
            ex.selectTrack(track)
            if (startMs > 0) ex.seekTo(startMs * MICROS_PER_MS, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
            codec =
                MediaCodec.createDecoderByType(requireNotNull(format.getString(MediaFormat.KEY_MIME))).apply {
                    configure(format, null, null, 0)
                    start()
                }
        }
    }

    /**
     * Decodes until video frame [frame] at [fps] has all its audio (or the track has ended), then writes
     * the samples the engine has not heard yet into [viz]'s sound engine, natively, before it renders.
     */
    fun deliverFrame(
        viz: NativeViz,
        frame: Int,
        fps: Int,
    ) {
        val f = feed
        if (f == 0L) return
        while (!ended && GeodeNative.fileFeedNeeds(f, frame.toLong(), fps)) {
            if (aiff != null) readAiff(aiff) else decodeStep()
        }
        viz.feedFile(f, frame.toLong(), fps)
    }

    private fun readAiff(a: AiffPcm) {
        val n = a.read(aiffBuf)
        if (n <= 0) {
            ended = true
            return
        }
        GeodeNative.fileFeedWriteShorts(feed, aiffBuf, n, a.sampleRate, a.channels)
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
            // No encoding key means 16-bit; the native feed refuses (and skips) one it cannot read.
            val encoding =
                if (fmt.containsKey(MediaFormat.KEY_PCM_ENCODING)) {
                    fmt.getInteger(MediaFormat.KEY_PCM_ENCODING)
                } else {
                    AudioFormat.ENCODING_PCM_16BIT
                }
            val buf = checkNotNull(c.getOutputBuffer(outIndex)) { "decoder output buffer null (codec error state)" }
            GeodeNative.fileFeedWrite(
                feed,
                buf,
                info.offset,
                info.size,
                encoding,
                fmt.getInteger(MediaFormat.KEY_SAMPLE_RATE),
                fmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT),
                info.presentationTimeUs,
            )
        }
        c.releaseOutputBuffer(outIndex, false)
        if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) ended = true
    }

    override fun close() {
        codec?.let {
            bestEffort(TAG, "codec.stop()") { it.stop() }
            it.release()
        }
        codec = null
        extractor?.release()
        aiff?.close()
        val f = feed
        feed = 0L
        if (f != 0L) GeodeNative.fileFeedDestroy(f)
    }

    private companion object {
        const val TAG = "TrackAudio"
        const val TIMEOUT_US = 10_000L
        const val AIFF_CHUNK = 8192
        const val MICROS_PER_MS = 1000L
    }
}
