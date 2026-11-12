package dev.geode.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.data.MacroBankState
import dev.geode.data.MacroEditorKnob
import dev.geode.data.MacroEditorState
import dev.geode.data.MacroLibraryKnob
import dev.geode.data.MacroLink
import dev.geode.data.MacroStore
import dev.geode.data.MacroTarget
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * Customize › Quick knobs › Edit knobs: the macro bank's editor. A list of the knobs; a tap opens one
 * on its name, a Turn slider (so the edit shows on the visual behind the sheet while it is made) and
 * the params it moves, each with how far a full turn up and a full turn down push it, as a share of
 * the param's range (and in its own units), and the curve it follows. A knob's ⋮ moves, copies, resets or
 * deletes it, or takes it off screen into the library. New macros are made above, under the knobs ([AddMacroButton]).
 *
 * Everything offered is native's answer ([MacroStore.editor], core/viz/MacroBank's editorText): the
 * caps, which targets exist and reach [sceneId], whether a knob can be reset or removed, the curves.
 * Every edit to a saved knob is a native call through [store] that is live and saved at once.
 */
@Composable
internal fun MacroEditor(
    bank: MacroBankState,
    store: MacroStore,
    sceneId: String,
) {
    val editor = remember(bank, sceneId) { store.editor(sceneId) }
    val saved = remember(editor, bank.draft) { editor.knobs.filter { it.id != bank.draft?.id } }
    var openId by rememberSaveable { mutableIntStateOf(NO_KNOB) }
    var asking by remember { mutableStateOf<Ask?>(null) }
    Column(Modifier.fillMaxWidth()) {
        saved.forEachIndexed { index, knob ->
            val open = knob.id == openId
            KnobRow(
                knob = knob,
                open = open,
                actions =
                    knobActions(
                        knob = knob,
                        place = Place(first = index == 0, last = index == saved.lastIndex, room = editor.canAdd),
                        store = store,
                        onOpen = { openId = it },
                        onAsk = { asking = it },
                    ),
                onToggle = { openId = if (open) NO_KNOB else knob.id },
            )
            if (open) KnobPage(knob, editor, store, sceneId)
        }
        if (!editor.canAdd && editor.maxKnobs > 0) Note(stringResource(R.string.macro_bank_full, editor.maxKnobs))
        val resetAll = Ask.resetAll { store.resetAll() }
        TextButton(
            onClick = { asking = resetAll },
            enabled = !editor.isDefault,
            modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget),
        ) { Text(stringResource(R.string.macro_reset_all)) }
    }
    asking?.let { ask ->
        ConfirmDialog(
            title = ask.title,
            text = ask.text,
            confirmLabel = ask.confirm,
            onDismiss = { asking = null },
            onConfirm = {
                asking = null
                ask.action()
            },
        )
    }
}

/** A question before an edit that cannot be undone. */
private class Ask(
    val title: String,
    val text: String,
    val confirm: String,
    val action: () -> Unit,
) {
    companion object {
        @Composable
        fun resetAll(action: () -> Unit) =
            Ask(
                stringResource(R.string.macro_reset_all_title),
                stringResource(R.string.macro_reset_all_text),
                stringResource(R.string.macro_reset_confirm),
                action,
            )

        @Composable
        fun delete(
            name: String,
            action: () -> Unit,
        ) = Ask(
            stringResource(R.string.macro_delete_title, name),
            stringResource(R.string.macro_delete_text),
            stringResource(R.string.macro_delete),
            action,
        )

        @Composable
        fun resetKnob(
            name: String,
            action: () -> Unit,
        ) = Ask(
            stringResource(R.string.macro_reset_knob_title, name),
            stringResource(R.string.macro_reset_knob_text),
            stringResource(R.string.macro_reset_confirm),
            action,
        )
    }
}

/** Where a knob sits, for what its menu can offer. */
private class Place(
    val first: Boolean,
    val last: Boolean,
    val room: Boolean,
)

/** One thing a knob's ⋮ does; TalkBack gets the same list as custom actions on the knob's row. */
private class KnobAction(
    val label: String,
    val enabled: Boolean,
    val run: () -> Unit,
)

@Composable
private fun knobActions(
    knob: MacroEditorKnob,
    place: Place,
    store: MacroStore,
    onOpen: (Int) -> Unit,
    onAsk: (Ask) -> Unit,
): List<KnobAction> {
    val delete = Ask.delete(knob.name) { store.remove(knob.id) }
    val reset = Ask.resetKnob(knob.name) { store.resetKnob(knob.id) }
    return listOf(
        KnobAction(stringResource(R.string.macro_move_earlier), !place.first) { store.move(knob.id, -1) },
        KnobAction(stringResource(R.string.macro_move_later), !place.last) { store.move(knob.id, 1) },
        KnobAction(stringResource(R.string.macro_duplicate), place.room) { store.duplicate(knob.id)?.let(onOpen) },
        KnobAction(stringResource(R.string.macro_remove_from_screen), true) { store.hide(knob.id) },
        if (knob.builtIn) {
            KnobAction(stringResource(R.string.macro_reset_knob), !knob.asBuilt) { onAsk(reset) }
        } else {
            KnobAction(stringResource(R.string.macro_delete), true) { onAsk(delete) }
        },
    )
}

@Composable
private fun KnobRow(
    knob: MacroEditorKnob,
    open: Boolean,
    actions: List<KnobAction>,
    onToggle: () -> Unit,
) {
    val toggleLabel = stringResource(if (open) R.string.macro_knob_close else R.string.macro_knob_open, knob.name)
    val summary =
        if (knob.links.isEmpty()) {
            stringResource(R.string.macro_knob_moves_none)
        } else {
            pluralStringResource(R.plurals.macro_knob_moves, knob.links.size, knob.links.size)
        }
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = GeodeSpacing.TouchTarget)
            .clickable(onClickLabel = toggleLabel, role = Role.Button, onClick = onToggle)
            .semantics {
                customActions = actions.filter { it.enabled }.map { a -> CustomAccessibilityAction(a.label) { true.also { a.run() } } }
            },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f).padding(vertical = GeodeSpacing.XS)) {
            Text(
                knob.name,
                style = MaterialTheme.typography.titleSmall,
                color = if (open) accentTextColor() else MaterialTheme.colorScheme.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(summary, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        KnobActionsMenu(knob.name, actions)
    }
}

@Composable
private fun KnobActionsMenu(
    name: String,
    actions: List<KnobAction>,
) {
    var open by remember { mutableStateOf(false) }
    Box {
        IconButton(onClick = { open = true }) {
            Icon(
                Icons.Filled.MoreVert,
                contentDescription = stringResource(R.string.customize_a11y_more_for, name),
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            actions.forEach { action ->
                DropdownMenuItem(text = { Text(action.label) }, enabled = action.enabled, onClick = {
                    open = false
                    action.run()
                })
            }
        }
    }
}

/**
 * The open knob: its name (a draft gets its name when saved instead), a Turn to try it with, its links with New
 * ranges beside them (native re-rolls the range and curve of each link it has), and the two ways to fill it: Add a
 * parameter, or Add random (a count, then native links that many more params it picks at random).
 */
@Composable
private fun KnobPage(
    knob: MacroEditorKnob,
    editor: MacroEditorState,
    store: MacroStore,
    sceneId: String,
    draft: Boolean = false,
) {
    var picking by remember(knob.id) { mutableStateOf(false) }
    val controls = remember(editor.targets) { editor.targets.associate { it.name to it.control } }
    Column(Modifier.fillMaxWidth().padding(start = GeodeSpacing.M, bottom = GeodeSpacing.M)) {
        if (!draft) KnobName(knob, editor.maxNameBytes) { store.rename(knob.id, it) }
        AmountSlider(
            label = stringResource(R.string.macro_turn),
            spoken = stringResource(R.string.macro_turn_a11y, knob.name),
            value = knob.value,
            onChange = { store.setValue(knob.id, it, persist = false) },
            onDone = store::commit,
        )
        MovesHeading(knob) { store.rollRanges(knob.id) }
        if (knob.links.isEmpty()) Note(stringResource(R.string.macro_empty_hint))
        knob.links.forEach { link -> LinkRows(knob.id, link, targetLabel(link.target, controls[link.target]), editor.curves, store) }
        TextButton(
            onClick = { picking = true },
            enabled = knob.canLink,
            modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget),
        ) {
            StoneIconArt(StoneIcon.ADD, null)
            Text(stringResource(R.string.macro_add_link), Modifier.padding(start = GeodeSpacing.S))
        }
        AddRandomRow(knob) { count -> store.addRandom(knob.id, sceneId, count) }
        if (!knob.canLink && editor.maxLinks > 0) Note(stringResource(R.string.macro_links_full, editor.maxLinks))
    }
    if (picking) {
        TargetPicker(
            knob = knob,
            editor = editor,
            categories = remember(editor) { store.controls().values.associate { it.target to it.category } },
            controls = controls,
            onPick = { target ->
                picking = false
                store.addLink(knob.id, target)
            },
            onDismiss = { picking = false },
        )
    }
}

/** "What it moves", with New ranges beside it: a second, smaller Random that keeps the params and re-rolls their ranges. */
@Composable
private fun MovesHeading(
    knob: MacroEditorKnob,
    onRollRanges: () -> Unit,
) {
    Row(Modifier.fillMaxWidth().padding(top = GeodeSpacing.S), verticalAlignment = Alignment.CenterVertically) {
        Text(
            stringResource(R.string.macro_moves),
            modifier = Modifier.weight(1f).semantics { heading() },
            style = MaterialTheme.typography.labelLarge,
            color = accentTextColor(),
        )
        val spoken = stringResource(R.string.macro_random_ranges_a11y, knob.name)
        TextButton(
            onClick = onRollRanges,
            enabled = knob.links.isNotEmpty(),
            modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget).semantics { contentDescription = spoken },
        ) {
            StoneIconArt(StoneIcon.SHUFFLE, null)
            Text(stringResource(R.string.macro_random_ranges), Modifier.padding(start = GeodeSpacing.S))
        }
    }
}

/**
 * Add random: a minus / plus stepper for how many params, within the most native says fit ([MacroEditorKnob.addMax]),
 * then a button that has native link that many more, picked at random.
 */
@Composable
private fun AddRandomRow(
    knob: MacroEditorKnob,
    onAdd: (Int) -> Unit,
) {
    val max = knob.addMax
    var count by rememberSaveable(knob.id) { mutableIntStateOf(knob.addCount) }
    val shown = count.coerceIn(minOf(1, max), max)
    val spoken = stringResource(R.string.macro_add_random_count_a11y)
    val fewer = stringResource(R.string.macro_add_random_fewer)
    val more = stringResource(R.string.macro_add_random_more)
    Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.macro_add_random), Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
        IconButton(onClick = { count = shown - 1 }, enabled = shown > 1, modifier = Modifier.semantics { contentDescription = fewer }) {
            Text(STEP_DOWN, style = MaterialTheme.typography.titleMedium)
        }
        Text(
            shown.toString(),
            Modifier.width(COUNT_WIDTH).semantics {
                contentDescription = spoken
                stateDescription = shown.toString()
            },
            style = MaterialTheme.typography.titleMedium,
        )
        IconButton(onClick = { count = shown + 1 }, enabled = shown < max, modifier = Modifier.semantics { contentDescription = more }) {
            Text(STEP_UP, style = MaterialTheme.typography.titleMedium)
        }
        TextButton(onClick = { onAdd(shown) }, enabled = shown > 0, modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget)) {
            StoneIconArt(StoneIcon.SHUFFLE, null)
            Text(pluralStringResource(R.plurals.macro_add_random_go, shown, shown), Modifier.padding(start = GeodeSpacing.S))
        }
    }
    if (max == 0 && knob.canLink) Note(stringResource(R.string.macro_add_random_none))
}

/**
 * The name field keeps what is typed and sends each change to native, which trims and caps it and
 * refuses an empty one (the knob keeps its last name while the field is empty). The field follows the
 * knob's name only when that changes to something other than what is typed: a reset, a cleaned name.
 */
@Composable
private fun KnobName(
    knob: MacroEditorKnob,
    maxBytes: Int,
    onRename: (String) -> Unit,
) {
    var draft by remember(knob.id) { mutableStateOf(knob.name) }
    LaunchedEffect(knob.name) { if (draft.trim() != knob.name) draft = knob.name }
    OutlinedTextField(
        value = draft,
        onValueChange = { typed ->
            if (typed.toByteArray().size <= maxBytes) {
                draft = typed
                onRename(typed)
            }
        },
        modifier = Modifier.fillMaxWidth(),
        label = { Text(stringResource(R.string.macro_name)) },
        singleLine = true,
        isError = draft.isBlank(),
        supportingText = { if (draft.isBlank()) Text(stringResource(R.string.macro_name_empty)) },
    )
}

/** One link: the param's name with its remove button, then how far each full turn pushes it. */
@Composable
private fun LinkRows(
    knobId: Int,
    link: MacroLink,
    name: String,
    curves: List<String>,
    store: MacroStore,
) {
    Column(Modifier.fillMaxWidth().alpha(if (link.reaches) 1f else UNREACHED_ALPHA)) {
        Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(name, style = MaterialTheme.typography.bodyMedium)
                if (!link.reaches) Note(stringResource(R.string.macro_link_unreached))
            }
            IconButton(onClick = { store.removeLink(knobId, link.target) }) {
                StoneIconArt(StoneIcon.CLOSE, stringResource(R.string.macro_link_remove, name))
            }
        }
        val up = stringResource(R.string.macro_link_up)
        val down = stringResource(R.string.macro_link_down)
        AmountSlider(
            label = up,
            spoken = stringResource(R.string.macro_link_a11y, name, up),
            value = link.up,
            units = link.upBy,
            onChange = { store.setLink(knobId, link.target, it, link.down, persist = false) },
            onDone = store::commit,
        )
        AmountSlider(
            label = down,
            spoken = stringResource(R.string.macro_link_a11y, name, down),
            value = link.down,
            units = link.downBy,
            onChange = { store.setLink(knobId, link.target, link.up, it, persist = false) },
            onDone = store::commit,
        )
        CurveChips(name, link.curve, curves) { store.setCurve(knobId, link.target, it) }
    }
}

/** How the link follows the knob, one chip per curve native offers. */
@Composable
private fun CurveChips(
    name: String,
    current: String,
    curves: List<String>,
    onPick: (String) -> Unit,
) {
    val group = stringResource(R.string.macro_curve_a11y, name)
    FlowRow(
        Modifier.semantics { contentDescription = group },
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        curves.forEach { curve ->
            FilterChip(
                selected = curve == current,
                onClick = { onPick(curve) },
                label = { Text(curveLabel(curve)) },
            )
        }
    }
}

@Composable
private fun curveLabel(curve: String): String =
    when (curve) {
        "LINEAR" -> stringResource(R.string.macro_curve_linear)
        "EASE_IN" -> stringResource(R.string.macro_curve_ease_in)
        "EASE_OUT" -> stringResource(R.string.macro_curve_ease_out)
        "S_CURVE" -> stringResource(R.string.macro_curve_s)
        else -> curve
    }

/** An offset in the param's own units, as native worked it out: signed, no trailing zeros. */
private fun unitsText(v: Float): String =
    if (abs(v) < UNITS_EPSILON) {
        "0"
    } else {
        "%+.3f".format(v).trimEnd('0').trimEnd('.', ',')
    }

/**
 * A -100 % to +100 % slider: a knob's turn, or a link's push as a share of its param's range, with [units],
 * the same push in the param's own units, under its label.
 */
@Composable
private fun AmountSlider(
    label: String,
    spoken: String,
    value: Float,
    units: Float? = null,
    onChange: (Float) -> Unit,
    onDone: () -> Unit,
) {
    val percent = "%+d%%".format((value * PERCENT).roundToInt())
    val shown = units?.let { "$percent, ${unitsText(it)}" } ?: percent
    Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.width(LABEL_WIDTH)) {
            Text(label, style = MaterialTheme.typography.bodySmall, maxLines = 2)
            if (units != null) Note(unitsText(units))
        }
        Slider(
            value = value,
            onValueChange = onChange,
            modifier =
                Modifier.weight(1f).semantics {
                    contentDescription = spoken
                    stateDescription = shown
                },
            valueRange = -1f..1f,
            onValueChangeFinished = onDone,
        )
        Text(
            percent,
            Modifier.width(VALUE_WIDTH).padding(start = GeodeSpacing.S),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

/**
 * The targets the knob does not move yet, grouped by the Customize category whose slider sets them (native's
 * controls table), the rest under Other; the ones this style does not show are marked.
 */
@Composable
private fun TargetPicker(
    knob: MacroEditorKnob,
    editor: MacroEditorState,
    categories: Map<String, String>,
    controls: Map<String, String>,
    onPick: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    val linked = remember(knob.links) { knob.links.mapTo(mutableSetOf()) { it.target } }
    val free = remember(editor.targets, linked) { editor.targets.filter { it.name !in linked } }
    val groups: List<Pair<String?, List<MacroTarget>>> =
        remember(free, categories) {
            val byCategory = free.groupBy { CustomizeCategory.named(categories[it.name]) }
            CustomizeCategory.entries.mapNotNull { c -> byCategory[c]?.let { c.title to it } } +
                listOfNotNull(byCategory[null]?.let { null to it })
        }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.macro_pick_title, knob.name), maxLines = 2, overflow = TextOverflow.Ellipsis) },
        text = {
            Column(Modifier.heightIn(max = PICKER_MAX_HEIGHT).verticalScroll(rememberScrollState())) {
                if (free.isEmpty()) Text(stringResource(R.string.macro_pick_none))
                val other = stringResource(R.string.macro_pick_other)
                groups.forEach { (title, targets) -> TargetGroup(title ?: other, targets, controls, onPick) }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.macro_pick_close)) } },
    )
}

@Composable
private fun TargetGroup(
    title: String,
    targets: List<MacroTarget>,
    controls: Map<String, String>,
    onPick: (String) -> Unit,
) {
    if (targets.isEmpty()) return
    Text(
        title,
        modifier = Modifier.padding(top = GeodeSpacing.S).semantics { heading() },
        style = MaterialTheme.typography.labelLarge,
        color = accentTextColor(),
    )
    val elsewhere = stringResource(R.string.macro_link_unreached)
    targets.forEach { target ->
        Column(
            Modifier
                .fillMaxWidth()
                .heightIn(min = GeodeSpacing.TouchTarget)
                .clickable(role = Role.Button) { onPick(target.name) }
                .padding(vertical = GeodeSpacing.S)
                .alpha(if (target.reaches) 1f else UNREACHED_ALPHA),
        ) {
            Text(targetLabel(target.name, controls[target.name]), style = MaterialTheme.typography.bodyLarge)
            if (!target.reaches) Note(elsewhere)
        }
    }
}

@Composable
private fun Note(text: String) {
    Text(text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

/**
 * A target's label: the modulation pickers' own, else the Customize control that sets it ([control], its ParamKeys
 * key, which is its name on screen), else its wire name.
 */
private fun targetLabel(
    wire: String,
    control: String?,
): String =
    ModCatalog
        .words("target")
        .firstOrNull { it.name == wire }
        ?.label
        ?.ifEmpty { null } ?: control?.ifEmpty { null } ?: wire

private const val NO_KNOB = -1
private const val UNITS_EPSILON = 1e-6f
private const val PERCENT = 100f
private const val UNREACHED_ALPHA = 0.6f
private val LABEL_WIDTH = 88.dp
private val VALUE_WIDTH = 56.dp
private val PICKER_MAX_HEIGHT = 420.dp
private val COUNT_WIDTH = 32.dp
private const val STEP_DOWN = "\u2212"
private const val STEP_UP = "+"

/**
 * "Add macro" (+): a menu of New macro, an empty knob as a draft, then its editor ([MacroDraftDialog]) to fill it
 * by hand or with Random and save it under a name, and the knobs in the bank's library (the built-in ones taken
 * off screen and the user's), a tap putting one back. New macro is the knob editor's and follows [allowed]; putting
 * a knob back is free, so a knob taken off screen can always come back. Each place that offers it shows its own
 * dialog only for the draft it made, so the drawer and Customize never both open one.
 */
@Composable
internal fun AddMacroButton(
    bank: MacroBankState,
    editor: MacroEditorState,
    store: MacroStore,
    sceneId: String,
    allowed: Boolean = true,
    onLocked: () -> Unit = {},
) {
    var making by rememberSaveable { mutableStateOf(false) }
    var open by remember { mutableStateOf(false) }
    val library = if (editor.canShow) editor.library else emptyList()
    Box {
        TextButton(
            onClick = { open = true },
            enabled = editor.canAdd || library.isNotEmpty(),
            modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget),
        ) {
            StoneIconArt(StoneIcon.ADD, null)
            Text(stringResource(R.string.macro_add_macro), Modifier.padding(start = GeodeSpacing.S))
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            DropdownMenuItem(
                text = { Text(stringResource(R.string.macro_new_knob)) },
                leadingIcon = { StoneIconArt(StoneIcon.ADD, null) },
                enabled = editor.canAdd,
                onClick = {
                    open = false
                    if (!allowed) {
                        onLocked()
                    } else if (store.newDraft() != null) {
                        making = true
                    }
                },
            )
            if (library.isNotEmpty()) HorizontalDivider()
            library.forEach { knob ->
                LibraryItem(knob) {
                    open = false
                    store.show(knob.id)
                }
            }
        }
    }
    MacroDraftHost(bank, store, sceneId, showing = making, onClosed = { making = false })
}

@Composable
private fun LibraryItem(
    knob: MacroLibraryKnob,
    onPick: () -> Unit,
) {
    DropdownMenuItem(
        text = {
            Column {
                Text(knob.name, maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (knob.hint.isNotEmpty()) Note(knob.hint)
            }
        },
        onClick = onPick,
    )
}

/**
 * Shows the draft's dialog while [showing] and there is a draft; closes once the draft is saved or dropped.
 * A host that leaves the screen with its draft open (the drawer closed under it) drops the draft, unless the
 * activity is only being recreated.
 */
@Composable
internal fun MacroDraftHost(
    bank: MacroBankState,
    store: MacroStore,
    sceneId: String,
    showing: Boolean,
    onClosed: () -> Unit,
) {
    val draft = bank.draft
    LaunchedEffect(showing, draft == null) { if (showing && draft == null) onClosed() }
    val open by rememberUpdatedState(showing)
    val activity = LocalContext.current.findActivity()
    DisposableEffect(store) {
        onDispose { if (open && activity?.isChangingConfigurations != true) store.discardDraft() }
    }
    if (showing && draft != null) MacroDraftDialog(draft.id, bank, store, sceneId)
}

/**
 * A new macro before it is saved: its links (added one by one or rolled with Random), then Save as under a
 * name native suggests until the user types one. Back, a tap outside or Discard asks once, then drops it.
 */
@Composable
private fun MacroDraftDialog(
    id: Int,
    bank: MacroBankState,
    store: MacroStore,
    sceneId: String,
) {
    val editor = remember(bank, sceneId) { store.editor(sceneId) }
    val knob = editor.knobs.firstOrNull { it.id == id } ?: return
    val suggested = remember(bank) { store.autoName(id) }
    var typed by rememberSaveable(id) { mutableStateOf<String?>(null) }
    val name = typed ?: suggested
    var leaving by remember { mutableStateOf(false) }
    AlertDialog(
        onDismissRequest = { leaving = true },
        title = { Text(stringResource(R.string.macro_new_title)) },
        text = {
            Column(Modifier.heightIn(max = DIALOG_MAX_HEIGHT).verticalScroll(rememberScrollState())) {
                KnobPage(knob, editor, store, sceneId, draft = true)
                OutlinedTextField(
                    value = name,
                    onValueChange = { if (it.toByteArray().size <= editor.maxNameBytes) typed = it },
                    modifier = Modifier.fillMaxWidth(),
                    label = { Text(stringResource(R.string.macro_save_as)) },
                    singleLine = true,
                )
            }
        },
        confirmButton = {
            TextButton(onClick = { store.saveDraft(name) }, enabled = name.isNotBlank()) { Text(stringResource(R.string.macro_save)) }
        },
        dismissButton = { TextButton(onClick = { leaving = true }) { Text(stringResource(R.string.macro_discard)) } },
    )
    if (leaving) {
        ConfirmDialog(
            title = stringResource(R.string.macro_discard_title),
            text = stringResource(R.string.macro_discard_text),
            confirmLabel = stringResource(R.string.macro_discard),
            onDismiss = { leaving = false },
            onConfirm = {
                leaving = false
                store.discardDraft()
            },
        )
    }
}

/**
 * A saved knob's editor as a popup, opened by a long press on the knob (or its TalkBack action): rename,
 * Random, its links with their range and curve, and Delete (Reset for a built-in knob). Edits are live and
 * saved as they are made, like Edit knobs.
 */
@Composable
internal fun KnobEditorDialog(
    id: Int,
    bank: MacroBankState,
    store: MacroStore,
    sceneId: String,
    onDismiss: () -> Unit,
) {
    val editor = remember(bank, sceneId) { store.editor(sceneId) }
    val knob = editor.knobs.firstOrNull { it.id == id }
    LaunchedEffect(knob == null) { if (knob == null) onDismiss() }
    if (knob == null) return
    var asking by remember { mutableStateOf<Ask?>(null) }
    val delete = Ask.delete(knob.name) { store.remove(knob.id) }
    val reset = Ask.resetKnob(knob.name) { store.resetKnob(knob.id) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(knob.name, maxLines = 1, overflow = TextOverflow.Ellipsis) },
        text = {
            Column(Modifier.heightIn(max = DIALOG_MAX_HEIGHT).verticalScroll(rememberScrollState())) {
                KnobPage(knob, editor, store, sceneId)
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.macro_done)) } },
        dismissButton = {
            if (knob.builtIn) {
                TextButton(onClick = { asking = reset }, enabled = !knob.asBuilt) { Text(stringResource(R.string.macro_reset_knob)) }
            } else {
                TextButton(onClick = { asking = delete }) { Text(stringResource(R.string.macro_delete)) }
            }
        },
    )
    asking?.let { ask ->
        ConfirmDialog(
            title = ask.title,
            text = ask.text,
            confirmLabel = ask.confirm,
            onDismiss = { asking = null },
            onConfirm = {
                asking = null
                ask.action()
            },
        )
    }
}

private val DIALOG_MAX_HEIGHT = 520.dp
