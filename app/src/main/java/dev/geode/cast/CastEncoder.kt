package dev.geode.cast

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.view.Surface
import androidx.annotation.OptIn
import androidx.media3.common.C
import androidx.media3.common.Format
import androidx.media3.common.util.MediaFormatUtil
import androidx.media3.common.util.UnstableApi
import androidx.media3.muxer.BufferInfo
import androidx.media3.muxer.FragmentedMp4Muxer
import dev.geode.RingLog
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Encodes the cast stream: H.264 from a GL surface and AAC from 16-bit PCM, muxed as fragmented
 * MP4 into [LiveSegments].
 *
 * The video codec's input [surface] is what the render loop draws into (through
 * `export/EncoderSurface`, the same EGL setup an export uses). Audio is pushed with [queueAudio].
 * One drain thread owns the muxer: it pulls both codecs' output, holds samples back until both
 * tracks' formats are known (a fragmented MP4's header names every track, so tracks can't be added
 * after the first sample), and then writes everything in arrival order.
 *
 * A keyframe a second ([KEYFRAME_INTERVAL_SEC]) and one-second fragments make one-second HLS
 * segments: the shortest the receiver handles well, so the TV runs as little behind as it can.
 */
@OptIn(UnstableApi::class)
internal class CastEncoder(
    segments: LiveSegments,
    val width: Int = castFormat[0],
    val height: Int = castFormat[1],
) {
    private val video: MediaCodec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
    private val audio: MediaCodec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC)
    private val muxer =
        FragmentedMp4Muxer
            .Builder(segments)
            .setFragmentDurationMs(FRAGMENT_MS)
            .build()

    /** The surface the render loop draws each frame into. */
    val surface: Surface

    @Volatile
    private var running = true
    private var drainThread: Thread? = null
    private var videoTrack = -1
    private var audioTrack = -1

    /** Each codec's output format, video then audio, as it arrives; tracks are added once both are here. */
    private val pendingFormats = arrayOfNulls<Format>(2)
    private val held = ArrayList<Triple<Boolean, ByteArray, BufferInfo>>()

    init {
        val videoFormat =
            MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height).apply {
                setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
                setInteger(MediaFormat.KEY_BIT_RATE, VIDEO_BITRATE)
                setInteger(MediaFormat.KEY_FRAME_RATE, FPS)
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, KEYFRAME_INTERVAL_SEC)
            }
        video.configure(videoFormat, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        surface = video.createInputSurface()
        val audioFormat =
            MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, AUDIO_RATE_HZ, CHANNELS).apply {
                setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
                setInteger(MediaFormat.KEY_BIT_RATE, AUDIO_BITRATE)
            }
        audio.configure(audioFormat, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        video.start()
        audio.start()
        drainThread =
            Thread({ drainLoop() }, "cast-encoder").apply {
                isDaemon = true
                start()
            }
    }

    /**
     * Queues [frames] interleaved stereo frames at [AUDIO_RATE_HZ], stamped [ptsUs]. Called from the
     * render loop; drops the block if the codec has no free input buffer (a stalled encoder must not
     * stall the picture).
     */
    fun queueAudio(
        pcm: ShortArray,
        frames: Int,
        ptsUs: Long,
    ) {
        val index = audio.dequeueInputBuffer(0)
        if (index < 0) return
        val buffer = audio.getInputBuffer(index) ?: return
        buffer.clear()
        buffer.order(ByteOrder.nativeOrder())
        val samples = minOf(frames * CHANNELS, buffer.capacity() / BYTES_PER_SAMPLE)
        buffer.asShortBuffer().put(pcm, 0, samples)
        audio.queueInputBuffer(index, 0, samples * BYTES_PER_SAMPLE, ptsUs, 0)
    }

    private fun drainLoop() {
        val info = MediaCodec.BufferInfo()
        // A codec error (or release() racing a slow join) throws here; uncaught on this thread it
        // would kill the process. The render loop's next feed fails too and ends the cast cleanly.
        runCatching {
            while (running) {
                val wroteVideo = drain(video, isVideo = true, info)
                val wroteAudio = drain(audio, isVideo = false, info)
                if (!wroteVideo && !wroteAudio) Thread.sleep(IDLE_MS)
            }
        }.onFailure { RingLog.note(TAG, "drain: ${it.javaClass.simpleName}") }
    }

    /** Takes one output buffer from [codec] if there is one; true if it did. */
    private fun drain(
        codec: MediaCodec,
        isVideo: Boolean,
        info: MediaCodec.BufferInfo,
    ): Boolean {
        val index = codec.dequeueOutputBuffer(info, 0)
        when {
            index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                pendingFormats[if (isVideo) 0 else 1] = MediaFormatUtil.createFormatFromMediaFormat(codec.outputFormat)
                addTracksWhenReady()
                return true
            }
            index < 0 -> return false
        }
        val buffer = codec.getOutputBuffer(index)
        val isConfig = info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG != 0
        if (buffer != null && info.size > 0 && !isConfig) {
            val bytes = ByteArray(info.size)
            buffer.position(info.offset)
            buffer.get(bytes, 0, info.size)
            val flags = if (info.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME != 0) C.BUFFER_FLAG_KEY_FRAME else 0
            write(isVideo, bytes, BufferInfo(info.presentationTimeUs, info.size, flags))
        }
        codec.releaseOutputBuffer(index, false)
        return true
    }

    private fun addTracksWhenReady() {
        val v = pendingFormats[0] ?: return
        val a = pendingFormats[1] ?: return
        if (videoTrack >= 0) return
        videoTrack = muxer.addTrack(v)
        audioTrack = muxer.addTrack(a)
        held.forEach { (isVideo, bytes, info) -> writeNow(isVideo, bytes, info) }
        held.clear()
    }

    private fun write(
        isVideo: Boolean,
        bytes: ByteArray,
        info: BufferInfo,
    ) {
        if (videoTrack < 0) {
            if (held.size < MAX_HELD) held += Triple(isVideo, bytes, info)
            return
        }
        writeNow(isVideo, bytes, info)
    }

    private fun writeNow(
        isVideo: Boolean,
        bytes: ByteArray,
        info: BufferInfo,
    ) {
        runCatching { muxer.writeSampleData(if (isVideo) videoTrack else audioTrack, ByteBuffer.wrap(bytes), info) }
            .onFailure { RingLog.note(TAG, "mux: ${it.javaClass.simpleName}") }
    }

    /** Stops both codecs and the muxer. Safe to call more than once. */
    fun release() {
        if (!running) return
        running = false
        bestEffort(TAG, "drain.join()") { drainThread?.join(JOIN_MS) }
        bestEffort(TAG, "video.stop()") { video.stop() }
        bestEffort(TAG, "audio.stop()") { audio.stop() }
        bestEffort(TAG, "video.release()") { video.release() }
        bestEffort(TAG, "audio.release()") { audio.release() }
        bestEffort(TAG, "surface.release()") { surface.release() }
        bestEffort(TAG, "muxer.close()") { muxer.close() }
    }

    companion object {
        private val castFormat = IntArray(3).also { GeodeNative.castFormat(it) } // width, height, fps: core/viz/frame
        val FPS = castFormat[2]
        const val AUDIO_RATE_HZ = 48_000
        const val CHANNELS = 2
        private const val VIDEO_BITRATE = 4_000_000
        private const val AUDIO_BITRATE = 160_000
        private const val KEYFRAME_INTERVAL_SEC = 1
        private const val FRAGMENT_MS = 1_000L
        private const val BYTES_PER_SAMPLE = 2
        private const val IDLE_MS = 5L
        private const val JOIN_MS = 1_000L

        /** Samples kept while waiting for the second track's format: a few seconds at most. */
        private const val MAX_HELD = 512
        private const val TAG = "CastEncoder"
    }
}
