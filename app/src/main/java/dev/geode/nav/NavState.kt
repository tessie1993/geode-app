package dev.geode.nav

/**
 * One immutable snapshot of where the person is. Every section keeps its own stack, so switching
 * away and back returns to the same page; the overlays sit over whichever section is current;
 * a gate, when up, sits over all of it.
 */
data class NavState(
    val section: Section = Section.PLAYER,
    val stacks: Map<Section, List<Destination>> = Section.entries.associateWith { listOf(it.root) },
    val overlays: List<Overlay> = emptyList(),
    val gate: Gate? = null,
) {
    init {
        require(Section.entries.all { !stacks[it].isNullOrEmpty() }) { "every section needs a stack with its root" }
    }

    /** The current section's stack, root first. */
    val stack: List<Destination> get() = stacks.getValue(section)

    /** The page on top of the current section. */
    val current: Destination get() = stack.last()

    /** The overlay on top, if any. */
    val overlay: Overlay? get() = overlays.lastOrNull()

    /** True while [Navigator.back] has something to do; false means the system should handle it. */
    val canGoBack: Boolean
        get() =
            when {
                gate != null -> gate.dismissible
                overlays.isNotEmpty() -> true
                stack.size > 1 -> true
                else -> section != Section.PLAYER
            }
}
