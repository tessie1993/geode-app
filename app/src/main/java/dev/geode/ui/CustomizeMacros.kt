package dev.geode.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
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
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.onLongClick
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.data.MacroState
import dev.geode.data.MacroStore
import dev.geode.ui.theme.GeodeSpacing
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
        "Shake, grain and colour split",
    )

/**
 * Customize › Macros: six knobs laid over every style. They never move the detail sliders; the engine
 * (core/viz/Macros) folds them into the look, the sound engine's tuning, the camera's music moves and
 * a few reactions of their own, on screen and in exports, the wallpaper and the dream alike.
 *
 * Drag a knob up or down, double-tap it back to "as styled", long-press to keep it out of Surprise.
 * The sets (Calm, Groove…) and saving your own are a page of their own, [MacroSetsPage].
 */
@Composable
internal fun MacrosTab(
    state: MacroState,
    store: MacroStore,
) {
    Column {
        // No caption over the knobs (UI V2 rule 3): each knob's ⋮ › What is this says what it does.
        for (row in 0 until 2) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
                for (col in 0 until 3) {
                    val i = row * 3 + col
                    MacroKnob(
                        title = MACRO_TITLES[i],
                        about = MACRO_HINTS[i],
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
    }
}

/**
 * Macros › Sets & macros: the built-in sets and the user's own, a tap sets all six knobs at once.
 * Using a set is free; saving the knobs as a new one is Pro.
 */
@Composable
internal fun MacroSetsPage(
    state: MacroState,
    store: MacroStore,
    canSavePresets: Boolean,
    onLockedSave: () -> Unit,
) {
    var naming by remember { mutableStateOf(false) }
    val starters = remember { MacroStore.starters() }
    Column {
        // One dropdown row per shelf, the set in use checked (UI V2 rule 2), in place of two chip walls.
        DropdownRow(
            stringResource(R.string.macro_sets_built_in),
            starters.map { it.name },
            starters.indexOfFirst { sameKnobs(it.knobs, state.knobs) },
            placeholder = stringResource(R.string.macro_sets_custom),
        ) { store.setKnobs(starters[it].knobs) }
        if (state.presets.isNotEmpty()) YourSetsRow(state, store)
        TextButton(onClick = { if (canSavePresets) naming = true else onLockedSave() }) {
            Text(if (canSavePresets) "Save these knobs as a set" else "Save these knobs as a set (Pro)")
        }
    }
    if (naming) {
        var name by remember { mutableStateOf("") }
        AlertDialog(
            onDismissRequest = { naming = false },
            title = { Text("Name this set") },
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

/**
 * The user's own sets as one dropdown row: a tap on a name applies it, its bin deletes it. The bin is
 * a full 48 dp icon button inside the menu item, with the set's name in its TalkBack label.
 */
@Composable
private fun YourSetsRow(
    state: MacroState,
    store: MacroStore,
) {
    var open by remember { mutableStateOf(false) }
    val current = state.presets.firstOrNull { sameKnobs(it.knobs, state.knobs) }
    val name = stringResource(R.string.macro_sets_yours)
    val chooseLabel = stringResource(R.string.customize_a11y_choose, name)
    Box {
        Row(
            Modifier
                .fillMaxWidth()
                .heightIn(min = GeodeSpacing.TouchTarget)
                .clickable(role = Role.DropdownList, onClickLabel = chooseLabel) { open = true }
                .semantics(mergeDescendants = true) {},
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(name, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
            Text(
                current?.name ?: stringResource(R.string.macro_sets_count, state.presets.size),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Icon(Icons.Filled.ArrowDropDown, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            state.presets.forEach { p ->
                DropdownMenuItem(
                    text = { Text(p.name) },
                    leadingIcon =
                        if (p == current) {
                            {
                                Icon(
                                    Icons.Filled.Check,
                                    contentDescription = stringResource(R.string.customize_a11y_selected),
                                    tint = accentTextColor(),
                                )
                            }
                        } else {
                            null
                        },
                    trailingIcon = {
                        IconButton(onClick = { store.deletePreset(p.name) }) {
                            Icon(Icons.Filled.Delete, contentDescription = stringResource(R.string.macro_sets_delete, p.name))
                        }
                    },
                    onClick = {
                        open = false
                        store.setKnobs(p.knobs)
                    },
                )
            }
        }
    }
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
    about: String,
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
    val knobState = if (locked) stringResource(R.string.customize_a11y_locked, text) else text
    val lockLabel = stringResource(if (locked) R.string.customize_a11y_knob_unlock else R.string.customize_a11y_knob_lock)
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
                    stateDescription = knobState
                    progressBarRangeInfo = ProgressBarRangeInfo(value, -1f..1f, steps = KNOB_STEPS)
                    setProgress { target ->
                        onSet(target.coerceIn(-1f, 1f))
                        true
                    }
                    onLongClick(lockLabel) {
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
        // Name and value, then the ⋮: no caption (UI V2 rule 3), and the knobs keep one height.
        Text(title, style = MaterialTheme.typography.labelLarge)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            KnobMenu(
                title = title,
                about = about,
                centred = abs(value) < KNOB_EPSILON,
                locked = locked,
                onReset = { onSet(0f) },
                onToggleLock = onToggleLock,
            )
        }
    }
}

/** A knob's ⋮: What is this, Reset (while it is off centre) and Keep out of Surprise. */
@Composable
private fun KnobMenu(
    title: String,
    about: String,
    centred: Boolean,
    locked: Boolean,
    onReset: () -> Unit,
    onToggleLock: () -> Unit,
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
            DropdownMenuItem(text = { Text(stringResource(R.string.customize_what_is_this)) }, onClick = {
                open = false
                explaining = true
            })
            DropdownMenuItem(text = { Text("Reset") }, enabled = !centred, onClick = {
                open = false
                onReset()
            })
            DropdownMenuItem(
                text = { Text(stringResource(R.string.macro_keep_out_of_surprise)) },
                trailingIcon = { Checkbox(checked = locked, onCheckedChange = null) },
                onClick = {
                    open = false
                    onToggleLock()
                },
            )
        }
    }
    if (explaining) AboutDialog(title, about) { explaining = false }
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
