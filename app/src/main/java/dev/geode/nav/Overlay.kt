package dev.geode.nav

/**
 * Surfaces that cover the whole shell, section switcher included, without belonging to any
 * section's stack. They stack in the order opened and back closes the top one.
 */
sealed interface Overlay {
    /** Search across the library; picking a result navigates and closes it. */
    data object Search : Overlay

    /** The visualizer expanded over everything: the full-screen now-playing mode. */
    data object Visualizer : Overlay

    /** Export controls and progress; closing this surface leaves foreground rendering running. */
    data object Export : Overlay
}
