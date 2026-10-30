package dev.geode.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AutoAwesome
import androidx.compose.material.icons.filled.BlurOn
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.Brightness6
import androidx.compose.material.icons.filled.Layers
import androidx.compose.material.icons.filled.Palette
import androidx.compose.material.icons.filled.Shuffle
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material.icons.filled.Waves
import androidx.compose.material.icons.filled.ZoomOutMap
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.render.scene.MacroSet
import dev.geode.render.scene.UserMacro
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * The first page of Customize: the active macro set as a grid of twist knobs, title under each.
 *
 * Turning a knob writes the real parameters (see [MacroController]), so the sliders in the other
 * tabs move with it and the change saves, undoes and exports like any other edit. Drag up or
 * down to turn; double-tap to put a knob back where it rests.
 */
@Composable
internal fun MacrosTab(
    set: MacroSet,
    onTurn: (Int, Float) -> Unit,
) {
    val query = LocalParamQuery.current.trim()
    Column {
        if (query.isBlank()) {
            Column(Modifier.fillMaxWidth().padding(top = 8.dp)) {
                Text(set.name, style = MaterialTheme.typography.titleSmall)
                Text(
                    "Drag a knob up or down. Double-tap puts it back.",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
        val shown = set.macros.withIndex().filter { query.isBlank() || it.value.name.contains(query, ignoreCase = true) }
        BoxWithConstraints(Modifier.fillMaxWidth().padding(top = 12.dp)) {
            val columns = if (maxWidth > WIDE_GRID) WIDE_COLUMNS else COLUMNS
            val cell = maxWidth / columns
            FlowRow(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                shown.forEach { (index, macro) ->
                    Box(Modifier.width(cell), contentAlignment = Alignment.TopCenter) {
                        MacroKnobCell(macro) { onTurn(index, it) }
                    }
                }
            }
        }
    }
}

@Composable
private fun MacroKnobCell(
    macro: UserMacro,
    onTurn: (Float) -> Unit,
) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        MacroKnob(macro, onTurn)
        Text(
            macro.name,
            modifier = Modifier.padding(top = 6.dp),
            style = MaterialTheme.typography.labelLarge,
            textAlign = TextAlign.Center,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            macroValueText(macro, macro.value),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

/** One twist knob: a 270° track, the turned arc from the rest position, and the macro's icon. */
@Composable
private fun MacroKnob(
    macro: UserMacro,
    onTurn: (Float) -> Unit,
) {
    val cs = MaterialTheme.colorScheme
    val value by rememberUpdatedState(macro.value)
    val rest by rememberUpdatedState(macro.rest)
    val turn by rememberUpdatedState(onTurn)
    val travelPx = with(LocalDensity.current) { DRAG_TRAVEL.toPx() }
    val shown = macroValueText(macro, macro.value)
    Box(
        modifier =
            Modifier
                .size(KNOB)
                .semantics {
                    contentDescription = macro.name
                    stateDescription = shown
                    progressBarRangeInfo = ProgressBarRangeInfo(macro.value, 0f..1f, steps = 0)
                    setProgress { target ->
                        turn(target.coerceIn(0f, 1f))
                        true
                    }
                }.pointerInput(Unit) {
                    detectTapGestures(onDoubleTap = { turn(rest) })
                }.pointerInput(Unit) {
                    // Accumulated here rather than read back from the set: several drag events can
                    // arrive before the knob recomposes, and each must build on the last.
                    var dragged = 0f
                    detectVerticalDragGestures(onDragStart = { dragged = value }) { change, dy ->
                        change.consume()
                        dragged = (dragged - dy / travelPx).coerceIn(0f, 1f)
                        turn(dragged)
                    }
                },
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.size(KNOB)) {
            val stroke = KNOB_STROKE.toPx()
            val arcSize = Size(size.width - stroke, size.height - stroke)
            val topLeft = Offset(stroke / 2f, stroke / 2f)
            val style = Stroke(stroke, cap = StrokeCap.Round)
            drawArc(cs.surfaceVariant, START_DEG, SWEEP_DEG, false, topLeft, arcSize, style = style)
            val restDeg = START_DEG + SWEEP_DEG * macro.rest
            val valueDeg = START_DEG + SWEEP_DEG * macro.value
            drawArc(cs.primary, minOf(restDeg, valueDeg), abs(valueDeg - restDeg), false, topLeft, arcSize, style = style)
            val radius = size.minDimension / 2f - stroke * POINTER_INSET
            val rad = Math.toRadians(valueDeg.toDouble())
            drawCircle(
                cs.onSurface,
                radius = stroke * POINTER_SIZE,
                center = Offset(center.x + radius * cos(rad).toFloat(), center.y + radius * sin(rad).toFloat()),
            )
        }
        Icon(macroIcon(macro.icon), contentDescription = null, tint = cs.onSurface, modifier = Modifier.size(22.dp))
    }
}

/** The glyph for a [dev.geode.render.scene.MacroIcons] id. */
internal fun macroIcon(id: String): ImageVector =
    when (id) {
        "bolt" -> Icons.Filled.Bolt
        "speed" -> Icons.Filled.Speed
        "layers" -> Icons.Filled.Layers
        "mood" -> Icons.Filled.Brightness6
        "space" -> Icons.Filled.ZoomOutMap
        "chaos" -> Icons.Filled.Shuffle
        "palette" -> Icons.Filled.Palette
        "camera" -> Icons.Filled.Videocam
        "waves" -> Icons.Filled.Waves
        "sparkle" -> Icons.Filled.AutoAwesome
        "blur" -> Icons.Filled.BlurOn
        else -> Icons.Filled.Tune
    }

/** "as styled" at rest; a signed step for a bipolar knob, a percentage for the rest. */
internal fun macroValueText(
    macro: UserMacro,
    value: Float,
): String {
    if (abs(value - macro.rest) < REST_EPSILON) return "as styled"
    return if (macro.bipolar) {
        "%+d".format(((value - macro.rest) / (1f - macro.rest) * PERCENT).roundToInt())
    } else {
        "${(value * PERCENT).roundToInt()}%"
    }
}

private val KNOB = 76.dp
private val KNOB_STROKE = 6.dp
private val DRAG_TRAVEL = 220.dp
private val WIDE_GRID = 560.dp
private const val COLUMNS = 3
private const val WIDE_COLUMNS = 6
private const val START_DEG = 135f
private const val SWEEP_DEG = 270f
private const val POINTER_INSET = 2.2f
private const val POINTER_SIZE = 0.6f
private const val REST_EPSILON = 0.005f
private const val PERCENT = 100f
