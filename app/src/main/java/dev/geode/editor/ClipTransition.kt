package dev.geode.editor

/**
 * The transition a clip opens with, from whatever ended on the same lane before it. [id] names an
 * entry of `gl_transitions.json`; the picture freezes on the previous clip's last frame while it runs.
 *
 * [onBeat] moves the change to the first beat at or after the clip's start, so a crossfade or wipe
 * lands on the music; with [HARD_CUT] it is the hard cut on the beat.
 */
data class ClipTransition(
    val id: String,
    val durationMs: Long = DEFAULT_TRANSITION_MS,
    val onBeat: Boolean = false,
) {
    val boundedDurationMs: Long get() = durationMs.coerceIn(MIN_TRANSITION_MS, MAX_TRANSITION_MS)

    companion object {
        const val MIN_TRANSITION_MS: Long = 100L
        const val MAX_TRANSITION_MS: Long = 5_000L
        const val DEFAULT_TRANSITION_MS: Long = 800L

        /** No blend at all: the next clip simply starts. Meaningful with [onBeat]. */
        const val HARD_CUT: String = "cut"
    }
}
