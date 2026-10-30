package dev.geode.ui

import android.content.Context
import android.content.Intent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import dev.geode.render.ReactSource
import dev.geode.render.scene.MacroDestination
import dev.geode.render.scene.MacroIcons
import dev.geode.render.scene.MacroKnobs
import dev.geode.render.scene.MacroParams
import dev.geode.render.scene.MacroSet
import dev.geode.render.scene.MacroSetCodec
import dev.geode.render.scene.MacroSound
import dev.geode.render.scene.SceneParams
import dev.geode.render.scene.UserMacro
import kotlinx.coroutines.delay
import kotlin.math.roundToInt

/**
 * The macro editor: every knob of the active set, Serum-style. Per knob: its name and icon,
 * whether it rests in the middle, the controls it turns and how far, and the sounds that turn it
 * by themselves. Below, the macro-preset folder: save the set, load or share a saved one, import
 * one somebody shared. Every change applies at once (the knobs on the Macros tab follow), and
 * undo there covers what the knobs did to the look.
 */
@Composable
internal fun MacroEditorDialog(
    set: MacroSet,
    onChange: (MacroSet) -> Unit,
    sceneId: String,
    look: SceneParams,
    actions: MacroEditorActions,
    onDismiss: () -> Unit,
) {
    var selected by rememberSaveable { mutableIntStateOf(0) }
    val index = selected.coerceIn(0, (set.macros.size - 1).coerceAtLeast(0))
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Column(
            Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.surface)
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Edit macros", style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f))
                TextButton(onClick = onDismiss) { Text("Done") }
            }
            OutlinedTextField(
                value = set.name,
                onValueChange = { onChange(set.copy(name = it)) },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                label = { Text("Set name") },
            )
            EditorHeader("Knobs")
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                set.macros.forEachIndexed { i, m ->
                    FilterChip(
                        selected = i == index,
                        onClick = { selected = i },
                        leadingIcon = { Icon(macroIcon(m.icon), contentDescription = null, modifier = Modifier.size(16.dp)) },
                        label = { Text(m.name) },
                    )
                }
                TextButton(onClick = {
                    onChange(set.copy(macros = set.macros + newMacro(set.macros.size)))
                    selected = set.macros.size
                }) { Text("+ Add knob") }
            }
            set.macros.getOrNull(index)?.let { macro ->
                MacroFields(
                    macro = macro,
                    sceneId = sceneId,
                    look = look,
                    onChange = { changed -> onChange(set.copy(macros = set.macros.toMutableList().also { it[index] = changed })) },
                    onDelete = {
                        onChange(set.copy(macros = set.macros.filterIndexed { i, _ -> i != index }))
                        selected = (index - 1).coerceAtLeast(0)
                    },
                )
            }
            HorizontalDivider(Modifier.padding(vertical = 16.dp))
            MacroFolder(set, actions)
        }
    }
}

@Composable
private fun EditorHeader(title: String) {
    Text(
        title.uppercase(),
        modifier = Modifier.padding(top = 16.dp, bottom = 4.dp),
        style = MaterialTheme.typography.labelMedium,
        color = accentTextColor(),
    )
}

@Composable
private fun MacroFields(
    macro: UserMacro,
    sceneId: String,
    look: SceneParams,
    onChange: (UserMacro) -> Unit,
    onDelete: () -> Unit,
) {
    var pickingControl by remember { mutableStateOf(false) }
    var pickingSound by remember { mutableStateOf(false) }
    OutlinedTextField(
        value = macro.name,
        onValueChange = { onChange(macro.copy(name = it)) },
        modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
        singleLine = true,
        label = { Text("Knob name") },
    )
    FlowRow(Modifier.padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        MacroIcons.ALL.forEach { id ->
            FilterChip(
                selected = macro.icon == id,
                onClick = { onChange(macro.copy(icon = id)) },
                label = { Icon(macroIcon(id), contentDescription = "Icon $id", modifier = Modifier.size(18.dp)) },
            )
        }
    }
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text("Turns both ways", style = MaterialTheme.typography.bodyMedium)
            Text(
                "Rests in the middle, like Energy; off rests at zero, like Chaos.",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        Switch(
            checked = macro.bipolar,
            onCheckedChange = { on -> onChange(macro.copy(bipolar = on, value = if (on) 0.5f else 0f)) },
        )
    }

    EditorHeader("Controls it turns")
    if (macro.destinations.isEmpty()) {
        Text(
            "None yet. Add controls one by one, or set up a look with the sliders and record it.",
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
    macro.destinations.forEachIndexed { i, d ->
        DestinationRow(
            destination = d,
            live = MacroParams.of(d.key)?.scope?.appliesTo(sceneId) == true,
            onChange = { changed -> onChange(macro.copy(destinations = macro.destinations.toMutableList().also { it[i] = changed })) },
            onRemove = { onChange(macro.copy(destinations = macro.destinations.filterIndexed { j, _ -> j != i })) },
        )
    }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        CrystalButton(compact = true, filled = false, onClick = { pickingControl = true }) { Text("Add a control") }
        CrystalButton(compact = true, filled = false, onClick = {
            val recorded = MacroKnobs.capture(SceneParams.DEFAULT, look, sceneId, macro.rest)
            if (recorded.isNotEmpty()) onChange(macro.copy(destinations = recorded))
        }) { Text("Record current look") }
    }
    Text(
        "Record: every control you've moved from its default becomes a destination, so turning " +
            "this knob all the way brings the look back.",
        style = MaterialTheme.typography.labelSmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )

    EditorHeader("Sounds that turn it")
    macro.sounds.forEachIndexed { i, s ->
        SoundRow(
            sound = s,
            onChange = { changed -> onChange(macro.copy(sounds = macro.sounds.toMutableList().also { it[i] = changed })) },
            onRemove = { onChange(macro.copy(sounds = macro.sounds.filterIndexed { j, _ -> j != i })) },
        )
    }
    CrystalButton(compact = true, filled = false, onClick = { pickingSound = true }) { Text("Add a sound") }
    TextButton(onClick = onDelete, modifier = Modifier.padding(top = 8.dp)) { Text("Delete this knob") }

    if (pickingControl) {
        ControlPicker(
            sceneId = sceneId,
            taken = macro.destinations.map { it.key }.toSet(),
            onPick = { key -> onChange(macro.copy(destinations = macro.destinations + MacroDestination(key, DEFAULT_AMOUNT))) },
            onDismiss = { pickingControl = false },
        )
    }
    if (pickingSound) {
        PickerDialog(
            title = "Which sound",
            options = ReactSource.entries.map { it.label },
            onPick = { i -> onChange(macro.copy(sounds = macro.sounds + MacroSound(ReactSource.entries[i], DEFAULT_AMOUNT))) },
            onDismiss = { pickingSound = false },
        )
    }
}

@Composable
private fun DestinationRow(
    destination: MacroDestination,
    live: Boolean,
    onChange: (MacroDestination) -> Unit,
    onRemove: () -> Unit,
) {
    Column(Modifier.padding(vertical = 2.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                "${destination.key} ${signedPercent(destination.amount)}",
                style = MaterialTheme.typography.labelMedium,
                modifier = Modifier.weight(1f),
            )
            TextButton(onClick = onRemove, modifier = Modifier.semantics { contentDescription = "Remove ${destination.key}" }) {
                Text("Remove")
            }
        }
        if (!live) {
            Text(
                "Not on this style; it still turns on styles that have it.",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        CrystalSlider(
            value = destination.amount,
            onValueChange = { onChange(destination.copy(amount = it)) },
            valueRange = -1f..1f,
            modifier =
                Modifier.fillMaxWidth().semantics {
                    contentDescription = "How far ${destination.key} moves"
                },
        )
    }
}

@Composable
private fun SoundRow(
    sound: MacroSound,
    onChange: (MacroSound) -> Unit,
    onRemove: () -> Unit,
) {
    Column(Modifier.padding(vertical = 2.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                "When ${sound.source.label} · ${signedPercent(sound.amount)}",
                style = MaterialTheme.typography.labelMedium,
                modifier = Modifier.weight(1f),
            )
            TextButton(onClick = onRemove) { Text("Remove") }
        }
        CrystalSlider(value = sound.amount, onValueChange = { onChange(sound.copy(amount = it)) }, valueRange = -1f..1f)
        Text("Rise ${"%.2f".format(sound.attackSeconds)} s", style = MaterialTheme.typography.labelSmall)
        CrystalSlider(value = sound.attackSeconds, onValueChange = { onChange(sound.copy(attackSeconds = it)) }, valueRange = 0f..1f)
        Text("Fall ${"%.2f".format(sound.releaseSeconds)} s", style = MaterialTheme.typography.labelSmall)
        CrystalSlider(value = sound.releaseSeconds, onValueChange = { onChange(sound.copy(releaseSeconds = it)) }, valueRange = 0f..3f)
    }
}

@Composable
private fun ControlPicker(
    sceneId: String,
    taken: Set<String>,
    onPick: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var query by remember { mutableStateOf("") }
    val options =
        remember(sceneId, taken, query) {
            MacroParams
                .liveOn(sceneId)
                .map { it.key }
                .filter { it !in taken && (query.isBlank() || it.contains(query, ignoreCase = true)) }
        }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Add a control") },
        text = {
            Column {
                OutlinedTextField(value = query, onValueChange = { query = it }, singleLine = true, label = { Text("Search") })
                Column(Modifier.height(PICKER_HEIGHT).verticalScroll(rememberScrollState())) {
                    options.forEach { key ->
                        TextButton(onClick = {
                            onPick(key)
                            onDismiss()
                        }) { Text(key) }
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Close") } },
    )
}

@Composable
private fun PickerDialog(
    title: String,
    options: List<String>,
    onPick: (Int) -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column(Modifier.height(PICKER_HEIGHT).verticalScroll(rememberScrollState())) {
                options.forEachIndexed { i, label ->
                    TextButton(onClick = {
                        onPick(i)
                        onDismiss()
                    }) { Text(label) }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Close") } },
    )
}

/** The macro-preset folder: save, load, share, import, and back to the standard six. */
@Composable
private fun MacroFolder(
    set: MacroSet,
    actions: MacroEditorActions,
) {
    val context = LocalContext.current
    var folder by rememberSaveable { mutableStateOf("") }
    var note by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(note) {
        if (note != null) {
            delay(NOTE_MS)
            note = null
        }
    }
    Text("Macro presets", style = MaterialTheme.typography.titleMedium)
    Text(
        "Save this set to come back to it, or share it as text. Sets live in folders, like presets.",
        style = MaterialTheme.typography.labelSmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    OutlinedTextField(
        value = folder,
        onValueChange = { folder = it },
        modifier = Modifier.fillMaxWidth().padding(top = 6.dp),
        singleLine = true,
        label = { Text("Folder (optional)") },
    )
    FlowRow(Modifier.padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        CrystalButton(compact = true, onClick = {
            actions.onSave(folder.trim()) { ok -> note = if (ok) "Saved “${set.name}”" else "Could not save the set" }
        }) { Text("Save set") }
        CrystalButton(compact = true, filled = false, onClick = { shareMacroSet(context, set) }) { Text("Share set") }
        CrystalButton(compact = true, filled = false, onClick = {
            val text = clipboardText(context)
            note = if (text != null && actions.onImport(text)) "Imported from the clipboard" else "No macro set on the clipboard"
        }) { Text("Paste a shared set") }
        CrystalButton(compact = true, filled = false, onClick = actions.onRestoreStandard) { Text("Standard six") }
    }
    note?.let { Text(it, style = MaterialTheme.typography.labelSmall, color = accentTextColor()) }
    actions.saved.groupBy { it.folder }.forEach { (name, sets) ->
        Text(
            name.ifBlank { "No folder" },
            modifier = Modifier.padding(top = 10.dp),
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        sets.forEach { saved ->
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(saved.name, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
                TextButton(onClick = {
                    actions.onLoad(saved) { ok -> note = if (ok) "Loaded “${saved.name}”" else "Could not read that set" }
                }) { Text("Load") }
                TextButton(
                    onClick = { actions.onDelete(saved) },
                    modifier = Modifier.semantics { contentDescription = "Delete ${saved.name}" },
                ) { Text("Delete") }
            }
        }
    }
}

private fun shareMacroSet(
    context: Context,
    set: MacroSet,
) {
    val send =
        Intent(Intent.ACTION_SEND)
            .setType("text/plain")
            .putExtra(Intent.EXTRA_SUBJECT, "MuzViz macro set: ${set.name}")
            .putExtra(Intent.EXTRA_TEXT, MacroSetCodec.encode(set))
    context.startActivity(Intent.createChooser(send, "Share macro set"))
}

private fun signedPercent(amount: Float): String = "%+d%%".format((amount * PERCENT_SCALE).roundToInt())

private val PICKER_HEIGHT = 360.dp
private const val DEFAULT_AMOUNT = 0.3f
private const val PERCENT_SCALE = 100f
private const val NOTE_MS = 2500L
