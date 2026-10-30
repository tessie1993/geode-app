package dev.geode.engine.audio

/**
 * Decides which [SampleRing] frames the live analysis feeds next, from the audio that has actually
 * been written, so live frames are spaced by exactly [hopFrames] samples of the stream the way
 * offline ones are.
 *
 * The loop that calls [plan] wakes about once per hop, but its clock is not the audio's: the tap
 * delivers in decoder-sized chunks, a 2x playback speed delivers twice as fast, and the loop can be
 * starved. Reading "the newest window" on a wall-clock tick, as this replaced, re-analysed the same
 * window between deliveries and while paused, skipped audio when a tick ran late, and counted
 * ticks instead of samples, so the tempo moved with the scheduler. Here every sample is fed once,
 * in order:
 * - one hop per wake while audio is waiting;
 * - extra hops when more is waiting than the source's own delivery size explains (catching up);
 * - nothing while the next hop has not arrived yet, which is a normal gap between deliveries;
 * - [Step.silence] once nothing has arrived for longer than the largest delivery seen, which is a
 *   source that stopped (pause, a stalled capture). The analysis then hears silence, not a frozen
 *   copy of the last window.
 *
 * Pure: no ring, no clock, no threads. The caller passes the ring's counters in.
 */
class HopCursor(
    private val hopFrames: Int,
    private val windowFrames: Int,
) {
    init {
        require(hopFrames in 1..windowFrames) { "hop $hopFrames must be in 1..$windowFrames" }
    }

    /** Push [frames] ring frames starting at [fromFrame] (none if 0); or, if [silence], one hop of zeros. */
    data class Step(
        val fromFrame: Long,
        val frames: Int,
        val silence: Boolean = false,
    )

    private var epoch = Int.MIN_VALUE
    private var next = UNSET
    private var lastWritten = 0L

    /** The largest delivery lately: how far behind the cursor may sit before it catches up. */
    private var burst = 0L

    /** The largest delivery since [restart]: how long a gap can be before it counts as silence. */
    private var peakBurst = 0L

    private var idleFrames = 0L

    /** Forget the position: the next [plan] starts from the newest window. For a seek or a reset. */
    fun restart() {
        next = UNSET
        idleFrames = 0L
        peakBurst = 0L
        burst = 0L
    }

    fun plan(
        writtenFrames: Long,
        epoch: Int,
        oldestAvailable: Long = 0L,
    ): Step {
        if (epoch != this.epoch) {
            // A new epoch mid-run is a gapless track change: its frame 0 follows straight on.
            val continuing = this.epoch != Int.MIN_VALUE && next != UNSET
            this.epoch = epoch
            lastWritten = 0L
            next = if (continuing) 0L else UNSET
        }
        if (next == UNSET || next < oldestAvailable) {
            // First start, a restart, or the ring lapped a stalled loop: begin at the newest window.
            lastWritten = writtenFrames
            if (writtenFrames < windowFrames) return NOTHING
            next = writtenFrames
            idleFrames = 0L
            return Step(writtenFrames - windowFrames, windowFrames)
        }

        val delivered = writtenFrames - lastWritten
        lastWritten = writtenFrames
        burst = maxOf(burst - hopFrames, delivered, 0L)
        peakBurst = maxOf(peakBurst, delivered)

        val waiting = writtenFrames - next
        if (waiting < hopFrames) {
            idleFrames += hopFrames
            return if (idleFrames > peakBurst + hopFrames) Step(next, 0, silence = true) else NOTHING
        }
        idleFrames = 0L
        var hops = 1
        while (waiting - hops.toLong() * hopFrames >= hopFrames && waiting - hops.toLong() * hopFrames > burst + hopFrames) hops++
        val from = next
        next += hops.toLong() * hopFrames
        return Step(from, hops * hopFrames)
    }

    private companion object {
        const val UNSET = -1L
        val NOTHING = Step(0L, 0)
    }
}
