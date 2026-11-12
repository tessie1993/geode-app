package dev.geode.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.MacroBankState
import dev.geode.data.MacroControl
import dev.geode.data.MacroStore
import dev.geode.geodeContainer
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt

/**
 * The knobs as Customize's sliders see them: which sliders a knob can move and which knobs move each now
 * (native's controls table, `geode_macro_bank_controls`), so a slider can be marked and long-pressed into
 * "Add to knob". The knob editor's lock gates the edit, as it does Edit knobs.
 */
@Immutable
internal class KnobLinks(
    val bank: MacroBankState,
    val store: MacroStore,
    val controls: Map<String, MacroControl>,
    val lock: ProLock,
    val sceneId: String,
)

internal val LocalKnobLinks = compositionLocalOf<KnobLinks?> { null }

@Composable
internal fun ProvideKnobLinks(
    sceneId: String,
    lock: ProLock,
    content: @Composable () -> Unit,
) {
    val context = LocalContext.current
    val store = remember { context.geodeContainer.macros }
    val bank by store.state.collectAsStateWithLifecycle()
    val controls = remember(bank) { store.controls() }
    val links = remember(bank, controls, lock, sceneId) { KnobLinks(bank, store, controls, lock, sceneId) }
    CompositionLocalProvider(LocalKnobLinks provides links, content = content)
}

/** One slider's place among the knobs; null when no knob can move it. [open] starts "Add to knob". */
internal class KnobSlot(
    val key: String,
    val control: MacroControl,
    val links: KnobLinks,
    /** The names of the knobs that move it now, for its mark and TalkBack; empty when none does. */
    val movedBy: String,
    private val asking: MutableState<Boolean>,
) {
    val showing: Boolean get() = asking.value

    fun open() {
        if (links.lock.allows(EDITOR_KEY)) asking.value = true else links.lock.onLocked()
    }

    fun close() {
        asking.value = false
    }
}

@Composable
internal fun rememberKnobSlot(key: String): KnobSlot? {
    val links = LocalKnobLinks.current ?: return null
    val asking = rememberSaveable(key) { mutableStateOf(false) }
    val control = links.controls[key] ?: return null
    val names =
        links.bank.knobs
            .filter { it.id in control.knobIds }
            .joinToString { it.name }
    return remember(key, control, links, names) { KnobSlot(key, control, links, names, asking) }
}

/** A small knob mark beside a slider some knob moves. */
@Composable
internal fun KnobMark(slot: KnobSlot?) {
    if (slot == null || slot.movedBy.isEmpty()) return
    Icon(
        Icons.Filled.Tune,
        contentDescription = stringResource(R.string.macro_moved_by, slot.movedBy),
        modifier = Modifier.padding(start = GeodeSpacing.XS).size(MARK_SIZE),
        tint = accentTextColor(),
    )
}

/**
 * "Add to knob" for one slider: every saved knob, a tap putting the slider's param on it or taking it off,
 * and New macro, a draft that already moves it (native names it after the slider), opened to be saved.
 */
@Composable
internal fun AddToKnobHost(
    slot: KnobSlot,
    name: String,
) {
    val links = slot.links
    var making by rememberSaveable(slot.key) { mutableStateOf(false) }
    MacroDraftHost(links.bank, links.store, links.sceneId, showing = making, onClosed = { making = false })
    if (!slot.showing) return
    val editor = remember(links.bank, links.sceneId) { links.store.editor(links.sceneId) }
    val draftId = links.bank.draft?.id
    val target = slot.control.target
    AlertDialog(
        onDismissRequest = slot::close,
        title = { Text(stringResource(R.string.macro_add_to_knob_title, name), maxLines = 2, overflow = TextOverflow.Ellipsis) },
        text = {
            Column(Modifier.heightIn(max = LIST_MAX_HEIGHT).verticalScroll(rememberScrollState())) {
                val movesIt = stringResource(R.string.macro_knob_moves_it)
                editor.knobs.filter { it.id != draftId }.forEach { knob ->
                    val linked = knob.id in slot.control.knobIds
                    val enabled = linked || knob.canLink
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .heightIn(min = GeodeSpacing.TouchTarget)
                            .alpha(if (enabled) 1f else DISABLED_ALPHA)
                            .clickable(enabled = enabled, role = Role.Checkbox) {
                                if (linked) links.store.removeLink(knob.id, target) else links.store.addLink(knob.id, target)
                            }.semantics { if (linked) stateDescription = movesIt },
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(Modifier.weight(1f)) {
                            Text(knob.name, style = MaterialTheme.typography.bodyLarge)
                            if (linked) Text(movesIt, style = MaterialTheme.typography.bodySmall, color = accentTextColor())
                        }
                        if (linked) StoneIconArt(StoneIcon.CHECK, null)
                    }
                }
                TextButton(
                    onClick = {
                        if (links.store.newDraftFor(slot.key) != null) {
                            slot.close()
                            making = true
                        }
                    },
                    enabled = editor.canAdd,
                    modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget),
                ) {
                    StoneIconArt(StoneIcon.ADD, null)
                    Text(stringResource(R.string.macro_new_knob), Modifier.padding(start = GeodeSpacing.S))
                }
            }
        },
        confirmButton = { TextButton(onClick = slot::close) { Text(stringResource(R.string.macro_done)) } },
    )
}

/** The knob editor's section key in the Customize map, whose lock every knob edit follows. */
internal const val EDITOR_KEY = "@macro_editor"
private val MARK_SIZE = 16.dp
private val LIST_MAX_HEIGHT = 420.dp
private const val DISABLED_ALPHA = 0.5f
