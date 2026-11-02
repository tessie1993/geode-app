package dev.geode.ui

import android.view.View
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.AnimationVector1D
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.Orientation
import androidx.compose.foundation.gestures.draggable
import androidx.compose.foundation.gestures.rememberDraggableState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.PrimaryScrollableTabRow
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.nestedscroll.NestedScrollConnection
import androidx.compose.ui.input.nestedscroll.NestedScrollSource
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.layout.layout
import androidx.compose.ui.layout.onLayoutRectChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.IntRect
import androidx.compose.ui.unit.Velocity
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.render.VisualizerView
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.floatingGlass
import dev.geode.ui.theme.glassBlur
import dev.geode.ui.theme.performStoneHaptic
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import kotlin.math.abs
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * The options over the full-screen visual: one glass surface, two layouts (V2 rule 5), one
 * [VisualsNavState] behind both.
 *
 * - Portrait ([side] false): a sheet at the bottom whose height is [heightPx], snapping between
 *   its tabs alone, half and tall ([sheetAnchors]). The header drags it, and so does a list that is
 *   already at its top: scrolling on down hands the drag to the sheet and the fling settles it
 *   ([SheetScrollConnection]), the way a Material bottom sheet behaves.
 * - Landscape ([side] true): a panel on the right, full height, [panelPx] wide (about 40 % of the
 *   width, 30–50 % by its leading edge). The visual stays full screen under it and shows through the
 *   glass; there is no scrim, so the visual keeps its gestures beside it. Dragging the edge far to
 *   the right, or flinging it there, hides the panel ([VisualsNavState.hideSheet]).
 *
 * Both sizes are read in the layout pass, so a drag or a settle never recomposes the page under it.
 * Both layouts keep the pages ([content]) at the same place in the tree, so turning the phone keeps
 * their scroll and state. TalkBack gets expand, collapse, wider, narrower and hide as custom actions,
 * since a drag is out of its reach.
 *
 * Over the visual the blur is the native one (see [SheetBlur]): [visualizerView] is told the
 * surface's bounds whenever they change, straight from the layout pass, with [blurActive] false
 * (hidden, PiP) turning it off and [glassAlpha] (the surface's visible alpha, read only in an effect)
 * fading it with the glass under a drop-in.
 */
@Composable
internal fun VisualsOptions(
    nav: VisualsNavState,
    side: Boolean,
    heightPx: Animatable<Float, AnimationVector1D>,
    anchors: Map<SheetHeight, Float>,
    panelPx: Animatable<Float, AnimationVector1D>,
    panelRange: ClosedFloatingPointRange<Float>,
    opacity: Float,
    visualizerView: VisualizerView,
    blurActive: Boolean,
    glassAlpha: () -> Float,
    onSettle: (SheetHeight) -> Unit,
    onPanelSettle: (widthPx: Float?) -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    val scope = rememberCoroutineScope()
    val maxPx = anchors.getValue(SheetHeight.TALL)
    // A drag never takes the sheet below its tabs (owner): hiding is the double tap, × and Fullscreen.
    val minPx = anchors.getValue(SheetHeight.PEEK)
    val currentAnchors by rememberUpdatedState(anchors)
    val currentSettle by rememberUpdatedState(onSettle)
    val listDrag =
        remember(heightPx) {
            SheetScrollConnection(
                heightPx = heightPx,
                scope = scope,
                restPx = { currentAnchors.getValue(nav.sheet) },
                minPx = { currentAnchors.getValue(SheetHeight.PEEK) },
                maxPx = { currentAnchors.getValue(SheetHeight.TALL) },
                onRelease = { velocityUp -> currentSettle(settleHeight(heightPx.value, velocityUp, currentAnchors)) },
            )
        }
    val shape = RoundedCornerShape(GeodeRadius.Floating)
    val blur = rememberSheetBlur(visualizerView, blurActive, glassAlpha)
    Box(
        modifier
            .layout { measurable, constraints ->
                if (side) {
                    val w = panelPx.value.roundToInt().coerceIn(0, constraints.maxWidth)
                    if (w == 0) {
                        layout(0, 0) {}
                    } else {
                        val placeable = measurable.measure(Constraints.fixed(w, constraints.maxHeight))
                        layout(w, placeable.height) { placeable.place(0, 0) }
                    }
                } else {
                    val h = heightPx.value.roundToInt().coerceIn(0, constraints.maxHeight)
                    if (h == 0) {
                        layout(constraints.maxWidth, 0) {}
                    } else {
                        val placeable = measurable.measure(constraints.copy(minHeight = h, maxHeight = h))
                        layout(placeable.width, h) { placeable.place(0, 0) }
                    }
                }
            }.glassBlur(shape)
            .floatingGlass(shape, fillAlpha = opacity),
    ) {
        // The column is exactly the glass (the node inside the paddings and the size above), so its
        // rect is the one the native blur needs; it arrives from the layout pass, never through state.
        Column(
            Modifier
                .fillMaxSize()
                .onLayoutRectChanged(throttleMillis = 0, debounceMillis = 0) { blur.onSheetMoved(it.boundsInWindow) },
        ) {
            OptionsHeader(
                nav = nav,
                side = side,
                onDrag = { delta -> scope.launch { heightPx.snapTo((heightPx.value - delta).coerceIn(minPx, maxPx)) } },
                onDragStopped = { velocity -> onSettle(settleHeight(heightPx.value, -velocity, anchors)) },
                onSettle = onSettle,
            )
            Box(
                Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .then(if (side) Modifier else Modifier.nestedScroll(listDrag)),
            ) { content() }
        }
        // After the pages, so they keep their place in the tree in both layouts.
        if (side) {
            PanelEdge(
                panelPx = panelPx,
                panelRange = panelRange,
                onSettle = onPanelSettle,
                modifier = Modifier.align(Alignment.CenterStart),
            )
        }
    }
}

/**
 * Builds the options surface's [SheetBlur] for [view] and feeds it everything but the rect (which
 * the surface's layout pass hands it): the strength, [glassAlpha] while [active] and 0 otherwise,
 * through one long-lived collector, so a fade costs one call per changed frame and no recomposition;
 * the view's own layout, so a rotation or a new host re-sends the rect against the view's new size;
 * and 0 on dispose, so leaving Visuals clears it.
 */
@Composable
private fun rememberSheetBlur(
    view: VisualizerView,
    active: Boolean,
    glassAlpha: () -> Float,
): SheetBlur {
    val cornerPx = with(LocalDensity.current) { GeodeRadius.Floating.toPx() }
    val blur = remember(view) { SheetBlur(view) }
    SideEffect { blur.onCorner(cornerPx) }
    val currentActive by rememberUpdatedState(active)
    val currentAlpha by rememberUpdatedState(glassAlpha)
    LaunchedEffect(blur) {
        snapshotFlow { if (currentActive) currentAlpha().coerceIn(0f, 1f) else 0f }
            .collect { blur.onStrength(it) }
    }
    DisposableEffect(blur) {
        val relayout = View.OnLayoutChangeListener { _, _, _, _, _, _, _, _, _ -> blur.push() }
        view.addOnLayoutChangeListener(relayout)
        onDispose {
            view.removeOnLayoutChangeListener(relayout)
            blur.clear()
        }
    }
    return blur
}

/**
 * The native blur's region for the options surface, kept outside composition: the surface's rect
 * comes from its layout pass ([onSheetMoved]), the strength from its visible alpha ([onStrength]),
 * and [push] sends [VisualizerView.setUiBlurRegion] only when the normalised rect, strength or
 * corner actually changed (one small event on the GL thread each).
 *
 * Both rects are read in the same space, the window: the surface's `boundsInWindow` and the view's
 * `getLocationInWindow`, re-read on every push. The rect used to live in composition state and reach
 * the GL thread through a LaunchedEffect keyed on it, relaunched every frame of a drag and run after
 * draw; when the bars hid, the surface dropped by the dock's reserve and the blur stayed where it had
 * been (285 px higher on the QA phone). Here every layout of the surface or the view sends it.
 */
private class SheetBlur(
    private val view: VisualizerView,
) {
    private val loc = IntArray(2)
    private var sheet = IntRect.Zero
    private var strength = 0f
    private var cornerPx = 0f
    private val next = FloatArray(SENT_FLOATS)
    private val sent = FloatArray(SENT_FLOATS) { Float.NaN }

    fun onSheetMoved(bounds: IntRect) {
        sheet = bounds
        push()
    }

    fun onStrength(value: Float) {
        strength = value
        push()
    }

    fun onCorner(px: Float) {
        cornerPx = px
        push()
    }

    fun push() {
        val w = view.width.toFloat()
        val h = view.height.toFloat()
        // A rect only once both are laid out, and only while the glass shows; anything else is off.
        val laidOut = !sheet.isEmpty && w > 0f && h > 0f
        if (laidOut && strength > 0f) {
            view.getLocationInWindow(loc)
            next[0] = (sheet.left - loc[0]) / w
            next[1] = (sheet.top - loc[1]) / h
            next[2] = (sheet.right - loc[0]) / w
            next[3] = (sheet.bottom - loc[1]) / h
            next[STRENGTH] = strength
        } else {
            next.fill(0f, 0, STRENGTH + 1)
        }
        next[CORNER] = cornerPx
        if (next.contentEquals(sent)) return
        next.copyInto(sent)
        view.setUiBlurRegion(next[0], next[1], next[2], next[3], next[STRENGTH], next[CORNER])
    }

    /** Off, whatever was sent before; the holder is done with. */
    fun clear() {
        sent.fill(Float.NaN)
        view.setUiBlurRegion(0f, 0f, 0f, 0f, 0f, 0f)
    }
}

private const val SENT_FLOATS = 6
private const val STRENGTH = 4
private const val CORNER = 5

/**
 * The surface's header: the grip (portrait), the page tabs and ×. In portrait it is the sheet's
 * drag handle. A tab tap on a sheet down to its tabs raises it to half ([VisualsNavState.openTab]).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun OptionsHeader(
    nav: VisualsNavState,
    side: Boolean,
    onDrag: (Float) -> Unit,
    onDragStopped: (Float) -> Unit,
    onSettle: (SheetHeight) -> Unit,
) {
    val view = LocalView.current
    val drag = rememberDraggableState(onDrag)
    val handle = stringResource(R.string.visuals_sheet_handle)
    val expand = stringResource(R.string.visuals_sheet_expand)
    val collapse = stringResource(R.string.visuals_sheet_collapse)
    val close = stringResource(R.string.visuals_sheet_close)
    Column(
        Modifier
            .fillMaxWidth()
            .then(
                if (side) {
                    Modifier
                } else {
                    Modifier
                        .draggable(drag, Orientation.Vertical, onDragStopped = { velocity -> onDragStopped(velocity) })
                        .semantics {
                            contentDescription = handle
                            customActions =
                                listOf(
                                    CustomAccessibilityAction(expand) {
                                        onSettle(if (nav.sheet == SheetHeight.HALF) SheetHeight.TALL else SheetHeight.HALF)
                                        true
                                    },
                                    CustomAccessibilityAction(collapse) {
                                        onSettle(if (nav.sheet == SheetHeight.TALL) SheetHeight.HALF else SheetHeight.PEEK)
                                        true
                                    },
                                    CustomAccessibilityAction(close) {
                                        onSettle(SheetHeight.HIDDEN)
                                        true
                                    },
                                )
                        }
                },
            ),
    ) {
        if (!side) {
            Box(
                Modifier
                    .padding(top = GeodeSpacing.S)
                    .size(width = GRIP_LENGTH, height = GRIP_THICKNESS)
                    .background(MaterialTheme.colorScheme.onSurfaceVariant, RoundedCornerShape(50))
                    .align(Alignment.CenterHorizontally),
            )
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            PrimaryScrollableTabRow(
                selectedTabIndex = nav.selectedTab,
                modifier = Modifier.weight(1f),
                edgePadding = GeodeSpacing.S,
                containerColor = Color.Transparent,
                contentColor = MaterialTheme.colorScheme.primary,
                // Plain glass: no hairline under the tabs, the indicator alone marks the page.
                divider = {},
            ) {
                VisualsTabs.forEachIndexed { i, page ->
                    val selected = i == nav.selectedTab
                    Tab(
                        selected = selected,
                        onClick = {
                            view.performStoneHaptic(StoneHapticCue.TAP)
                            nav.openTab(page)
                        },
                        selectedContentColor = MaterialTheme.colorScheme.primary,
                        unselectedContentColor = MaterialTheme.colorScheme.onSurfaceVariant,
                        text = { Text(stringResource(page.titleRes), style = MaterialTheme.typography.titleSmall) },
                    )
                }
            }
            IconButton(onClick = {
                view.performStoneHaptic(StoneHapticCue.TAP)
                onSettle(SheetHeight.HIDDEN)
            }) {
                StoneIconArt(StoneIcon.CLOSE, close)
            }
        }
    }
}

/**
 * The side panel's leading edge: a grip that resizes the panel between [panelRange] (px) and hides
 * it past its narrow end. Kept short and in the middle of the edge, so the rows beside it stay
 * tappable. [onSettle] gets the width it came to rest at, or null for hidden.
 */
@Composable
private fun PanelEdge(
    panelPx: Animatable<Float, AnimationVector1D>,
    panelRange: ClosedFloatingPointRange<Float>,
    onSettle: (Float?) -> Unit,
    modifier: Modifier = Modifier,
) {
    val scope = rememberCoroutineScope()
    val drag =
        rememberDraggableState { delta ->
            // Rightward (positive) narrows a panel that grows from the right.
            scope.launch { panelPx.snapTo((panelPx.value - delta).coerceIn(panelRange)) }
        }
    val handle = stringResource(R.string.visuals_panel_handle)
    val wider = stringResource(R.string.visuals_panel_wider)
    val narrower = stringResource(R.string.visuals_panel_narrower)
    val close = stringResource(R.string.visuals_sheet_close)
    val step = (panelRange.endInclusive - panelRange.start) / 2f
    Box(
        modifier
            .size(width = GeodeSpacing.XL, height = EDGE_TOUCH_HEIGHT)
            .draggable(
                drag,
                Orientation.Horizontal,
                onDragStopped = {
                    // Like the sheet, the panel's edge resizes it but never drags it away (owner).
                    onSettle(panelPx.value.coerceIn(panelRange))
                },
            ).semantics {
                contentDescription = handle
                customActions =
                    listOf(
                        CustomAccessibilityAction(wider) {
                            onSettle((panelPx.value + step).coerceIn(panelRange))
                            true
                        },
                        CustomAccessibilityAction(narrower) {
                            onSettle((panelPx.value - step).coerceIn(panelRange))
                            true
                        },
                        CustomAccessibilityAction(close) {
                            onSettle(null)
                            true
                        },
                    )
            },
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier
                .size(width = GRIP_THICKNESS, height = GRIP_LENGTH)
                .background(MaterialTheme.colorScheme.onSurfaceVariant, RoundedCornerShape(50)),
        )
    }
}

/**
 * Hands a list's drag to the sheet: a list at its top that is scrolled on down pulls the sheet
 * down with it, a finger going back up raises the sheet to where it rested before the list scrolls
 * again, and the release settles the sheet at a height ([onRelease], upward velocity in px/s).
 * Only the user's own drag moves it; a fling that runs into the top of the list stops there.
 */
private class SheetScrollConnection(
    private val heightPx: Animatable<Float, AnimationVector1D>,
    private val scope: CoroutineScope,
    private val restPx: () -> Float,
    private val minPx: () -> Float,
    private val maxPx: () -> Float,
    private val onRelease: (Float) -> Unit,
) : NestedScrollConnection {
    private var moved = false

    override fun onPreScroll(
        available: Offset,
        source: NestedScrollSource,
    ): Offset {
        if (source != NestedScrollSource.UserInput || available.y >= 0f) return Offset.Zero
        val below = restPx() - heightPx.value
        if (below <= 0f) return Offset.Zero
        val up = min(-available.y, below)
        raise(up)
        return Offset(0f, -up)
    }

    override fun onPostScroll(
        consumed: Offset,
        available: Offset,
        source: NestedScrollSource,
    ): Offset {
        if (source != NestedScrollSource.UserInput || available.y <= 0f) return Offset.Zero
        raise(-available.y)
        return Offset(0f, available.y)
    }

    override suspend fun onPreFling(available: Velocity): Velocity {
        if (!moved) return Velocity.Zero
        moved = false
        onRelease(-available.y)
        return available
    }

    private fun raise(px: Float) {
        moved = true
        scope.launch { heightPx.snapTo((heightPx.value + px).coerceIn(minPx(), maxPx())) }
    }
}

/** The sheet's resting heights in px for [roomPx] of room: its tabs alone, half, nearly all. */
internal fun sheetAnchors(
    roomPx: Float,
    peekPx: Float,
): Map<SheetHeight, Float> =
    mapOf(
        SheetHeight.HIDDEN to 0f,
        SheetHeight.PEEK to peekPx.coerceAtMost(roomPx),
        SheetHeight.HALF to (roomPx * HALF_FRACTION).coerceAtLeast(peekPx).coerceAtMost(roomPx),
        SheetHeight.TALL to roomPx,
    )

/**
 * Where a drag released at [px] comes to rest: the next height up or down on a fling, else the
 * nearest. [velocityUp] is px/s, positive upward. A drag or fling never hides the sheet: it rests at
 * its tabs at the lowest (owner), and only the double tap, × or Fullscreen hide it.
 */
internal fun settleHeight(
    px: Float,
    velocityUp: Float,
    anchors: Map<SheetHeight, Float>,
): SheetHeight {
    val ordered = SheetHeight.entries.filter { it != SheetHeight.HIDDEN }.sortedBy { anchors.getValue(it) }
    return when {
        velocityUp > FLING_PX_PER_S -> ordered.firstOrNull { anchors.getValue(it) > px + 1f } ?: ordered.last()
        velocityUp < -FLING_PX_PER_S -> ordered.lastOrNull { anchors.getValue(it) < px - 1f } ?: ordered.first()
        else -> ordered.minBy { abs(anchors.getValue(it) - px) }
    }
}

/**
 * The side panel's width range in px for [widthPx] of window: 30–50 % of it (the supporting-pane
 * split's "about a third" up to half), never narrower than [PANEL_MIN] (the cards stop reading)
 * nor wider than [PANEL_MAX], and never more than half the window.
 */
internal fun panelRange(
    widthPx: Float,
    minPx: Float,
    maxPx: Float,
): ClosedFloatingPointRange<Float> {
    val half = widthPx * PANEL_MAX_FRACTION
    val low = (widthPx * PANEL_MIN_FRACTION).coerceAtLeast(minPx).coerceAtMost(half)
    val high = half.coerceAtMost(maxPx).coerceAtLeast(low)
    return low..high
}

// Peek: the grip and the tabs row, the sheet's header alone.
internal val SHEET_PEEK = 72.dp

internal const val PANEL_DEFAULT_FRACTION = 0.4f
internal val PANEL_MIN = 280.dp
internal val PANEL_MAX = 480.dp

private const val PANEL_MIN_FRACTION = 0.3f
private const val PANEL_MAX_FRACTION = 0.5f

private const val HALF_FRACTION = 0.5f

private const val FLING_PX_PER_S = 1200f

private val GRIP_LENGTH = 36.dp

private val GRIP_THICKNESS = 4.dp

private val EDGE_TOUCH_HEIGHT = 96.dp
