package dev.geode.engine.bridge

// The JNI declarations of geode_audio_out_jni.cpp.
object AudioOutNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // The playback output (core/audio/output): an Oboe stream under Media3's sink, where the AudioTrack stands.

    /** A stream failed for good at [nowMs] (a monotonic clock); native then closes the output for a minute. */
    external fun audioOutNoteFailed(nowMs: Long)

    /** Whether the native output is wanted: [chosen] by the user, unless bit-perfect is on and supported. */
    external fun audioOutWanted(
        chosen: Boolean,
        bitPerfect: Boolean,
        bitPerfectSupported: Boolean,
    ): Boolean

    /**
     * Whether the output plays a Media3 output config, its fields as they are; false for a minute after
     * [audioOutNoteFailed] at [nowMs] (a monotonic clock).
     */
    external fun audioOutTakes(
        nowMs: Long,
        offload: Boolean,
        tunneling: Boolean,
        offloadGapless: Boolean,
        playbackParameters: Boolean,
        virtualDeviceId: Int,
        usage: Int,
        channelMask: Int,
        encoding: Int,
        sampleRateHz: Int,
    ): Boolean

    /**
     * Opens a stream for writes of [encoding] at [channelMask]'s channels; [bufferBytes] is the sink's buffer
     * size for it, [sessionId] the player's audio session (0 or below allocates one). 0 when the device gave no
     * stream that is exactly this. Ends with [audioOutRelease].
     */
    external fun audioOutOpen(
        sampleRateHz: Int,
        channelMask: Int,
        encoding: Int,
        bufferBytes: Int,
        sessionId: Int,
    ): Long

    /** Silences the stream and closes it off-thread; the handle is dead when this returns. */
    external fun audioOutRelease(handle: Long)

    external fun audioOutPlay(handle: Long)

    external fun audioOutPause(handle: Long)

    external fun audioOutFlush(handle: Long)

    /** The last frame was written: what waits plays out, and the position ends on it. */
    external fun audioOutStop(handle: Long)

    /**
     * Takes the whole frames that fit from [bytes] bytes of a direct [buffer] at [position], without
     * blocking. Returns the bytes taken; -1 for a heap buffer or once the stream has failed for good.
     */
    external fun audioOutWrite(
        handle: Long,
        buffer: java.nio.ByteBuffer,
        position: Int,
        bytes: Int,
    ): Int

    /** How much of what was written the listener has heard, in µs of audio; never backwards. */
    external fun audioOutPositionUs(handle: Long): Long

    /** 0..1, reached over a short ramp. */
    external fun audioOutSetVolume(
        handle: Long,
        volume: Float,
    )

    /** The device to play on (`AudioDeviceInfo.getId`), 0 for the default; a change reopens the stream. */
    external fun audioOutSetDevice(
        handle: Long,
        deviceId: Int,
    )

    /** GEODE_AUDIO_OUT_*: the position began to move, an underrun, the stream failed for good. */
    const val AUDIO_OUT_ADVANCING = 1
    const val AUDIO_OUT_UNDERRUN = 2
    const val AUDIO_OUT_FAILED = 4

    /** The AUDIO_OUT_* bits since the last call. */
    external fun audioOutEvents(handle: Long): Int

    external fun audioOutStalled(handle: Long): Boolean

    external fun audioOutSessionId(handle: Long): Int

    /** The output's buffer in frames: the FIFO plus the stream's own. */
    external fun audioOutBufferFrames(handle: Long): Long
}
