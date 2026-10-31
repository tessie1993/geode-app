package dev.geode.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.InputChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
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
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.onLongClick
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import dev.geode.data.MacroPreset
import dev.geode.data.MacroState
import dev.geode.data.MacroStore
import kotlin.math.abs
import kotlin.math.roundToInt

/** The six knobs' names and what each does, in core/viz/Macros order. Display only. */
private val MACRO_TITLES = listOf("Energy", "Speed", "Complexity", "Mood", "Space", "Chaos")
private val MACRO_HINTS =
    listOf(
        "How hard it hits the music",
        "How fast everything runs",
        "More pattern and detail",
        "Warmer, richer colour",
        "Wider view, bigger moves",
        "Shake, glitch and cuts",
    )

/**
 * Customize › Macros: six knobs laid over every style. They never move the detail sliders; the engine
 * (core/viz/Macros) folds them into the look, the sound engine's tuning, the camera's music moves and
 * a few reactions of their own, on screen and in exports, the wallpaper and the dream alike.
 *
 * Drag a knob up or down, double-tap it back to "as styled", long-press to keep it out of Surprise.
 */
@Composable
internal fun MacrosTab(
    state: MacroState,
    store: MacroStore,
    canSavePresets: Boolean,
    onLockedSave: () -> Unit,
) {
    var naming by remember { mutableStateOf(false) }
    val starters = remember { MacroStore.starters() }
    Column {
        Text(
            "Six knobs over every style. Centre is the look as styled; turn right for more, left for less. " +
                "Double-tap a knob to centre it, long-press to keep it out of Surprise.",
            modifier = Modifier.padding(top = 8.dp, bottom = 8.dp),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        for (row in 0 until 2) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
                for (col in 0 until 3) {
                    val i = row * 3 + col
                    MacroKnob(
                        title = MACRO_TITLES[i],
                        hint = MACRO_HINTS[i],
                        value = state.knobs[i],
                        locked = state.isLocked(i),
                        onChange = { store.setKnob(i, it, persist = false) },
                        onCommit = store::commit,
                        onSet = { store.setKnob(i, it) },
                        onToggleLock = { store.toggleLock(i) },
                        modifier = Modifier.weight(1f),
                    )
                }
            }
        }
        Text("Presets", style = MaterialTheme.typography.titleSmall, modifier = Modifier.padding(top = 12.dp))
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            starters.forEach { p -> PresetChip(p, state, store) }
            state.presets.forEach { p ->
                InputChip(
                    selected = sameKnobs(p.knobs, state.knobs),
                    onClick = { store.setKnobs(p.knobs) },
                    label = { Text(p.name, style = MaterialTheme.typography.labelSmall) },
                    trailingIcon = {
                        Text(
                            "×",
                            modifier =
                                Modifier
                                    .pointerInput(p.name) { detectTapGestures { store.deletePreset(p.name) } }
                                    .semantics { contentDescription = "Delete ${p.name}" },
                        )
                    },
                )
            }
        }
        TextButton(onClick = { if (canSavePresets) naming = true else onLockedSave() }) {
            Text(if (canSavePresets) "Save these knobs as a preset" else "Save these knobs as a preset (Pro)")
        }
    }
    if (naming) {
        var name by remember { mutableStateOf("") }
        AlertDialog(
            onDismissRequest = { naming = false },
            title = { Text("Name this macro preset") },
            text = { OutlinedTextField(value = name, onValueChange = { name = it }, singleLine = true) },
            confirmButton = {
                TextButton(enabled = name.isNotBlank(), onClick = {
                    store.savePreset(name)
                    naming = false
                }) { Text("Save") }
            },
            dismissButton = { TextButton(onClick = { naming = false }) { Text("Cancel") } },
        )
    }
}

@Composable
private fun PresetChip(
    preset: MacroPreset,
    state: MacroState,
    store: MacroStore,
) {
    FilterChip(
        selected = sameKnobs(preset.knobs, state.knobs),
        onClick = { store.setKnobs(preset.knobs) },
        label = { Text(preset.name, style = MaterialTheme.typography.labelSmall) },
    )
}

private fun sameKnobs(
    a: List<Float>,
    b: List<Float>,
): Boolean = a.size == b.size && a.indices.all { abs(a[it] - b[it]) < KNOB_EPSILON }

/** "As styled", or how far off centre, for the label and TalkBack. */
private fun knobText(v: Float): String = if (abs(v) < KNOB_EPSILON) "as styled" else "%+d%%".format((v * PERCENT).roundToInt())

@Composable
private fun MacroKnob(
    title: String,
    hint: String,
    value: Float,
    locked: Boolean,
    onChange: (Float) -> Unit,
    onCommit: () -> Unit,
    onSet: (Float) -> Unit,
    onToggleLock: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val current by rememberUpdatedState(value)
    val change by rememberUpdatedState(onChange)
    // A full sweep, -1 to 1, is two of these: a comfortable thumb travel.
    val unitPx = with(LocalDensity.current) { KNOB_DRAG_PER_UNIT.toPx() }
    val track = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.25f)
    val arc = MaterialTheme.colorScheme.primary
    val text = knobText(value)
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
                    detectTapGestures(onDoubleTap = { onSet(0f) }, onLongPress = { onToggleLock() })
                }.semantics {
                    contentDescription = title
                    stateDescription = text + if (locked) ", locked" else ""
                    progressBarRangeInfo = ProgressBarRangeInfo(value, -1f..1f, steps = KNOB_STEPS)
                    setProgress { target ->
                        onSet(target.coerceIn(-1f, 1f))
                        true
                    }
                    onLongClick(if (locked) "Unlock for Surprise" else "Lock against Surprise") {
                        onToggleLock()
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
                drawArc(track, START_DEG, SWEEP_DEG, false, topLeft, arcSize, style = Stroke(stroke, cap = StrokeCap.Round))
                // From the top (as styled) toward the turn.
                drawArc(arc, CENTRE_DEG, value * HALF_SWEEP_DEG, false, topLeft, arcSize, style = Stroke(stroke, cap = StrokeCap.Round))
            }
            if (locked) Icon(Icons.Filled.Lock, contentDescription = null, modifier = Modifier.size(16.dp))
        }
        Text(title, style = MaterialTheme.typography.labelLarge)
        Text(text, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(
            hint,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
            modifier = Modifier.height(32.dp),
        )
        if (abs(value) >= KNOB_EPSILON) {
            TextButton(onClick = { onSet(0f) }, modifier = Modifier.semantics { contentDescription = "Reset $title" }) {
                Text("Reset", style = MaterialTheme.typography.labelSmall)
            }
        }
    }
}

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
