package dev.geode.ui.studio

import androidx.compose.runtime.Immutable
import androidx.compose.ui.unit.Dp

/** Maps timeline milliseconds to pixels inside the scrolling lane content. */
@Immutable
data class TimelineScale(
    val pxPerMs: Float,
    val contentMs: Long,
) {
    fun xOf(ms: Long): Float = ms * pxPerMs

    fun msOf(x: Float): Long = (x / pxPerMs).toLong().coerceIn(0L, contentMs)

    fun widthOf(ms: Long): Float = ms * pxPerMs

    val contentPx: Float get() = xOf(contentMs)

    /** How many milliseconds a finger-sized nudge covers; the snap radius follows the zoom. */
    fun msOf(
        dp: Dp,
        density: Float,
    ): Long = (dp.value * density / pxPerMs).toLong().coerceAtLeast(1L)

    companion object {
        const val MIN_PX_PER_MS = 0.005f
        const val MAX_PX_PER_MS = 1.2f
        const val DEFAULT_PX_PER_MS = 0.04f
    }
}
