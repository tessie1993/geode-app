package dev.geode.ui.tour

import android.view.WindowManager
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
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
import androidx.compose.ui.graphics.drawscope.Stroke
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
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.paneTitle
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.window.DialogWindowProvider
import dev.geode.R
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.floatingGlass
import kotlin.math.roundToInt

/**
 * One pointer of the tour: the screen dims, the real control under [target] stays lit, and a
 * glass card beside it says what it does. Tapping the lit control is the same as [onPrimary].
 *
 * A dialog window, not a layer in the app's: TalkBack then stays in the card instead of wandering
 * into the app underneath, and system Back reaches it first (Back skips, like the button). The
 * dialog's own dim is switched off, since the spotlight is the dim. [target] is in screen pixels
 * ([TourTargets]); null centres the card with no spotlight, for a control not on screen.
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
        val window = (view.parent as? DialogWindowProvider)?.window
        SideEffect {
            window?.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND)
            window?.setDimAmount(0f)
        }
        val density = LocalDensity.current
        val primary by rememberUpdatedState(onPrimary)
        // Where this window's top-left sits on screen, so a screen-pixel target lands right.
        var origin by remember { mutableStateOf(Offset.Zero) }
        val hole =
            target?.let { r ->
                val pad = with(density) { HOLE_PADDING.toPx() }
                r.translate(-origin.x, -origin.y).inflate(pad)
            }
        val ring = MaterialTheme.colorScheme.primary
        Box(
            Modifier
                .fillMaxSize()
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
                drawRect(Color.Black.copy(alpha = SCRIM_ALPHA))
                hole?.let {
                    val corner = CornerRadius(HOLE_CORNER.toPx())
                    drawRoundRect(Color.Black, it.topLeft, it.size, corner, blendMode = BlendMode.Clear)
                    drawRoundRect(ring.copy(alpha = RING_ALPHA), it.topLeft, it.size, corner, style = Stroke(RING_WIDTH.toPx()))
                }
            }
            PointerPlacement(hole) {
                TourCard(title, body, index, count) {
                    TextButton(onClick = onSkip) { Text(skipLabel) }
                    Button(onClick = onPrimary) { Text(primaryLabel) }
                }
            }
        }
    }
}

/**
 * Puts the card under the spotlight when the control sits in the top half and there is room,
 * otherwise above it, and always inside the safe area with a margin; centred with no spotlight.
 */
@Composable
private fun PointerPlacement(
    hole: Rect?,
    card: @Composable () -> Unit,
) {
    val insets = WindowInsets.safeDrawing
    Layout(content = card, modifier = Modifier.fillMaxSize()) { measurables, constraints ->
        val margin = GeodeSpacing.L.roundToPx()
        val gap = GeodeSpacing.M.roundToPx()
        val top = insets.getTop(this) + margin
        val bottom = constraints.maxHeight - insets.getBottom(this) - margin
        val maxWidth = minOf(constraints.maxWidth - 2 * margin, CARD_MAX_WIDTH.roundToPx()).coerceAtLeast(0)
        val placeable = measurables.first().measure(Constraints(maxWidth = maxWidth))
        val w = placeable.width
        val h = placeable.height
        val centredY = (constraints.maxHeight - h) / 2
        val y =
            if (hole == null) {
                centredY
            } else {
                val below = hole.bottom.roundToInt() + gap
                val above = hole.top.roundToInt() - gap - h
                when {
                    hole.center.y < constraints.maxHeight / 2f && below + h <= bottom -> below
                    above >= top -> above
                    below + h <= bottom -> below
                    else -> centredY
                }
            }
        val x =
            if (hole == null) {
                (constraints.maxWidth - w) / 2
            } else {
                (hole.center.x.roundToInt() - w / 2).coerceIn(margin, (constraints.maxWidth - margin - w).coerceAtLeast(margin))
            }
        layout(constraints.maxWidth, constraints.maxHeight) {
            placeable.place(x, y.coerceIn(top, (bottom - h).coerceAtLeast(top)))
        }
    }
}

/** The pop-up itself: floating glass, the step count, a title, one or two lines, the buttons. */
@Composable
private fun TourCard(
    title: String,
    body: String,
    index: Int,
    count: Int,
    buttons: @Composable () -> Unit,
) {
    Column(
        Modifier
            .widthIn(max = CARD_MAX_WIDTH)
            .floatingGlass(fillAlpha = CARD_FILL)
            .semantics {
                paneTitle = title
                liveRegion = LiveRegionMode.Polite
            }.padding(GeodeSpacing.L),
        verticalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        if (count > 1) {
            Text(
                stringResource(R.string.tour_step_of, index, count),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        Text(title, style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.onSurface)
        Text(body, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S, Alignment.End)) { buttons() }
    }
}

/**
 * A small glass line at the top of the app while the guided path waits on the user to do
 * something in the app itself (tap a song). Not a dialog: the app underneath must stay usable.
 */
@Composable
internal fun TourHint(
    text: String,
    actionLabel: String,
    onAction: () -> Unit,
) {
    Box(Modifier.fillMaxSize().statusBarsPadding().padding(top = GeodeSpacing.S), contentAlignment = Alignment.TopCenter) {
        Row(
            Modifier
                .padding(horizontal = GeodeSpacing.L)
                .widthIn(max = CARD_MAX_WIDTH)
                .floatingGlass(fillAlpha = CARD_FILL)
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

private const val SCRIM_ALPHA = 0.62f
private const val RING_ALPHA = 0.9f
private const val CARD_FILL = 0.86f
private val HOLE_PADDING = 6.dp
private val HOLE_CORNER = 16.dp
private val RING_WIDTH = 2.dp
private val CARD_MAX_WIDTH = 360.dp
