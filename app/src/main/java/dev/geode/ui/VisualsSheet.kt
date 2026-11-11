package dev.geode.ui

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
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.PrimaryScrollableTabRow
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.nestedscroll.NestedScrollConnection
import androidx.compose.ui.input.nestedscroll.NestedScrollSource
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.layout.boundsInWindow
import androidx.compose.ui.layout.layout
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.Velocity
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.render.VisualizerView
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.edgeFade
import dev.geode.ui.theme.floatingGlass
import dev.geode.ui.theme.glassBlur
import dev.geode.ui.theme.performStoneHaptic
import dev.geode.ui.tour.GestureTip
import dev.geode.ui.tour.GestureTips
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
 * Over the visual the blur is the native one (see [UiBlurRegion]): [visualizerView] is told the
 * surface's bounds whenever they change, with [blurActive] false (hidden, PiP) turning it off.
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
    onSettle: (SheetHeight) -> Unit,
    onPanelSettle: (widthPx: Float?) -> Unit,
    modifier: Modifier = Modifier,
    bottomBar: (@Composable () -> Unit)? = null,
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
    // Not Compose state: the bounds change every frame of a drag or settle, and a state read here
    // recomposed the options and restarted an effect each time. The sink pushes from the layout callback.
    val blurSink = rememberUiBlurSink(visualizerView, blurActive)
    var bounds by blurSink::bounds
    // One glass slab: the sheet, and under it the docked mini player ([bottomBar]) when there is
    // one. A hidden sheet lays out at zero height, which leaves the player alone as a pill.
    Column(
        modifier
            .onGloballyPositioned { bounds = it.boundsInWindow() }
            .glassBlur(shape)
            .floatingGlass(shape, fillAlpha = opacity),
    ) {
        Box(
            Modifier
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
                },
        ) {
            Column(Modifier.fillMaxSize()) {
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
        bottomBar?.invoke()
    }
}

/**
 * Tells the native blur where the options surface is ([UiBlurSink.bounds], in the window) so the
 * live visual frosts under it. Only a change of the bounds or of [active] calls through (one small
 * event on the GL thread each); strength is 1 while the surface is shown, 0 when it is hidden or
 * has no size, and 0 again on dispose so leaving Visuals clears it. The bounds are written from
 * layout, outside composition, so a drag does not recompose the sheet.
 */
@Composable
private fun rememberUiBlurSink(
    view: VisualizerView,
    active: Boolean,
): UiBlurSink {
    val cornerPx = with(LocalDensity.current) { GeodeRadius.Floating.toPx() }
    val sink = remember(view) { UiBlurSink(view) }
    SideEffect { sink.update(active, cornerPx) }
    DisposableEffect(view) {
        onDispose { view.setUiBlurRegion(0f, 0f, 0f, 0f, 0f, 0f) }
    }
    return sink
}

private class UiBlurSink(
    private val view: VisualizerView,
) {
    private var active = false
    private var cornerPx = 0f

    var bounds: Rect? = null
        set(value) {
            if (field != value) {
                field = value
                push()
            }
        }

    fun update(
        active: Boolean,
        cornerPx: Float,
    ) {
        if (this.active == active && this.cornerPx == cornerPx) return
        this.active = active
        this.cornerPx = cornerPx
        push()
    }

    private fun push() {
        val loc = IntArray(2)
        view.getLocationInWindow(loc)
        val w = view.width.toFloat()
        val h = view.height.toFloat()
        // A rect only once laid out and measured; anything else turns the blur off.
        val rect = bounds?.takeIf { !it.isEmpty && w > 0f && h > 0f }
        if (rect == null || !active) {
            view.setUiBlurRegion(0f, 0f, 0f, 0f, 0f, cornerPx)
        } else {
            view.setUiBlurRegion(
                (rect.left - loc[0]) / w,
                (rect.top - loc[1]) / h,
                (rect.right - loc[0]) / w,
                (rect.bottom - loc[1]) / h,
                1f,
                cornerPx,
            )
        }
    }
}

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
            val tabScroll = rememberScrollState()
            PrimaryScrollableTabRow(
                selectedTabIndex = nav.selectedTab,
                modifier = Modifier.weight(1f).edgeFade(tabScroll, horizontal = true),
                scrollState = tabScroll,
                // Tabs as wide as their labels with no edge padding, so all four fit beside × on a
                // phone instead of reading "tyles" or "MilkDr" at the scroll window's ends. Where
                // they still overflow (a large font, the narrow side panel), the cut ends fade.
                edgePadding = 0.dp,
                minTabWidth = 0.dp,
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
                    ) {
                        // The content overload: Material's text one pads 16 dp a side, more than four
                        // labels and × have room for on a 400 dp phone.
                        Box(Modifier.height(TAB_HEIGHT).padding(horizontal = TAB_TEXT_PADDING), contentAlignment = Alignment.Center) {
                            Text(stringResource(page.titleRes), style = MaterialTheme.typography.titleSmall, maxLines = 1)
                        }
                    }
                }
            }
            IconButton(onClick = {
                view.performStoneHaptic(StoneHapticCue.TAP)
                GestureTips.nudge(GestureTip.DOUBLE_TAP_SHEET)
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
 *
 * The touch area is [GeodeSpacing.TouchTarget] wide (48 dp, Material's minimum target) while the
 * grip stays where it was drawn, centred in the edge's first [GeodeSpacing.XL]: a 24 dp strip was
 * easy to miss with a thumb, and moving the grip would only shift the miss.
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
            .size(width = GeodeSpacing.TouchTarget, height = EDGE_TOUCH_HEIGHT)
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
        contentAlignment = Alignment.CenterStart,
    ) {
        Box(
            Modifier
                .padding(start = (GeodeSpacing.XL - GRIP_THICKNESS) / 2)
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

/** A tab's height: Material's text-only tab, and the 48 dp touch target. */
private val TAB_HEIGHT = 48.dp

/** Each side of a tab's label. */
private val TAB_TEXT_PADDING = 8.dp
