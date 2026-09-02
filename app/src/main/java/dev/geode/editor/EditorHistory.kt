package dev.geode.editor

/**
 * Undo as "keep the previous value": every edit pushes the whole [EditorProject], because a ripple
 * moves markers and keyframes together with the clips and a timeline-only stack would pull them apart.
 */
data class EditorHistory(
    val present: EditorProject,
    val past: List<EditorProject> = emptyList(),
    val future: List<EditorProject> = emptyList(),
) {
    val canUndo: Boolean get() = past.isNotEmpty()

    val canRedo: Boolean get() = future.isNotEmpty()

    /** A no-op edit (same value) leaves the stacks alone, so a refused operation never eats a redo. */
    fun push(
        next: EditorProject,
        limit: Int = DEFAULT_LIMIT,
    ): EditorHistory =
        if (next == present) {
            this
        } else {
            copy(present = next, past = (past + present).takeLast(limit), future = emptyList())
        }

    fun undo(): EditorHistory =
        if (past.isEmpty()) this else copy(present = past.last(), past = past.dropLast(1), future = listOf(present) + future)

    fun redo(): EditorHistory =
        if (future.isEmpty()) this else copy(present = future.first(), past = past + present, future = future.drop(1))

    companion object {
        const val DEFAULT_LIMIT = 100
    }
}
