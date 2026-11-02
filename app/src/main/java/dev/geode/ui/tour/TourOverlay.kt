package dev.geode.ui.tour

import android.os.Build
import android.view.WindowManager
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.MutableTransitionState
import androidx.compose.animation.core.VectorConverter
import androidx.compose.animation.fadeIn
import androidx.compose.animation.slideInVertically
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.BlendMode
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.CompositingStrategy
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.positionInWindow
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.paneTitle
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.window.DialogWindowProvider
import dev.geode.R
import dev.geode.ui.theme.GeodeColors
import dev.geode.ui.theme.GeodeElevation
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.floatingGlass
import dev.geode.ui.theme.performStoneHaptic
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * One pointer of the tour: the screen dims, the real control under [target] stays lit, and a
 * glass card beside it says what it does. Tapping the lit control is the same as [onPrimary].
 *
 * A dialog window, not a layer in the app's: TalkBack then stays in the card instead of wandering
 * into the app underneath, and system Back reaches it first (Back skips, like the button). The
 * dialog's own dim and zoom are switched off, since the spotlight is the dim and the motion is
 * ours. A screen's pointers reuse one dialog, so a step does not flash a window: the hole morphs
 * to the next control and the card follows. [target] is in screen pixels ([TourTargets]); null
 * centres the card with no spotlight, for a control not on screen.
 *
 * The last step of a run has no Skip: there is nothing left to skip. Back still ends it.
 */
@Composable
internal fun TourPointer(
    title: String,
    body: String,
    index: Int,
    count: Int,
    target: Rect?,
    primaryLabel: String,
    onPrimary: () -> Unit,
    skipLabel: String,
    onSkip: () -> Unit,
) {
    Dialog(
        onDismissRequest = onSkip,
        properties =
            DialogProperties(
                usePlatformDefaultWidth = false,
                decorFitsSystemWindows = false,
                dismissOnClickOutside = false,
            ),
    ) {
        val view = LocalView.current
        SideEffect { (view.parent as? DialogWindowProvider)?.window?.let(::fitSpotlightWindow) }
        val density = LocalDensity.current
        val motion = LocalGeodeMotion.current
        val last = count > 1 && index == count
        val primary by rememberUpdatedState {
            view.performStoneHaptic(if (last || count == 1) StoneHapticCue.CONFIRM else StoneHapticCue.TAP)
            onPrimary()
        }
        val skip = {
            view.performStoneHaptic(StoneHapticCue.TAP)
            onSkip()
        }
        // Where this window's top-left sits on screen, so a screen-pixel target lands right.
        var origin by remember { mutableStateOf(Offset.Zero) }
        val goal =
            target?.let { r ->
                with(density) { spotlight(r.translate(-origin.x, -origin.y), HOLE_PADDING.toPx(), MIN_HOLE.toPx()) }
            }
        // Morphs from one control to the next (the first one appears in place); the
        // reduced-motion scheme snaps it.
        val morph = remember { Animatable(Rect.Zero, Rect.VectorConverter) }
        LaunchedEffect(goal) {
            val to = goal ?: return@LaunchedEffect
            if (morph.value == Rect.Zero) morph.snapTo(to) else morph.animateTo(to, motion.defaultSpatialSpec())
        }
        val hole = goal?.let { morph.value.takeIf { it != Rect.Zero } ?: it }
        val shown = remember { Animatable(0f) }
        LaunchedEffect(Unit) { shown.animateTo(1f, motion.defaultEffectsSpec()) }
        Box(
            Modifier
                .fillMaxSize()
                .graphicsLayer { alpha = shown.value }
                .onGloballyPositioned { c ->
                    val onScreen = IntArray(2).also(view::getLocationOnScreen)
                    origin = c.positionInWindow() + Offset(onScreen[0].toFloat(), onScreen[1].toFloat())
                },
        ) {
            Canvas(
                Modifier
                    .fillMaxSize()
                    .graphicsLayer(compositingStrategy = CompositingStrategy.Offscreen)
                    .clearAndSetSemantics { }
                    .pointerInput(hole) { detectTapGestures { p -> if (hole?.contains(p) == true) primary() } },
            ) {
                drawRect(GeodeColors.Scrim.copy(alpha = SCRIM_ALPHA))
                hole?.let {
                    // One rule for every control: a pill for a nav item, a circle for an icon, the
                    // glass corner for anything big.
                    val corner = CornerRadius(minOf(it.height / 2f, GeodeRadius.Floating.toPx()))
                    drawRoundRect(Color.Black, it.topLeft, it.size, corner, blendMode = BlendMode.Clear)
                }
            }
            PointerPlacement(hole) {
                TourCard(
                    title = title,
                    body = body,
                    index = index,
                    count = count,
                    scale = ENTER_SCALE + (1f - ENTER_SCALE) * shown.value,
                ) {
                    if (!last) TextButton(onClick = skip) { Text(skipLabel) }
                    Button(onClick = primary) { Text(primaryLabel) }
                }
            }
        }
    }
}

/** The lit area around a control: padded, and never smaller than a touch target. */
private fun spotlight(
    r: Rect,
    pad: Float,
    min: Float,
): Rect {
    val padded = r.inflate(pad)
    val w = max(padded.width, min)
    val h = max(padded.height, min)
    return Rect.fromCenter(padded.center, w, h)
}

/** Full-screen, no platform dim or zoom, drawn under the camera cutout and a clear nav bar. */
private fun fitSpotlightWindow(window: android.view.Window) {
    window.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND)
    window.setDimAmount(0f)
    window.setWindowAnimations(0)
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
        window.attributes =
            window.attributes.apply {
                layoutInDisplayCutoutMode =
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                        WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
                    } else {
                        WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
                    }
            }
    }
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) window.isNavigationBarContrastEnforced = false
}

/**
 * Puts the card on whichever side of the spotlight has more room, a gap away, centred on it and
 * clamped inside the safe area; centred on screen with no spotlight. It follows the hole as it
 * morphs, so the card glides with it.
 */
@Composable
private fun PointerPlacement(
    hole: Rect?,
    card: @Composable () -> Unit,
) {
    val insets = WindowInsets.safeDrawing
    Layout(content = card, modifier = Modifier.fillMaxSize()) { measurables, constraints ->
        val compact = constraints.maxWidth < COMPACT_WIDTH.roundToPx()
        val margin = (if (compact) GeodeSpacing.EdgeInsetCompact else GeodeSpacing.EdgeInset).roundToPx()
        val gap = GeodeSpacing.M.roundToPx()
        val top = insets.getTop(this) + margin
        val bottom = constraints.maxHeight - insets.getBottom(this) - margin
        val width =
            (constraints.maxWidth - 2 * margin)
                .coerceIn(CARD_MIN_WIDTH.roundToPx(), CARD_MAX_WIDTH.roundToPx())
                .coerceAtMost(constraints.maxWidth)
        // Never taller than the safe area: the text scrolls, the buttons stay on screen.
        val placeable = measurables.first().measure(Constraints(width, width, 0, (bottom - top).coerceAtLeast(0)))
        val h = placeable.height
        val y =
            if (hole == null) {
                (constraints.maxHeight - h) / 2
            } else {
                val roomBelow = bottom - hole.bottom
                val roomAbove = hole.top - top
                if (roomBelow >= roomAbove) hole.bottom.roundToInt() + gap else hole.top.roundToInt() - gap - h
            }
        val x =
            if (hole == null) {
                (constraints.maxWidth - width) / 2
            } else {
                (hole.center.x.roundToInt() - width / 2).coerceIn(margin, (constraints.maxWidth - margin - width).coerceAtLeast(margin))
            }
        layout(constraints.maxWidth, constraints.maxHeight) {
            placeable.place(x, y.coerceIn(top, (bottom - h).coerceAtLeast(top)))
        }
    }
}

/**
 * The pop-up itself: floating glass, the title (a heading, so TalkBack lands on it), one or two
 * lines, then the step count at the start and the actions at the end, dismissive before confirm.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun TourCard(
    title: String,
    body: String,
    index: Int,
    count: Int,
    scale: Float,
    buttons: @Composable () -> Unit,
) {
    Column(
        Modifier
            .graphicsLayer {
                scaleX = scale
                scaleY = scale
            }.floatingGlass(elevation = GeodeElevation.Overlay)
            .semantics { paneTitle = title }
            .padding(start = GeodeSpacing.L, end = GeodeSpacing.L, top = GeodeSpacing.L, bottom = GeodeSpacing.S),
    ) {
        Column(Modifier.weight(1f, fill = false).verticalScroll(rememberScrollState())) {
            Text(
                title,
                Modifier.semantics { heading() },
                style = MaterialTheme.typography.titleSmall,
                color = MaterialTheme.colorScheme.onSurface,
            )
            Spacer(Modifier.height(GeodeSpacing.XS))
            // The dialog is reused between steps, so focus stays on Next: the new body is read out.
            Text(
                body,
                Modifier.semantics { liveRegion = LiveRegionMode.Polite },
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        Spacer(Modifier.height(GeodeSpacing.M))
        // Wraps at large font sizes instead of squeezing the last button.
        FlowRow(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S, Alignment.End),
        ) {
            if (count > 1) {
                Text(
                    stringResource(R.string.tour_step_of, index, count),
                    Modifier.weight(1f).align(Alignment.CenterVertically),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            buttons()
        }
    }
}

/**
 * A small glass line under the status bar while the guided path waits on the user to do
 * something in the app itself (tap a song), and for gesture tips. Not a dialog: the app
 * underneath must stay usable. It eases in; the reduced-motion scheme drops the slide.
 */
@Composable
internal fun TourHint(
    text: String,
    actionLabel: String,
    onAction: () -> Unit,
) {
    val motion = LocalGeodeMotion.current
    val visible = remember { MutableTransitionState(false).apply { targetState = true } }
    val slide = with(LocalDensity.current) { HINT_SLIDE.roundToPx() }
    Box(Modifier.fillMaxSize().statusBarsPadding().padding(top = GeodeSpacing.S), contentAlignment = Alignment.TopCenter) {
        AnimatedVisibility(
            visibleState = visible,
            enter = fadeIn(motion.defaultEffectsSpec()) + slideInVertically(motion.defaultSpatialSpec()) { -slide },
        ) {
            Row(
                Modifier
                    .padding(horizontal = GeodeSpacing.EdgeInset)
                    .widthIn(max = CARD_MAX_WIDTH)
                    // Denser than the pop-up's glass: there is no scrim under it, and the visual
                    // can be white.
                    .floatingGlass(fillAlpha = HINT_FILL)
                    .semantics { liveRegion = LiveRegionMode.Polite }
                    .padding(start = GeodeSpacing.L, end = GeodeSpacing.XS),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text,
                    Modifier.weight(1f, fill = false).padding(vertical = GeodeSpacing.M),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurface,
                )
                TextButton(onClick = onAction) { Text(actionLabel) }
            }
        }
    }
}

private const val SCRIM_ALPHA = 0.70f
private const val ENTER_SCALE = 0.94f
private const val HINT_FILL = 0.75f
private val HOLE_PADDING = 8.dp
private val MIN_HOLE = GeodeSpacing.TouchTarget
private val CARD_MIN_WIDTH = 280.dp
private val CARD_MAX_WIDTH = 360.dp
private val COMPACT_WIDTH = 360.dp
private val HINT_SLIDE = 8.dp
