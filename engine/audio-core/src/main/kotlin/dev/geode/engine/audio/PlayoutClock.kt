package dev.geode.engine.audio

import dev.geode.engine.bridge.GeodeNative

/**
 * How far into a [SampleRing] epoch the listener has heard. [SoundCursor] hands its engine nothing past
 * it, so a renderer reacts to a beat when the speaker plays it rather than when the player's sink buffers
 * it (#349). An interface so the cursor's tests can stand in for the native clock.
 */
fun interface Playout {
    /** The newest frame of ring epoch [epoch] that has been heard, given the ring holds [written] of it. */
    fun heardFrame(
        epoch: Int,
        written: Long,
    ): Long
}

/**
 * The handle on `core/audio/PlayoutClock`, where the delay maths lives: Kotlin only passes on what the
 * player's sink reports (its position, play state, speed, the time of each epoch's first buffer) and asks
 * for the heard frame. One per player, made once and never freed, because GL threads (the wallpaper, the
 * dream) may still read it after the playback session that fed it is gone; [reset] starts it over.
 */
class PlayoutClock : Playout {
    private val handle: Long = GeodeNative.playoutCreate()

    fun reset() = GeodeNative.playoutReset(handle)

    /** Playback thread: the player's tap began ring epoch [epoch] at [rateHz]. */
    fun beginEpoch(
        epoch: Int,
        rateHz: Int,
    ) = GeodeNative.playoutBeginEpoch(handle, epoch, rateHz)

    /** Playback thread: the sink handled a buffer that starts at [mediaUs]. */
    fun bufferHandled(mediaUs: Long) = GeodeNative.playoutBuffer(handle, mediaUs)

    fun flush() = GeodeNative.playoutFlush(handle)

    /** Playback thread: the sink's position, in media time, as of now. */
    fun position(mediaUs: Long) = GeodeNative.playoutPosition(handle, mediaUs, System.nanoTime())

    fun playing(on: Boolean) = GeodeNative.playoutPlaying(handle, on, System.nanoTime())

    fun speed(speed: Float) = GeodeNative.playoutSpeed(handle, speed)

    override fun heardFrame(
        epoch: Int,
        written: Long,
    ): Long = GeodeNative.playoutHeardFrame(handle, epoch, written, System.nanoTime())
}
