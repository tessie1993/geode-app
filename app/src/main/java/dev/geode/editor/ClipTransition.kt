package dev.geode.editor

/**
 * The transition a clip opens with, from whatever ended on the same lane before it. [id] names an
 * entry of `gl_transitions.json`; the picture freezes on the previous clip's last frame while it runs.
 *
 * [onBeat] moves the change to the first beat at or after the clip's start, so a crossfade or wipe
 * lands on the music; with [HARD_CUT] it is the hard cut on the beat.
 *
 * Only the saved shape is left: nothing draws a transition between clips since the Studio project
 * render, which ran it as a GL Transition through Media3, went with the video editor (ADR 0007).
 */
data class ClipTransition(
    val id: String,
    val durationMs: Long = DEFAULT_TRANSITION_MS,
    val onBeat: Boolean = false,
) {
    companion object {
        const val DEFAULT_TRANSITION_MS: Long = 800L

        /** No blend at all: the next clip simply starts. Meaningful with [onBeat]. */
        const val HARD_CUT: String = "cut"
    }
}
