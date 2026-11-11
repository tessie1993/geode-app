package dev.geode.ui.tour

import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.boundsInWindow
import androidx.compose.ui.node.CompositionLocalConsumerModifierNode
import androidx.compose.ui.node.GlobalPositionAwareModifierNode
import androidx.compose.ui.node.ModifierNodeElement
import androidx.compose.ui.node.currentValueOf
import androidx.compose.ui.platform.LocalView
import dev.geode.model.TourSpot

/**
 * Where each marked control is on screen right now, in screen pixels.
 *
 * Process-wide, like the one renderer: the tour draws in a dialog window of its own, so it
 * cannot read the app's layout through composition, and screen coordinates are the one frame
 * both windows share (split screen and freeform windows included). Snapshot state, so the
 * spotlight follows a control that moves (a sheet settling, a list scrolling).
 *
 * Each entry remembers which node wrote it: two nodes can briefly carry one spot while a screen
 * swaps its content, and the one leaving must not erase the one arriving.
 */
object TourTargets {
    private val bounds = mutableStateMapOf<TourSpot, Pair<Any, Rect>>()

    fun boundsOf(spot: TourSpot): Rect? = bounds[spot]?.second

    /** The marked controls laid out now. */
    val shown: Set<TourSpot> get() = bounds.keys.toSet()

    internal fun put(
        spot: TourSpot,
        owner: Any,
        rect: Rect,
    ) {
        val now = bounds[spot]
        if (now == null || now.first === owner) {
            if (now?.second != rect) bounds[spot] = owner to rect
        }
    }

    internal fun remove(
        spot: TourSpot,
        owner: Any,
    ) {
        if (bounds[spot]?.first === owner) bounds.remove(spot)
    }
}

/**
 * Marks this control as [spot], so the tour can light it: the tour never draws a copy of a
 * control. Costs one position callback per layout pass; a control that scrolls off screen or
 * leaves composition unregisters itself, and its step is then passed over.
 */
fun Modifier.tourTarget(spot: TourSpot): Modifier = this then TourTargetElement(spot)

private data class TourTargetElement(
    val spot: TourSpot,
) : ModifierNodeElement<TourTargetNode>() {
    override fun create(): TourTargetNode = TourTargetNode(spot)

    override fun update(node: TourTargetNode) {
        node.retarget(spot)
    }
}

private class TourTargetNode(
    private var spot: TourSpot,
) : Modifier.Node(),
    GlobalPositionAwareModifierNode,
    CompositionLocalConsumerModifierNode {
    private val origin = IntArray(2)

    override fun onGloballyPositioned(coordinates: LayoutCoordinates) {
        if (!coordinates.isAttached) return
        val inWindow = coordinates.boundsInWindow()
        if (inWindow.isEmpty) {
            TourTargets.remove(spot, this)
            return
        }
        currentValueOf(LocalView).getLocationOnScreen(origin)
        TourTargets.put(spot, this, inWindow.translate(origin[0].toFloat(), origin[1].toFloat()))
    }

    override fun onDetach() {
        TourTargets.remove(spot, this)
    }

    fun retarget(next: TourSpot) {
        if (next == spot) return
        TourTargets.remove(spot, this)
        spot = next
    }
}
