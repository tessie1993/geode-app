package dev.geode.render.offscreen

import android.content.Context
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
 * Streaming, not decoded up front: a long hi-res track would be hundreds of megabytes of floats. The
 * samples never cross into Kotlin: each decoded chunk goes to a native feed (`core/audio/ingest/TrackFeed`,
 * pinned to this class's old Kotlin by `core/tests/track_feed_test.cpp`), which converts it, drops what
 * precedes [startMs] (whatever keyframe the extractor landed on), and hands the renderer's engine exactly
 * the frames up to the one asked for, keeping the rest for the next call. AIFF goes through [AiffPcm],
 * as the analyser does, because MediaExtractor does not read it. This class only drives the decoder.
 *
 * One thread, the export's. [close] releases the codec, the extractor and the feed.
 */
class TrackAudio(
    context: Context,
    uri: Uri,
    startMs: Long,
) : AutoCloseable {
    private var feed = GeodeNative.trackFeedCreate(startMs).also { check(it != 0L) { "track feed not created" } }
    private val aiff: AiffPcm? = openAiff(context, uri)
    private val extractor: MediaExtractor? = if (aiff == null) MediaExtractor() else null
    private var codec: MediaCodec? = null
    private val info = MediaCodec.BufferInfo()
    private var inputDone = false
    private var ended = false

    init {
        if (aiff == null) runCatching { openDecoder(context, uri, startMs) }.onFailure { close() }.getOrThrow()
    }

    // The feed is native, and a constructor that throws leaves the caller nothing to close: free it here.
    private fun openAiff(
        context: Context,
        uri: Uri,
    ): AiffPcm? = runCatching { AiffPcm.open(context, uri) }.onFailure { GeodeNative.trackFeedDestroy(feed) }.getOrThrow()

    private fun openDecoder(
        context: Context,
        uri: Uri,
        startMs: Long,
    ) {
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

    /**
     * Hands [viz]'s sound engine everything from the range start up to [endMs] into the range, at whatever
     * rate the track decodes to, decoding as needed. Returns false once the track has run out.
     */
    fun deliverUntilMs(
        endMs: Long,
        viz: NativeViz,
    ): Boolean {
        // The rate is only known once the decoder has produced something.
        while (!GeodeNative.trackFeedHasRate(feed) && !ended) step()
        if (!GeodeNative.trackFeedHasRate(feed)) return false
        GeodeNative.trackFeedTargetMs(feed, endMs)
        while (!viz.hearTrack(feed)) {
            if (ended) return false
            step()
        }
        return true
    }

    private fun step() {
        val a = aiff
        if (a == null) {
            decodeStep()
        } else if (!GeodeNative.trackFeedPushAiff(feed, a.handle)) {
            ended = true
        }
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
            val isFloat =
                fmt.containsKey(MediaFormat.KEY_PCM_ENCODING) &&
                    fmt.getInteger(MediaFormat.KEY_PCM_ENCODING) == android.media.AudioFormat.ENCODING_PCM_FLOAT
            val buf = checkNotNull(c.getOutputBuffer(outIndex)) { "decoder output buffer null (codec error state)" }
            // MediaCodec's buffers are direct, so native reads the chunk where it lies.
            val taken =
                GeodeNative.trackFeedPushDirect(
                    handle = feed,
                    buffer = buf,
                    offset = info.offset,
                    size = info.size,
                    isFloat = isFloat,
                    channels = fmt.getInteger(MediaFormat.KEY_CHANNEL_COUNT),
                    sampleRate = fmt.getInteger(MediaFormat.KEY_SAMPLE_RATE),
                    ptsUs = info.presentationTimeUs,
                )
            check(taken) { "decoder output buffer not direct" }
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
        GeodeNative.trackFeedDestroy(feed)
        feed = 0L
    }

    private companion object {
        const val TAG = "TrackAudio"
        const val TIMEOUT_US = 10_000L
    }
}
