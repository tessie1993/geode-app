package dev.geode.record

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaMuxer
import android.view.Surface
import dev.geode.RingLog
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * The Android half of Record: an H.264 encoder whose input [surface] the stage's GL thread copies
 * frames into, an AAC encoder fed the sound the stage heard, and the MP4 muxer, all on one thread.
 *
 * It times nothing. Which frames arrive and their PTS, the audio and its PTS, and when the
 * recording ends all come from the native clock behind [recorder] (`core/output/Recorder`); this
 * thread only moves bytes: audio from that clock into the AAC encoder, both encoders' output into
 * [file], and back to the clock how many frames came out and how much was written.
 *
 * The end is the clock's too. Once it says stopped, [onStopping] asks the owner to close the GL copy
 * (the encoder's surface must not be drawn into after its end of stream), [teeClosed] answers, the
 * rest of the audio is taken, both streams are ended and the file is finished; [onFinished] then
 * says whether [file] holds a video.
 */
internal class RecordEncoder private constructor(
    private val file: File,
    private val recorder: Long,
    private val video: MediaCodec,
    val width: Int,
    val height: Int,
) {
    /** The video encoder's input: what `GeodeNative.vizRecordStart` draws into. */
    val surface: Surface = video.createInputSurface()

    private val audio: MediaCodec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC)
    private val muxer = MediaMuxer(file.path, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
    private val pcm: ByteBuffer = ByteBuffer.allocateDirect(PCM_FRAMES * FRAME_BYTES).order(ByteOrder.nativeOrder())
    private val ptsUs = LongArray(1)
    private val info = MediaCodec.BufferInfo()
    private val teeClosed = CountDownLatch(1)

    private var formats = arrayOfNulls<MediaFormat>(2)
    private val tracks = intArrayOf(-1, -1)
    private var muxing = false
    private val held = ArrayList<Held>()
    private var wroteVideo = false
    private var audioInput = -1
    private var audioEnded = false
    private val done = BooleanArray(2)

    private class Held(
        val track: Int,
        val bytes: ByteBuffer,
        val info: MediaCodec.BufferInfo,
    )

    /** Starts both encoders and the thread. [onStopping] and [onFinished] run on that thread. */
    fun start(
        onStopping: () -> Unit,
        onFinished: (saved: Boolean) -> Unit,
    ) {
        val format =
            MediaFormat.createAudioFormat(
                MediaFormat.MIMETYPE_AUDIO_AAC,
                GeodeNative.RECORD_AUDIO_RATE_HZ,
                GeodeNative.RECORD_AUDIO_CHANNELS,
            ).apply {
                setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
                setInteger(MediaFormat.KEY_BIT_RATE, GeodeNative.RECORD_AUDIO_BPS)
            }
        Thread({
            val saved =
                runCatching {
                    audio.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
                    video.start()
                    audio.start()
                    pump(onStopping)
                }.onFailure {
                    RingLog.note(TAG, "record: ${it.javaClass.simpleName}")
                    GeodeNative.recorderStop(recorder, GeodeNative.RECORD_STOP_FAILED, GeodeNative.recordNowNs())
                    onStopping()
                    awaitTee()
                }.isSuccess
            onFinished(release() && saved && wroteVideo)
        }, "record-encoder").start()
    }

    /** The GL copy into [surface] has ended; the video stream may now end too. Any thread. */
    fun teeClosed() = teeClosed.countDown()

    private fun pump(onStopping: () -> Unit) {
        var ending = false
        while (!(done[VIDEO] && done[AUDIO])) {
            val stopped = GeodeNative.recorderStopped(recorder) != 0
            if (stopped && !ending) {
                ending = true
                onStopping()
                awaitTee()
                video.signalEndOfInputStream()
            }
            var busy = feedAudio(stopped)
            busy = drain(video, VIDEO) or busy
            busy = drain(audio, AUDIO) or busy
            if (!busy) Thread.sleep(IDLE_MS)
        }
    }

    private fun awaitTee() {
        bestEffort(TAG, "await the GL copy's end") { teeClosed.await(TEE_WAIT_MS, TimeUnit.MILLISECONDS) }
    }

    /** One input buffer of the recording's audio, when the encoder has room and the clock has some. */
    private fun feedAudio(stopped: Boolean): Boolean {
        if (audioEnded) return false
        if (audioInput < 0) audioInput = audio.dequeueInputBuffer(0)
        if (audioInput < 0) return false
        val buffer = audio.getInputBuffer(audioInput) ?: return false
        val room = minOf(PCM_FRAMES, buffer.capacity() / FRAME_BYTES)
        val frames = GeodeNative.recorderTakeAudio(recorder, GeodeNative.recordNowNs(), pcm, room, ptsUs)
        if (frames <= 0 && !stopped) return false
        pcm.position(0).limit(frames.coerceAtLeast(0) * FRAME_BYTES)
        buffer.clear()
        buffer.put(pcm)
        pcm.clear()
        // After the stop the clock hands over what is left; its first empty answer is the end.
        audioEnded = frames <= 0
        val flags = if (audioEnded) MediaCodec.BUFFER_FLAG_END_OF_STREAM else 0
        audio.queueInputBuffer(audioInput, 0, frames.coerceAtLeast(0) * FRAME_BYTES, ptsUs[0], flags)
        audioInput = -1
        return true
    }

    /** Takes one output buffer from [codec] if there is one; true if it did. */
    private fun drain(
        codec: MediaCodec,
        which: Int,
    ): Boolean {
        if (done[which]) return false
        val index = codec.dequeueOutputBuffer(info, 0)
        if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
            formats[which] = codec.outputFormat
            startMuxerWhenReady()
            return true
        }
        if (index < 0) return false
        val buffer = codec.getOutputBuffer(index)
        val config = info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG != 0
        if (buffer != null && info.size > 0 && !config) {
            buffer.position(info.offset).limit(info.offset + info.size)
            if (which == VIDEO) GeodeNative.recorderVideoEncoded(recorder)
            write(which, buffer)
        }
        if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) done[which] = true
        codec.releaseOutputBuffer(index, false)
        return true
    }

    /** A sample that comes out before both formats are known waits as a copy; MP4 names its tracks first. */
    private fun write(
        which: Int,
        buffer: ByteBuffer,
    ) {
        if (!muxing) {
            val copy = ByteBuffer.allocate(info.size).put(buffer).flip() as ByteBuffer
            val kept = MediaCodec.BufferInfo().apply { set(0, info.size, info.presentationTimeUs, info.flags) }
            held += Held(which, copy, kept)
            return
        }
        muxer.writeSampleData(tracks[which], buffer, info)
        wroteVideo = wroteVideo || which == VIDEO
        GeodeNative.recorderWrote(recorder, info.size.toLong(), GeodeNative.recordNowNs())
    }

    private fun startMuxerWhenReady() {
        val videoFormat = formats[VIDEO] ?: return
        val audioFormat = formats[AUDIO] ?: return
        if (muxing) return
        tracks[VIDEO] = muxer.addTrack(videoFormat)
        tracks[AUDIO] = muxer.addTrack(audioFormat)
        muxer.start()
        muxing = true
        for (sample in held) {
            muxer.writeSampleData(tracks[sample.track], sample.bytes, sample.info)
            wroteVideo = wroteVideo || sample.track == VIDEO
            GeodeNative.recorderWrote(recorder, sample.info.size.toLong(), GeodeNative.recordNowNs())
        }
        held.clear()
    }

    /** Frees everything; false when the muxer could not finish the file. */
    private fun release(): Boolean {
        // MediaMuxer.stop() throws on a file with no sample, which is not a video anyway.
        val finished = muxing && wroteVideo && runCatching { muxer.stop() }.isSuccess
        bestEffort(TAG, "muxer.release()") { muxer.release() }
        bestEffort(TAG, "video.stop()") { video.stop() }
        bestEffort(TAG, "audio.stop()") { audio.stop() }
        bestEffort(TAG, "video.release()") { video.release() }
        bestEffort(TAG, "audio.release()") { audio.release() }
        bestEffort(TAG, "surface.release()") { surface.release() }
        return finished
    }

    companion object {
        private const val TAG = "RecordEncoder"
        private const val VIDEO = 0
        private const val AUDIO = 1
        private const val FRAME_BYTES = GeodeNative.RECORD_AUDIO_CHANNELS * 2
        private const val PCM_FRAMES = 4096
        private const val IDLE_MS = 5L

        /** How long the stream's end waits for the GL thread, which may already be gone with its surface. */
        private const val TEE_WAIT_MS = 1_000L

        /**
         * Opens the video encoder for a stage of [stageWidth] x [stageHeight] at the first size the
         * device accepts of the ones native offers for [shortSide] (`geode_record_encoder_attempt`),
         * or null when it accepts none.
         */
        fun open(
            file: File,
            recorder: Long,
            stageWidth: Int,
            stageHeight: Int,
            shortSide: Int,
            fps: Int,
        ): RecordEncoder? {
            val ask = IntArray(3)
            val limitMs = GeodeNative.recorderLimitMs(recorder)
            var attempt = 0
            while (GeodeNative.recordEncoderAttempt(stageWidth, stageHeight, shortSide, fps, limitMs, attempt++, ask)) {
                val format =
                    MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, ask[0], ask[1]).apply {
                        setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
                        setInteger(MediaFormat.KEY_BIT_RATE, ask[2])
                        setInteger(MediaFormat.KEY_FRAME_RATE, fps)
                        setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
                    }
                val codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
                val opened =
                    runCatching {
                        codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
                        RecordEncoder(file, recorder, codec, ask[0], ask[1])
                    }.onFailure { bestEffort(TAG, "codec.release()") { codec.release() } }.getOrNull()
                if (opened != null) return opened
            }
            return null
        }
    }
}
