package dev.geode.engine.bridge

// The JNI declarations of geode_playout_jni.cpp.
object PlayoutNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // Playout clock (core/sound/interp/PlayoutClock, #349): the ring sample the speaker plays now.

    /** One clock per player, freed with [playoutDestroy]. */
    external fun playoutCreate(): Long

    external fun playoutDestroy(handle: Long)

    /** The sink handled a buffer at [presentationUs]; [IngestNative.pcmRingHead] read before and after it. */
    external fun playoutRingSpan(
        handle: Long,
        presentationUs: Long,
        headBefore: Long,
        headAfter: Long,
    )

    /** The sink's position in media time, read at [nowNs]; `AudioSink.CURRENT_POSITION_NOT_SET` is ignored. */
    external fun playoutPosition(
        handle: Long,
        sinkUs: Long,
        nowNs: Long,
    )

    external fun playoutPlaying(
        handle: Long,
        playing: Boolean,
        nowNs: Long,
    )

    external fun playoutFlush(handle: Long)

    external fun playoutSpeed(
        handle: Long,
        speed: Float,
    )

    /** The user's A/V offset, clamped to ±[playoutMaxOffsetMs]; positive shows the picture later. */
    external fun playoutSetOffsetMs(
        handle: Long,
        ms: Int,
    )

    /** The offset's bound in ms, for Settings' Picture delay slider. */
    external fun playoutMaxOffsetMs(): Int

    /** Cast: while [on], the TV plays the ring as written and every reader takes the newest sample again. */
    external fun playoutSetBypass(
        handle: Long,
        on: Boolean,
    )

    /** Wall ms from [nowNs] until the newest sample the sink took is heard; 0 with no map, 2000 at most. */
    external fun playoutLagMs(
        handle: Long,
        nowNs: Long,
    ): Int
}
