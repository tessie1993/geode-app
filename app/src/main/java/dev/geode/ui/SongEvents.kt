package dev.geode.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.scale
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.engine.bridge.ResponderNative
import dev.geode.engine.bridge.StyleChipSwitch
import dev.geode.model.SceneParams
import dev.geode.ui.theme.GeodeSpacing
import kotlinx.coroutines.delay
import kotlin.math.roundToInt

/**
 * Customize › Music › Song events: the page of the Event Responder (core/viz/modulation/Responder). One row per event
 * with its live light, its name and its switch; under it a chip for each response it plays, the style's own reactions
 * as dimmed chips with a switch each, and "+ Add response". A chip opens its controls; each change is one edit that
 * native applies to the saved reactions text, and the text goes through the reactions' own save path.
 *
 * Every word, range, default, unit and preset is native's ([ResponseCatalogue], `geode_resp_field_text`); this draws
 * them and hands each touch over. Every row starts off, and adding a response is what puts a row on.
 */
@Composable
internal fun SongEventsPage(
    p: SceneParams,
    reactions: String?,
    handMask: Int,
    vm: VisualsViewModel,
) {
    if (LocalParamQuery.current.isNotBlank()) return
    val catalogue = ResponseCatalogue.live
    val sceneId = LocalSceneId.current
    val seed = p.int("reactSeed")
    val look = remember(p) { p.frame() }
    val rows =
        remember(reactions, sceneId, seed, handMask) {
            ResponderNative.responderRows(reactions, sceneId, seed, handMask)
                ?: FloatArray(0)
        }
    var open by rememberSaveable { mutableStateOf("") }
    var adding by rememberSaveable { mutableIntStateOf(-1) }
    var undo by remember { mutableStateOf<RowUndo?>(null) }
    // The event lights and the cost, as the last frame left them.
    val status = produceState(FloatArray(0)) {
        while (true) {
            value = vm.responderStatus()
            delay(STATUS_POLL_MS)
        }
    }
    Column {
        SectionHeader(stringResource(R.string.song_events_header))
        ControlHint(stringResource(R.string.song_events_hint))
        CostMeter { status.value }
        var category = -1
        catalogue.rows.forEach { info ->
            if (info.category != category) {
                category = info.category
                Text(
                    catalogue.categories.getOrElse(category) { "" },
                    modifier = Modifier.padding(top = GeodeSpacing.M).semantics { heading() },
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            val on = rows.getOrElse(info.row * ResponderNative.RESP_ROW_VALUES + ROW_ON) { 0f } > 0.5f
            EventRow(
                info = info,
                on = on,
                light = { status.value.getOrElse(info.row) { 0f } },
                chips = remember(reactions, sceneId, seed, handMask) { listedOf(reactions, sceneId, seed, handMask, info.row) },
                open = open,
                look = look,
                catalogue = catalogue,
                vm = vm,
                onOpen = { open = if (open == it) "" else it },
                onAdd = { adding = info.row },
                onReset = { undo = RowUndo(info.row, info.label, reactions) },
            )
        }
        undo?.let { saved ->
            LaunchedEffect(saved) {
                delay(UNDO_MS)
                undo = null
            }
            UndoBar(stringResource(R.string.song_events_row_cleared, saved.label)) {
                vm.restoreReactions(saved.reactions)
                undo = null
            }
        }
    }
    if (adding >= 0) {
        val info = catalogue.rows.firstOrNull { it.row == adding }
        AddResponseSheet(
            rowLabel = info?.label.orEmpty(),
            catalogue = catalogue,
            tileFields = { tile -> tileFields(tile.ordinal, adding, sceneId, seed) },
            onPreview = vm::previewResponse,
            onAdd = { tile ->
                vm.editResponderCatalog(ResponderNative.RESP_CATALOG_ADD, adding, 0, tile.ordinal.toFloat())
                adding = -1
            },
            onDismiss = { adding = -1 },
        )
    }
}

/** What a row Reset put away: the row, its name and the saved text it had, which Undo puts back. */
private data class RowUndo(
    val row: Int,
    val label: String,
    val reactions: String?,
)

/** One response a row lists: the user's (its place among the user's) or a style's own (its route). */
private class Listed(
    val userIndex: Int,
    val source: Int,
    val target: Int,
    val fields: FloatArray,
    val title: String,
) {
    val style: Boolean get() = userIndex < 0
    val enabled: Boolean get() = fields[ResponderNative.RESP_ENABLED] > 0.5f
}

private fun listedOf(
    reactions: String?,
    sceneId: String,
    seed: Int,
    handMask: Int,
    row: Int,
): List<Listed> {
    val all = ResponderNative.responderValues(reactions, sceneId, seed, handMask, row) ?: return emptyList()
    var user = 0
    return List(all.size / ResponderNative.RESP_LIST_VALUES) { i ->
        val at = i * ResponderNative.RESP_LIST_VALUES
        val fields = all.copyOfRange(at, at + ResponderNative.RESP_FIELDS)
        val styled = all[at + ResponderNative.RESP_FIELDS].toInt() == ResponderNative.RESP_ORIGIN_STYLE
        // After the origin and the route's index: the style route's source and target ordinals, which mute it.
        val source = all[at + ResponderNative.RESP_FIELDS + 2].toInt()
        val target = all[at + ResponderNative.RESP_FIELDS + 3].toInt()
        Listed(if (styled) -1 else user++, source, target, fields, ResponderNative.responderTitle(fields))
    }
}

/** A tile's response as a new style would hold it on [row]: native adds it to an empty text and lists it. */
private fun tileFields(
    tile: Int,
    row: Int,
    sceneId: String,
    seed: Int,
): FloatArray {
    val text = ResponderNative.responderCatalogEdit(null, ResponderNative.RESP_CATALOG_ADD, row, 0, tile.toFloat())
    return (ResponderNative.responderValues(text, sceneId, seed, 0, row) ?: FloatArray(0)).copyOf(ResponderNative.RESP_FIELDS)
}

@Composable
private fun EventRow(
    info: EventRowInfo,
    on: Boolean,
    light: () -> Float,
    chips: List<Listed>,
    open: String,
    look: FloatArray,
    catalogue: ResponseCatalogue,
    vm: VisualsViewModel,
    onOpen: (String) -> Unit,
    onAdd: () -> Unit,
    onReset: () -> Unit,
) {
    val users = chips.count { !it.style }
    val rowSwitch = stringResource(R.string.song_events_row_switch, info.label)
    val lightColor = MaterialTheme.colorScheme.primary
    Column(Modifier.fillMaxWidth().padding(vertical = GeodeSpacing.XS)) {
        Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier
                    .size(10.dp)
                    .clip(CircleShape)
                    .drawBehind { drawRect(lightColor.copy(alpha = LIGHT_FLOOR + (1f - LIGHT_FLOOR) * light().coerceIn(0f, 1f))) },
            )
            Column(Modifier.weight(1f).padding(start = GeodeSpacing.M)) {
                Text(info.label, style = MaterialTheme.typography.bodyLarge)
                val note =
                    if (info.needsBeat) {
                        R.string.song_events_needs_beat
                    } else if (users == 0) {
                        R.string.song_events_nothing_yet
                    } else {
                        0
                    }
                if (note != 0) {
                    Text(
                        stringResource(note),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
            Switch(
                checked = on,
                onCheckedChange = { vm.editResponder(ResponderNative.RESP_EDIT_ROW_ON, info.row, value = if (it) 1f else 0f) },
                modifier = Modifier.semantics { contentDescription = rowSwitch },
            )
        }
        FlowRow(
            Modifier.fillMaxWidth().padding(start = GeodeSpacing.L),
            horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
            verticalArrangement = Arrangement.spacedBy(GeodeSpacing.XS),
        ) {
            chips.forEach { chip ->
                if (chip.style) {
                    StyleChip(chip, vm.styleChipSwitch)
                } else {
                    val key = "${info.row}:${chip.userIndex}"
                    FilterChip(
                        selected = open == key,
                        onClick = { onOpen(key) },
                        label = { Text(chip.title) },
                        modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget).alpha(if (chip.enabled && on) 1f else OFF_ALPHA),
                    )
                }
            }
            OutlinedButton(onClick = onAdd, modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget)) {
                Text(stringResource(R.string.song_events_add))
            }
            if (users > 0 || on) {
                TextButton(onClick = {
                    onReset()
                    vm.editResponderCatalog(ResponderNative.RESP_CATALOG_RESET_ROW, info.row)
                }) { Text(stringResource(R.string.song_events_reset_row)) }
            }
        }
        chips.firstOrNull { !it.style && open == "${info.row}:${it.userIndex}" }?.let { chip ->
            ResponseCard(info.row, chip, look, catalogue, vm) { onOpen("${info.row}:${chip.userIndex}") }
        }
    }
}

/** A style's own reaction: dimmed, with its own switch and nothing else to edit here. */
@Composable
private fun StyleChip(
    chip: Listed,
    switch: StyleChipSwitch,
) {
    val name = stringResource(R.string.song_events_style_switch, chip.title)
    Surface(
        shape = CircleShape,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = STYLE_CHIP_ALPHA),
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant),
        modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget).alpha(if (chip.enabled) STYLE_ALPHA else OFF_ALPHA),
    ) {
        Row(Modifier.padding(start = GeodeSpacing.M, end = GeodeSpacing.XS), verticalAlignment = Alignment.CenterVertically) {
            Column {
                Text(chip.title, style = MaterialTheme.typography.labelLarge)
                Text(stringResource(R.string.song_events_style_tag), style = MaterialTheme.typography.labelSmall)
            }
            Switch(
                checked = chip.enabled,
                onCheckedChange = { switch.mute(chip.source, chip.target, !it) },
                modifier = Modifier.scale(SWITCH_SCALE).semantics { contentDescription = name },
            )
        }
    }
}

/** One response's controls: its own, then strength, envelope, when it plays and how often. */
@Composable
private fun ResponseCard(
    row: Int,
    chip: Listed,
    look: FloatArray,
    catalogue: ResponseCatalogue,
    vm: VisualsViewModel,
    onClose: () -> Unit,
) {
    val index = chip.userIndex
    val fields = chip.fields
    val set: (Int, Float) -> Unit = { field, value -> vm.editResponder(ResponderNative.RESP_EDIT_SET, row, index, field, value) }
    val kind = fields[ResponderNative.RESP_KIND].roundToInt()
    val preset = ResponderNative.responderPresetOf(fields)
    Surface(
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = CARD_ALPHA),
        modifier = Modifier.fillMaxWidth().padding(top = GeodeSpacing.S),
    ) {
        Column(Modifier.padding(GeodeSpacing.M)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(chip.title, style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f))
                Switch(
                    checked = chip.enabled,
                    onCheckedChange = { set(ResponderNative.RESP_ENABLED, if (it) 1f else 0f) },
                    modifier = Modifier.semantics { contentDescription = chip.title },
                )
            }
            catalogue.controlsOf(kind).forEach { ResponseControlRow(it, fields, look, set) }
            CardHeading(R.string.song_events_strength)
            Choices(catalogue.curves.map { it.name }, fields[ResponderNative.RESP_STRENGTH].roundToInt()) {
                set(ResponderNative.RESP_STRENGTH, it.toFloat())
            }
            ControlHint(
                catalogue.curves
                    .getOrNull(fields[ResponderNative.RESP_STRENGTH].roundToInt())
                    ?.sentence
                    .orEmpty(),
            )
            CardHeading(R.string.song_events_envelope)
            Choices(catalogue.presets, preset) { vm.editResponderCatalog(ResponderNative.RESP_CATALOG_PRESET, row, index, it.toFloat()) }
            catalogue.common.filter { it.field in ENVELOPE_FIELDS }.forEach { ResponseControlRow(it, fields, look, set) }
            CardHeading(R.string.song_events_when)
            Choices(catalogue.drives.map { it.name }, fields[ResponderNative.RESP_DRIVE].roundToInt()) {
                set(ResponderNative.RESP_DRIVE, it.toFloat())
            }
            ControlHint(
                catalogue.drives
                    .getOrNull(fields[ResponderNative.RESP_DRIVE].roundToInt())
                    ?.sentence
                    .orEmpty(),
            )
            CardHeading(R.string.song_events_often)
            catalogue.common.filter { it.field !in ENVELOPE_FIELDS }.forEach { ResponseControlRow(it, fields, look, set) }
            Row(Modifier.fillMaxWidth().padding(top = GeodeSpacing.S), horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S)) {
                FilledTonalButton(onClick = { vm.previewResponse(fields) }) { Text(stringResource(R.string.song_events_play_once)) }
                TextButton(onClick = {
                    vm.editResponder(ResponderNative.RESP_EDIT_REMOVE, row, index)
                    onClose()
                }) { Text(stringResource(R.string.song_events_remove)) }
            }
        }
    }
}

@Composable
private fun CardHeading(text: Int) {
    Text(
        stringResource(text),
        modifier = Modifier.padding(top = GeodeSpacing.M).semantics { heading() },
        style = MaterialTheme.typography.labelLarge,
        color = MaterialTheme.colorScheme.primary,
    )
}

/** One control native describes: a pick from its choices, or a slider with the words native gives its value. */
@Composable
private fun ResponseControlRow(
    control: ResponseControl,
    fields: FloatArray,
    look: FloatArray,
    set: (Int, Float) -> Unit,
) {
    val value = fields[control.field]
    if (control.unit == ResponseCatalogue.UNIT_CHOICE) {
        Text(control.label, modifier = Modifier.padding(top = GeodeSpacing.S), style = MaterialTheme.typography.bodyMedium)
        Choices(control.choices, value.roundToInt()) { set(control.field, it.toFloat()) }
        return
    }
    val shown = ResponderNative.responderFieldText(fields, control.field, look)
    val steps = if (control.step > 0f) ((control.range.endInclusive - control.range.start) / control.step).roundToInt() - 1 else 0
    Column(Modifier.padding(top = GeodeSpacing.XS)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(control.label, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
            Text(shown, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Slider(
            value = value.coerceIn(control.range),
            onValueChange = { set(control.field, it) },
            valueRange = control.range,
            steps = steps.coerceAtLeast(0),
            modifier =
                Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget).semantics {
                    contentDescription = control.label
                    stateDescription = shown
                },
        )
    }
}

@Composable
private fun Choices(
    names: List<String>,
    selected: Int,
    onPick: (Int) -> Unit,
) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S)) {
        names.forEachIndexed { i, name ->
            FilterChip(
                selected = i == selected,
                onClick = { onPick(i) },
                label = { Text(name) },
                modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget),
            )
        }
    }
}

/** The "+ Add response" sheet: the families as tabs, then a tile for each response; a tile plays once or is added. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun AddResponseSheet(
    rowLabel: String,
    catalogue: ResponseCatalogue,
    tileFields: (ResponseTile) -> FloatArray,
    onPreview: (FloatArray) -> Unit,
    onAdd: (ResponseTile) -> Unit,
    onDismiss: () -> Unit,
) {
    var family by rememberSaveable { mutableIntStateOf(0) }
    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(Modifier.padding(horizontal = GeodeSpacing.L)) {
            Text(stringResource(R.string.song_events_add_title, rowLabel), style = MaterialTheme.typography.titleMedium)
            Text(
                stringResource(R.string.song_events_add_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            FlowRow(Modifier.padding(vertical = GeodeSpacing.S), horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S)) {
                catalogue.families.forEachIndexed { i, name ->
                    FilterChip(
                        selected = i == family,
                        onClick = { family = i },
                        label = { Text(name) },
                        modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget),
                    )
                }
            }
            catalogue.tiles.filter { it.family == family }.forEach { tile ->
                Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(tile.name, style = MaterialTheme.typography.bodyLarge)
                        Text(tile.line, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    TextButton(onClick = { onPreview(tileFields(tile)) }) { Text(stringResource(R.string.song_events_preview)) }
                    FilledTonalButton(onClick = { onAdd(tile) }) { Text(stringResource(R.string.song_events_add_button)) }
                }
            }
            Spacer(Modifier.size(GeodeSpacing.XL))
        }
    }
}

/** This frame's work against the budget as a quiet meter: a line, and a word once it is busy. */
@Composable
private fun CostMeter(status: () -> FloatArray) {
    val level by remember {
        derivedStateOf {
            val s = status()
            if (s.size < ResponderNative.RESP_STATUS_VALUES) NO_LEVEL else s[ResponderNative.RESP_STATUS_LEVEL].roundToInt()
        }
    }
    if (level == NO_LEVEL) return
    val word =
        stringResource(
            when (level) {
                0 -> R.string.song_events_cost_light
                1 -> R.string.song_events_cost_heavy
                else -> R.string.song_events_cost_over
            },
        )
    val name = stringResource(R.string.song_events_cost)
    Row(
        Modifier.fillMaxWidth().padding(vertical = GeodeSpacing.S).semantics(mergeDescendants = true) {
            contentDescription = name
            stateDescription = word
        },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(name, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        LinearProgressIndicator(
            progress = {
                val s = status()
                val budget = s.getOrElse(ResponderNative.RESP_STATUS_BUDGET) { 0f }
                if (budget > 0f) (s.getOrElse(ResponderNative.RESP_STATUS_COST) { 0f } / budget).coerceIn(0f, 1f) else 0f
            },
            modifier = Modifier.weight(1f).padding(horizontal = GeodeSpacing.M),
            color = if (level == 0) MaterialTheme.colorScheme.outline else MaterialTheme.colorScheme.primary,
        )
        Text(word, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun UndoBar(
    text: String,
    onUndo: () -> Unit,
) {
    Surface(
        shape = RoundedCornerShape(12.dp),
        color = MaterialTheme.colorScheme.inverseSurface,
        modifier = Modifier.fillMaxWidth().padding(top = GeodeSpacing.S),
    ) {
        Row(Modifier.padding(start = GeodeSpacing.L), verticalAlignment = Alignment.CenterVertically) {
            Text(text, modifier = Modifier.weight(1f), color = MaterialTheme.colorScheme.inverseOnSurface)
            TextButton(
                onClick = onUndo,
            ) { Text(stringResource(R.string.song_events_undo), color = MaterialTheme.colorScheme.inversePrimary) }
        }
    }
}

private const val ROW_ON = 0
private const val NO_LEVEL = -1
private const val STATUS_POLL_MS = 80L
private const val UNDO_MS = 6000L
private const val LIGHT_FLOOR = 0.15f
private const val OFF_ALPHA = 0.5f
private const val STYLE_ALPHA = 0.7f
private const val STYLE_CHIP_ALPHA = 0.4f
private const val CARD_ALPHA = 0.5f
private const val SWITCH_SCALE = 0.75f

// The envelope's three: the common controls that are fade in, hold and fade out.
private val ENVELOPE_FIELDS = setOf(ResponderNative.RESP_FADE_IN, ResponderNative.RESP_HOLD, ResponderNative.RESP_FADE_OUT)
