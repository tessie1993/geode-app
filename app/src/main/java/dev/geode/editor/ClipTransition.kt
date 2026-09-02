package dev.geode.editor

/**
 * The transition a clip opens with, from whatever ended on the same lane before it. [id] names an
 * entry of `gl_transitions.json`; the picture freezes on the previous clip's last frame while it runs.
 */
data class ClipTransition(
    val id: String,
    val durationMs: Long = DEFAULT_TRANSITION_MS,
) {
    val boundedDurationMs: Long get() = durationMs.coerceIn(MIN_TRANSITION_MS, MAX_TRANSITION_MS)

    companion object {
        const val MIN_TRANSITION_MS: Long = 100L
        const val MAX_TRANSITION_MS: Long = 5_000L
        const val DEFAULT_TRANSITION_MS: Long = 800L
        val DURATION_CHOICES_MS: List<Long> = listOf(400L, 800L, 1_500L, 3_000L)
    }
}

/** The clip that ends before [clip] on its lane, if any: the one a transition would come from. */
fun Lane.predecessorOf(clip: Clip): Clip? = clips.filter { it.id != clip.id && it.endMs <= clip.startMs }.maxByOrNull { it.endMs }
