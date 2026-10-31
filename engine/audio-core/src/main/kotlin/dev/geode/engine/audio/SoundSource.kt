package dev.geode.engine.audio

import dev.geode.engine.bridge.GeodeNative

/**
 * One surface's sound engine (`core/sound`): feeds it what the [SampleRing] gained since the last frame and
 * steps it once per rendered frame into [frame], a `GeodeSoundFrame` as floats ([SoundFrameLayout]).
 *
 * One per surface (screen, wallpaper, dream), owned by the thread that renders it, because the engine is
 * frame-locked: its smoothing scales by the dt it is stepped with, so two surfaces sharing one would each
 * see it run at the sum of their frame rates. Export does not use this class; it writes the decoded track
 * straight into its own engine and steps at 1/fps, through the same native code.
 *
 * Not thread-safe. [close] on the same thread, or after that thread has stopped.
 */
class SoundSource(
    private val ring: SampleRing,
) {
    private val cursor = SoundCursor(ring)
    private var handle = 0L
    private var rate = 0
    private var tuning: FloatArray? = null

    /** The last stepped frame. Zero (silence) until the first [step]. */
    val frame = FloatArray(SoundFrameLayout.FLOATS)

    /**
     * The stream's rate as the tap reports it. A change builds a new engine, since its FFT size and
     * filter bank are fixed at creation.
     */
    @Volatile
    var sampleRateHz: Int = DEFAULT_SAMPLE_RATE_HZ

    /** User tuning (Settings › Audio), [SoundFrameLayout.TUNING_FLOATS] long; applied before the next step. */
    @Volatile
    private var pendingTuning: FloatArray? = null

    fun setTuning(values: FloatArray) {
        require(values.size >= SoundFrameLayout.TUNING_FLOATS) { "tuning needs ${SoundFrameLayout.TUNING_FLOATS} floats" }
        pendingTuning = values.copyOf()
    }

    fun step(dtSeconds: Float): FloatArray {
        val wanted = sampleRateHz
        if (handle == 0L || wanted != rate) {
            if (handle != 0L) GeodeNative.soundDestroy(handle)
            handle = GeodeNative.soundCreate(wanted)
            rate = wanted
            tuning?.let { GeodeNative.soundSetTuning(handle, it) }
            cursor.reset()
        }
        if (handle == 0L) return frame
        pendingTuning?.let {
            pendingTuning = null
            tuning = it
            GeodeNative.soundSetTuning(handle, it)
        }
        val take = cursor.take()
        if (take.restarted) GeodeNative.soundReset(handle)
        if (take.frames > 0) GeodeNative.soundWrite(handle, cursor.interleaved, take.frames, cursor.channels)
        GeodeNative.soundStep(handle, dtSeconds.coerceIn(0f, MAX_DT_SECONDS), frame)
        return frame
    }

    fun close() {
        if (handle != 0L) GeodeNative.soundDestroy(handle)
        handle = 0L
    }

    companion object {
        const val DEFAULT_SAMPLE_RATE_HZ = 48_000

        /** A frame longer than this (a paused surface, a debugger) steps as this, so envelopes don't leap. */
        const val MAX_DT_SECONDS = 0.1f
    }
}
