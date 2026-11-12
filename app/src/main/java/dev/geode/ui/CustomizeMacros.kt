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
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.MacroBankState
import dev.geode.data.MacroEditorKnob
import dev.geode.data.MacroLiveMod
import dev.geode.data.MacroStore
import dev.geode.model.KnobSize
import dev.geode.ui.theme.GeodeColors
import dev.geode.ui.theme.LocalReducedMotion
import kotlinx.coroutines.delay
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * Quick knobs: the macro bank's knobs (core/viz/MacroBank), global over every style. Each adds its
 * offsets to the params it links, on screen and in exports, the wallpaper and the dream alike; the
 * names, hints and values come from the bank, and every turn is a native edit through [MacroStore].
 *
 * Drag a knob up or down, double-tap it back to centre, long-press it (or TalkBack's Edit knob) for its
 * editor ([KnobEditorDialog]); its ⋮ takes it off screen into the bank's library. Add macro under them
 * ([AddMacroButton]) makes a new one or puts one back from the library. The knobs on screen are native's
 * (the editor text), drawn at Settings › Look's knob size. Editing is the knob editor's, so [lock] gates it as
 * it gates Edit knobs.
 */
@Composable
internal fun MacrosTab(
    state: MacroBankState,
    store: MacroStore,
    sceneId: String,
    lock: ProLock,
) {
    var editing by rememberSaveable { mutableIntStateOf(NO_KNOB) }
    val editAllowed = lock.allows(EDITOR_KEY)
    val settings: SettingsViewModel = geodeViewModel()
    val general by settings.general.collectAsStateWithLifecycle()
    val dial = dialOf(general.knobSize)
    val editor = remember(state, sceneId) { store.editor(sceneId) }
    val draftId = state.draft?.id
    // The knobs' live modulation, polled while they are on screen; under reduced motion read once, for the still range.
    val reduced = LocalReducedMotion.current
    val live by produceState(emptyMap<Int, MacroLiveMod>(), reduced, state) {
        value = store.liveMods()
        while (!reduced) {
            delay(LIVE_POLL_MS)
            value = store.liveMods()
        }
    }
    Column {
        // No caption over the knobs (UI V2 rule 3): each knob's ⋮ › What is this says what it does.
        editor.knobs.filter { it.id != draftId }.chunked(dial.perRow).forEach { row ->
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
                row.forEach { knob ->
                    MacroKnobDial(
                        knob = knob,
                        live = live[knob.id],
                        reduced = reduced,
                        dial = dial,
                        onChange = { store.setValue(knob.id, it, persist = false) },
                        onCommit = store::commit,
                        onSet = { store.setValue(knob.id, it) },
                        onEdit = { if (editAllowed) editing = knob.id else lock.onLocked() },
                        onRemove = { store.hide(knob.id) },
                        modifier = Modifier.weight(1f),
                    )
                }
                repeat(dial.perRow - row.size) { Spacer(Modifier.weight(1f)) }
            }
        }
        AddMacroButton(state, editor, store, sceneId, allowed = editAllowed, onLocked = lock.onLocked)
    }
    if (editing != NO_KNOB) KnobEditorDialog(editing, state, store, sceneId) { editing = NO_KNOB }
}

private const val NO_KNOB = -1
private const val LIVE_POLL_MS = 33L

/** Centre, or how far off it, for the label and TalkBack. */
private fun knobText(v: Float): String = if (abs(v) < KNOB_EPSILON) "as styled" else "%+d%%".format((v * PERCENT).roundToInt())

/** A dial's drawn size and stroke, and how many share a row. */
private class Dial(
    val size: Dp,
    val stroke: Dp,
    val perRow: Int,
)

private fun dialOf(size: KnobSize): Dial =
    when (size) {
        KnobSize.SMALL -> Dial(KNOB_SIZE / 2, KNOB_STROKE / 2, KNOBS_PER_ROW_SMALL)
        KnobSize.MEDIUM -> Dial(KNOB_SIZE * MEDIUM_SCALE, KNOB_STROKE * MEDIUM_SCALE, KNOBS_PER_ROW_MEDIUM)
        KnobSize.LARGE -> Dial(KNOB_SIZE, KNOB_STROKE, KNOBS_PER_ROW)
    }

/** The second arc's colour: who turns the knob besides the user's hand. */
private fun modColour(sources: Int): Color =
    when (sources) {
        MacroLiveMod.WAVE -> GeodeColors.ModWave
        MacroLiveMod.ENVELOPE -> GeodeColors.ModEnvelope
        else -> GeodeColors.ModBoth
    }

@Composable
private fun liveText(
    live: MacroLiveMod?,
    turn: Float,
): String? {
    live ?: return null
    val now = knobText((turn + live.add).coerceIn(-1f, 1f))
    return when (live.sources) {
        MacroLiveMod.WAVE -> stringResource(R.string.macro_knob_moved_by_wave, now)
        MacroLiveMod.ENVELOPE -> stringResource(R.string.macro_knob_moved_by_envelope, now)
        else -> stringResource(R.string.macro_knob_moved_by_both, now)
    }
}

/**
 * A knob's modulation, Serum's way, inside the user's arc: the range the waves and envelopes can reach as a faint
 * ring, and (unless reduced motion stills it) where they have it now as a bright arc from the user's turn.
 */
private fun DrawScope.drawModulation(
    live: MacroLiveMod,
    turn: Float,
    reduced: Boolean,
    stroke: Float,
) {
    val colour = modColour(live.sources)
    val inset = stroke * MOD_INSET
    val arcSize = Size(size.width - 2f * inset, size.height - 2f * inset)
    val topLeft = Offset(inset, inset)
    val line = Stroke(stroke * MOD_STROKE, cap = StrokeCap.Round)
    val low = (turn + live.low).coerceIn(-1f, 1f)
    val high = (turn + live.high).coerceIn(-1f, 1f)
    val start = CENTRE_DEG + low * HALF_SWEEP_DEG
    drawArc(colour.copy(alpha = MOD_RANGE_ALPHA), start, (high - low) * HALF_SWEEP_DEG, false, topLeft, arcSize, style = line)
    if (reduced) return
    val now = (turn + live.add).coerceIn(-1f, 1f)
    drawArc(colour, CENTRE_DEG + turn * HALF_SWEEP_DEG, (now - turn) * HALF_SWEEP_DEG, false, topLeft, arcSize, style = line)
}

@Composable
private fun MacroKnobDial(
    knob: MacroEditorKnob,
    live: MacroLiveMod?,
    reduced: Boolean,
    dial: Dial,
    onChange: (Float) -> Unit,
    onCommit: () -> Unit,
    onSet: (Float) -> Unit,
    onEdit: () -> Unit,
    onRemove: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val edit by rememberUpdatedState(onEdit)
    val editLabel = stringResource(R.string.macro_edit_knob)
    val current by rememberUpdatedState(knob.value)
    val change by rememberUpdatedState(onChange)
    // A full sweep, -1 to 1, is two of these: a comfortable thumb travel.
    val unitPx = with(LocalDensity.current) { KNOB_DRAG_PER_UNIT.toPx() }
    val track = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.25f)
    val arc = MaterialTheme.colorScheme.primary
    val text = knobText(knob.value)
    val spoken = liveText(live, knob.value)?.let { "$text, $it" } ?: text
    Column(modifier.padding(vertical = 6.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Box(
            Modifier
                .size(dial.size)
                .pointerInput(Unit) {
                    detectVerticalDragGestures(onDragEnd = onCommit) { c, dy ->
                        c.consume()
                        change((current - dy / unitPx).coerceIn(-1f, 1f))
                    }
                }.pointerInput(Unit) {
                    detectTapGestures(onDoubleTap = { onSet(0f) }, onLongPress = { edit() })
                }.semantics {
                    contentDescription = knob.name
                    customActions = listOf(CustomAccessibilityAction(editLabel) { true.also { edit() } })
                    stateDescription = spoken
                    progressBarRangeInfo = ProgressBarRangeInfo(knob.value, -1f..1f, steps = KNOB_STEPS)
                    setProgress { target ->
                        onSet(target.coerceIn(-1f, 1f))
                        true
                    }
                },
            contentAlignment = Alignment.Center,
        ) {
            Canvas(Modifier.size(dial.size)) {
                val stroke = dial.stroke.toPx()
                val inset = stroke / 2f
                val arcSize = Size(size.width - stroke, size.height - stroke)
                val topLeft = Offset(inset, inset)
                val line = Stroke(stroke, cap = StrokeCap.Round)
                drawArc(track, START_DEG, SWEEP_DEG, false, topLeft, arcSize, style = line)
                // From the top (centre) toward the turn.
                drawArc(arc, CENTRE_DEG, knob.value * HALF_SWEEP_DEG, false, topLeft, arcSize, style = line)
                if (live != null) drawModulation(live, knob.value, reduced, stroke)
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
                onRemove = onRemove,
            )
        }
    }
}

/** A knob's ⋮: What is this (when the knob has a hint), Reset (while it is off centre) and Remove from screen. */
@Composable
private fun KnobMenu(
    title: String,
    about: String,
    centred: Boolean,
    onReset: () -> Unit,
    onRemove: () -> Unit,
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
            DropdownMenuItem(text = { Text(stringResource(R.string.macro_remove_from_screen)) }, onClick = {
                open = false
                onRemove()
            })
        }
    }
    if (explaining) AboutDialog(title, about) { explaining = false }
}

private const val KNOBS_PER_ROW = 2
private const val KNOBS_PER_ROW_MEDIUM = 3
private const val KNOBS_PER_ROW_SMALL = 4
private const val MEDIUM_SCALE = 0.75f
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
private const val MOD_INSET = 1.9f
private const val MOD_STROKE = 0.6f
private const val MOD_RANGE_ALPHA = 0.35f
