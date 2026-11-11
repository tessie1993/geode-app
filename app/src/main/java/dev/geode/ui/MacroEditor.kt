package dev.geode.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
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
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
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
import dev.geode.data.MacroLink
import dev.geode.data.MacroStore
import dev.geode.render.LfoTarget
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import kotlin.math.roundToInt

/**
 * Customize › Quick knobs › Edit knobs: the macro bank's editor. A list of the knobs; a tap opens one
 * on its name, a Turn slider (so the edit shows on the visual behind the sheet while it is made) and
 * the params it moves, each with how far a full turn up and a full turn down push it, as a share of
 * the param's range. A knob's ⋮ moves, copies, resets or deletes it.
 *
 * Everything offered is native's answer ([MacroStore.editor], core/viz/MacroBank's editorText): the
 * caps, which targets exist and reach [sceneId], whether a knob can be reset or removed. Every edit
 * is a native call through [store] that is live and saved at once; there is no Save.
 */
@Composable
internal fun MacroEditor(
    bank: MacroBankState,
    store: MacroStore,
    sceneId: String,
) {
    val editor = remember(bank.text, sceneId) { store.editor(sceneId) }
    var openId by rememberSaveable { mutableIntStateOf(NO_KNOB) }
    var asking by remember { mutableStateOf<Ask?>(null) }
    Column(Modifier.fillMaxWidth()) {
        editor.knobs.forEachIndexed { index, knob ->
            val open = knob.id == openId
            KnobRow(
                knob = knob,
                open = open,
                actions =
                    knobActions(
                        knob = knob,
                        place = Place(first = index == 0, last = index == editor.knobs.lastIndex, room = editor.canAdd),
                        store = store,
                        onOpen = { openId = it },
                        onAsk = { asking = it },
                    ),
                onToggle = { openId = if (open) NO_KNOB else knob.id },
            )
            if (open) KnobPage(knob, editor, store)
        }
        TextButton(
            onClick = { store.addKnob()?.let { openId = it } },
            enabled = editor.canAdd,
            modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget),
        ) {
            StoneIconArt(StoneIcon.ADD, null)
            Text(stringResource(R.string.macro_new_knob), Modifier.padding(start = GeodeSpacing.S))
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

/** The open knob: its name, a Turn to try it with, and its links. */
@Composable
private fun KnobPage(
    knob: MacroEditorKnob,
    editor: MacroEditorState,
    store: MacroStore,
) {
    var picking by remember(knob.id) { mutableStateOf(false) }
    Column(Modifier.fillMaxWidth().padding(start = GeodeSpacing.M, bottom = GeodeSpacing.M)) {
        KnobName(knob, editor.maxNameBytes) { store.rename(knob.id, it) }
        AmountSlider(
            label = stringResource(R.string.macro_turn),
            spoken = stringResource(R.string.macro_turn_a11y, knob.name),
            value = knob.value,
            onChange = { store.setValue(knob.id, it, persist = false) },
            onDone = store::commit,
        )
        Text(
            stringResource(R.string.macro_moves),
            modifier = Modifier.padding(top = GeodeSpacing.S).semantics { heading() },
            style = MaterialTheme.typography.labelLarge,
            color = accentTextColor(),
        )
        knob.links.forEach { link -> LinkRows(knob.id, link, store) }
        TextButton(
            onClick = { picking = true },
            enabled = knob.canLink,
            modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget),
        ) {
            StoneIconArt(StoneIcon.ADD, null)
            Text(stringResource(R.string.macro_add_link), Modifier.padding(start = GeodeSpacing.S))
        }
        if (!knob.canLink && editor.maxLinks > 0) Note(stringResource(R.string.macro_links_full, editor.maxLinks))
    }
    if (picking) {
        TargetPicker(
            knob = knob,
            editor = editor,
            onPick = { target ->
                picking = false
                store.addLink(knob.id, target)
            },
            onDismiss = { picking = false },
        )
    }
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
    store: MacroStore,
) {
    val name = targetLabel(link.target)
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
            onChange = { store.setLink(knobId, link.target, it, link.down, persist = false) },
            onDone = store::commit,
        )
        AmountSlider(
            label = down,
            spoken = stringResource(R.string.macro_link_a11y, name, down),
            value = link.down,
            onChange = { store.setLink(knobId, link.target, link.up, it, persist = false) },
            onDone = store::commit,
        )
    }
}

/** A -100 % to +100 % slider: a knob's turn, or a link's push as a share of its param's range. */
@Composable
private fun AmountSlider(
    label: String,
    spoken: String,
    value: Float,
    onChange: (Float) -> Unit,
    onDone: () -> Unit,
) {
    val shown = "%+d%%".format((value * PERCENT).roundToInt())
    Row(Modifier.fillMaxWidth().heightIn(min = GeodeSpacing.TouchTarget), verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.width(LABEL_WIDTH), style = MaterialTheme.typography.bodySmall, maxLines = 2)
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
            shown,
            Modifier.width(VALUE_WIDTH).padding(start = GeodeSpacing.S),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

/** The targets the knob does not move yet, the ones this style shows first. */
@Composable
private fun TargetPicker(
    knob: MacroEditorKnob,
    editor: MacroEditorState,
    onPick: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    val linked = remember(knob.links) { knob.links.mapTo(mutableSetOf()) { it.target } }
    val free = remember(editor.targets, linked) { editor.targets.filter { it.name !in linked } }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.macro_pick_title, knob.name), maxLines = 2, overflow = TextOverflow.Ellipsis) },
        text = {
            Column(Modifier.heightIn(max = PICKER_MAX_HEIGHT).verticalScroll(rememberScrollState())) {
                if (free.isEmpty()) Text(stringResource(R.string.macro_pick_none))
                TargetGroup(stringResource(R.string.macro_pick_here), free.filter { it.reaches }.map { it.name }, onPick)
                TargetGroup(stringResource(R.string.macro_pick_elsewhere), free.filterNot { it.reaches }.map { it.name }, onPick)
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.macro_pick_close)) } },
    )
}

@Composable
private fun TargetGroup(
    title: String,
    targets: List<String>,
    onPick: (String) -> Unit,
) {
    if (targets.isEmpty()) return
    Text(
        title,
        modifier = Modifier.padding(top = GeodeSpacing.S).semantics { heading() },
        style = MaterialTheme.typography.labelLarge,
        color = accentTextColor(),
    )
    targets.forEach { target ->
        Text(
            targetLabel(target),
            modifier =
                Modifier
                    .fillMaxWidth()
                    .heightIn(min = GeodeSpacing.TouchTarget)
                    .clickable(role = Role.Button) { onPick(target) }
                    .padding(vertical = GeodeSpacing.M),
            style = MaterialTheme.typography.bodyLarge,
        )
    }
}

@Composable
private fun Note(text: String) {
    Text(text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

/** A target's label, the modulation pickers' own; its wire name if this shell does not know it. */
private fun targetLabel(wire: String): String = LfoTarget.entries.firstOrNull { it.name == wire }?.label?.ifEmpty { wire } ?: wire

private const val NO_KNOB = -1
private const val PERCENT = 100f
private const val UNREACHED_ALPHA = 0.6f
private val LABEL_WIDTH = 88.dp
private val VALUE_WIDTH = 56.dp
private val PICKER_MAX_HEIGHT = 420.dp
