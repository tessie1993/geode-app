package dev.geode.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.data.MacroBankState
import dev.geode.data.MacroKnob
import dev.geode.data.MacroStore
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * Quick knobs: the macro bank's knobs (core/viz/MacroBank), global over every style. Each adds its
 * offsets to the params it links, on screen and in exports, the wallpaper and the dream alike; the
 * names, hints and values come from the bank, and every turn is a native edit through [MacroStore].
 *
 * Drag a knob up or down, double-tap it back to centre. Making and editing knobs is a later page.
 */
@Composable
internal fun MacrosTab(
    state: MacroBankState,
    store: MacroStore,
) {
    Column {
        // No caption over the knobs (UI V2 rule 3): each knob's ⋮ › What is this says what it does.
        state.knobs.chunked(KNOBS_PER_ROW).forEach { row ->
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
                row.forEach { knob ->
                    MacroKnobDial(
                        knob = knob,
                        onChange = { store.setValue(knob.id, it, persist = false) },
                        onCommit = store::commit,
                        onSet = { store.setValue(knob.id, it) },
                        modifier = Modifier.weight(1f),
                    )
                }
                repeat(KNOBS_PER_ROW - row.size) { Spacer(Modifier.weight(1f)) }
            }
        }
    }
}

/** Centre, or how far off it, for the label and TalkBack. */
private fun knobText(v: Float): String = if (abs(v) < KNOB_EPSILON) "as styled" else "%+d%%".format((v * PERCENT).roundToInt())

@Composable
private fun MacroKnobDial(
    knob: MacroKnob,
    onChange: (Float) -> Unit,
    onCommit: () -> Unit,
    onSet: (Float) -> Unit,
    modifier: Modifier = Modifier,
) {
    val current by rememberUpdatedState(knob.value)
    val change by rememberUpdatedState(onChange)
    // A full sweep, -1 to 1, is two of these: a comfortable thumb travel.
    val unitPx = with(LocalDensity.current) { KNOB_DRAG_PER_UNIT.toPx() }
    val track = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.25f)
    val arc = MaterialTheme.colorScheme.primary
    val text = knobText(knob.value)
    Column(modifier.padding(vertical = 6.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Box(
            Modifier
                .size(KNOB_SIZE)
                .pointerInput(Unit) {
                    detectVerticalDragGestures(onDragEnd = onCommit) { c, dy ->
                        c.consume()
                        change((current - dy / unitPx).coerceIn(-1f, 1f))
                    }
                }.pointerInput(Unit) {
                    detectTapGestures(onDoubleTap = { onSet(0f) })
                }.semantics {
                    contentDescription = knob.name
                    stateDescription = text
                    progressBarRangeInfo = ProgressBarRangeInfo(knob.value, -1f..1f, steps = KNOB_STEPS)
                    setProgress { target ->
                        onSet(target.coerceIn(-1f, 1f))
                        true
                    }
                },
            contentAlignment = Alignment.Center,
        ) {
            Canvas(Modifier.size(KNOB_SIZE)) {
                val stroke = KNOB_STROKE.toPx()
                val inset = stroke / 2f
                val arcSize = Size(size.width - stroke, size.height - stroke)
                val topLeft = Offset(inset, inset)
                val line = Stroke(stroke, cap = StrokeCap.Round)
                drawArc(track, START_DEG, SWEEP_DEG, false, topLeft, arcSize, style = line)
                // From the top (centre) toward the turn.
                drawArc(arc, CENTRE_DEG, knob.value * HALF_SWEEP_DEG, false, topLeft, arcSize, style = line)
            }
        }
        // Name and value, then the ⋮: no caption (UI V2 rule 3), and the knobs keep one height.
        Text(knob.name, style = MaterialTheme.typography.labelLarge, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            KnobMenu(
                title = knob.name,
                about = knob.hint,
                centred = abs(knob.value) < KNOB_EPSILON,
                onReset = { onSet(0f) },
            )
        }
    }
}

/** A knob's ⋮: What is this (when the knob has a hint) and Reset (while it is off centre). */
@Composable
private fun KnobMenu(
    title: String,
    about: String,
    centred: Boolean,
    onReset: () -> Unit,
) {
    var open by remember { mutableStateOf(false) }
    var explaining by remember { mutableStateOf(false) }
    Box {
        IconButton(onClick = { open = true }) {
            Icon(
                Icons.Filled.MoreVert,
                contentDescription = stringResource(R.string.customize_a11y_more_for, title),
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            if (about.isNotEmpty()) {
                DropdownMenuItem(text = { Text(stringResource(R.string.customize_what_is_this)) }, onClick = {
                    open = false
                    explaining = true
                })
            }
            DropdownMenuItem(text = { Text("Reset") }, enabled = !centred, onClick = {
                open = false
                onReset()
            })
        }
    }
    if (explaining) AboutDialog(title, about) { explaining = false }
}

private const val KNOBS_PER_ROW = 2
private val KNOB_SIZE = 72.dp
private val KNOB_STROKE = 6.dp
private val KNOB_DRAG_PER_UNIT = 110.dp
private const val KNOB_EPSILON = 0.005f
private const val KNOB_STEPS = 39
private const val PERCENT = 100f
private const val START_DEG = 135f
private const val SWEEP_DEG = 270f
private const val CENTRE_DEG = 270f
private const val HALF_SWEEP_DEG = 135f
