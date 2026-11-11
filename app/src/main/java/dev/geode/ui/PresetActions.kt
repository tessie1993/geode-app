package dev.geode.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.DriveFileMove
import androidx.compose.material.icons.automirrored.filled.PlaylistAdd
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.CreateNewFolder
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.PlaylistRemove
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Share
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.data.BuiltInPresets
import dev.geode.data.Preset
import dev.geode.model.PresetFiltering
import dev.geode.model.VizPlaylistEntry
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.VisualStyleCatalog
import dev.geode.ui.theme.GeodeSpacing

/**
 * The long-press sheet of a preset row. It and the dialogs below hold no state beyond what is
 * typed, so `PresetsTab` stays the one place that decides what an action does.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun PresetActionsSheet(
    preset: Preset,
    builtIn: Boolean,
    inPlaylist: Boolean,
    onDismiss: () -> Unit,
    onAction: (PresetAction) -> Unit,
) {
    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(Modifier.fillMaxWidth().navigationBarsPadding().padding(bottom = GeodeSpacing.S)) {
            Text(
                preset.name,
                Modifier.padding(horizontal = GeodeSpacing.XL),
                style = MaterialTheme.typography.titleMedium,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                sceneDisplayLabel(preset.sceneId).let { if (builtIn) stringResource(R.string.presets_built_in_style, it) else it },
                Modifier.padding(horizontal = GeodeSpacing.XL, vertical = GeodeSpacing.XS),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            SheetAction(
                if (inPlaylist) Icons.Filled.PlaylistRemove else Icons.AutoMirrored.Filled.PlaylistAdd,
                stringResource(if (inPlaylist) R.string.presets_playlist_remove else R.string.presets_playlist_add),
            ) { onAction(PresetAction.PLAYLIST) }
            SheetAction(Icons.Filled.Share, stringResource(R.string.presets_share)) { onAction(PresetAction.SHARE) }
            SheetAction(Icons.Filled.ContentCopy, stringResource(R.string.presets_duplicate)) { onAction(PresetAction.DUPLICATE) }
            if (builtIn) {
                Text(
                    stringResource(R.string.presets_built_in_note),
                    Modifier.padding(horizontal = GeodeSpacing.XL, vertical = GeodeSpacing.S),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            } else {
                SheetAction(Icons.Filled.Edit, stringResource(R.string.action_rename)) { onAction(PresetAction.RENAME) }
                SheetAction(Icons.AutoMirrored.Filled.DriveFileMove, stringResource(R.string.presets_move_to_folder)) {
                    onAction(PresetAction.MOVE)
                }
                SheetAction(Icons.Filled.Refresh, stringResource(R.string.presets_replace_with_current)) {
                    onAction(PresetAction.REPLACE)
                }
                SheetAction(Icons.Filled.Delete, stringResource(R.string.action_delete), MaterialTheme.colorScheme.error) {
                    onAction(PresetAction.DELETE)
                }
            }
        }
    }
}

@Composable
private fun SheetAction(
    icon: ImageVector,
    label: String,
    tint: Color = MaterialTheme.colorScheme.onSurface,
    onClick: () -> Unit,
) {
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = SHEET_ROW_HEIGHT)
            .clickable(role = Role.Button, onClick = onClick)
            .padding(horizontal = GeodeSpacing.XL),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.L),
    ) {
        Icon(icon, contentDescription = null, tint = tint)
        Text(label, style = MaterialTheme.typography.bodyLarge, color = tint)
    }
}

/**
 * The page's one save form. A name that is already one of the user's presets turns the button
 * into Replace and says so, since saving over it cannot be undone; a built-in's name is refused,
 * because a user preset by that name would be taken for the built-in and could not be deleted.
 */
@Composable
internal fun SavePresetDialog(
    initialFolder: String,
    folders: List<String>,
    presets: List<Preset>,
    onDismiss: () -> Unit,
    onSave: (name: String, folder: String) -> Unit,
) {
    var name by rememberSaveable { mutableStateOf("") }
    var folder by rememberSaveable { mutableStateOf(initialFolder) }
    val cleaned = PresetFiltering.cleanName(name)
    val replaces = if (cleaned.isEmpty()) null else presetReplaceTarget(cleaned, presets)
    val builtInClash =
        cleaned.isNotEmpty() &&
            presets.any { BuiltInPresets.isBuiltIn(it.name) && it.name.equals(cleaned, ignoreCase = true) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.presets_save_current_look)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(GeodeSpacing.S)) {
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it },
                    singleLine = true,
                    label = { Text(stringResource(R.string.preset_name)) },
                    isError = builtInClash,
                    supportingText = {
                        when {
                            builtInClash -> Text(stringResource(R.string.presets_name_built_in_clash))
                            replaces != null ->
                                Text(stringResource(R.string.presets_name_replaces, replaces))
                            else -> Unit
                        }
                    },
                    modifier = Modifier.fillMaxWidth(),
                )
                if (folders.isNotEmpty()) FolderPickRow(folder, folders) { folder = it }
            }
        },
        confirmButton = {
            Button(enabled = cleaned.isNotEmpty() && !builtInClash, onClick = { onSave(cleaned, folder) }) {
                Text(stringResource(if (replaces != null) R.string.presets_replace else R.string.action_save))
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) } },
    )
}

/** The save form's folder: one row with the pick and ▾, its menu checking the current one. */
@Composable
private fun FolderPickRow(
    folder: String,
    folders: List<String>,
    onPick: (String) -> Unit,
) {
    var open by remember { mutableStateOf(false) }
    Box {
        Row(
            Modifier
                .fillMaxWidth()
                .heightIn(min = GeodeSpacing.TouchTarget)
                .clickable(role = Role.DropdownList) { open = true },
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
        ) {
            Text(stringResource(R.string.presets_folder), Modifier.weight(1f), style = MaterialTheme.typography.labelLarge)
            Text(
                folder.ifEmpty { NO_FOLDER },
                style = MaterialTheme.typography.bodyLarge,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            (listOf("") + folders).forEach { f ->
                DropdownMenuItem(
                    text = { Text(f.ifEmpty { NO_FOLDER }) },
                    leadingIcon = { if (f == folder) Icon(Icons.Filled.Check, contentDescription = null) },
                    onClick = {
                        open = false
                        onPick(f)
                    },
                )
            }
        }
    }
}

/** A name to type, checked as it is typed: [error] says why it cannot be used, or null. */
@Composable
internal fun NameDialog(
    title: String,
    initial: String,
    confirmLabel: String,
    error: (String) -> String?,
    onDismiss: () -> Unit,
    onConfirm: (String) -> Unit,
) {
    var text by rememberSaveable { mutableStateOf(initial) }
    val problem = error(text)
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            OutlinedTextField(
                value = text,
                onValueChange = { text = it },
                singleLine = true,
                // An empty field says nothing yet: "give it a name" before typing is a scold.
                isError = problem != null && text.isNotEmpty(),
                supportingText = { if (problem != null && text.isNotEmpty()) Text(problem) },
                modifier = Modifier.fillMaxWidth(),
            )
        },
        confirmButton = {
            Button(enabled = problem == null, onClick = { onConfirm(text) }) { Text(confirmLabel) }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) } },
    )
}

/** One choice per folder, as radio rows the whole width, so each is a full touch target. */
@Composable
internal fun MovePresetDialog(
    name: String,
    current: String,
    folders: List<String>,
    onDismiss: () -> Unit,
    onMove: (String) -> Unit,
    onNewFolder: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.presets_move_title, name), maxLines = 2, overflow = TextOverflow.Ellipsis) },
        text = {
            Column(Modifier.selectableGroup()) {
                (listOf("") + folders).forEach { f ->
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .heightIn(min = GeodeSpacing.TouchTarget)
                            .selectable(selected = f == current, role = Role.RadioButton, onClick = { onMove(f) }),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        RadioButton(selected = f == current, onClick = null)
                        Text(
                            f.ifEmpty { NO_FOLDER },
                            Modifier.padding(start = GeodeSpacing.M),
                            style = MaterialTheme.typography.bodyLarge,
                        )
                    }
                }
                TextButton(onClick = onNewFolder, modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget)) {
                    Icon(Icons.Filled.CreateNewFolder, contentDescription = null, modifier = Modifier.size(18.dp))
                    Text(stringResource(R.string.presets_new_folder), Modifier.padding(start = GeodeSpacing.S))
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_close)) } },
    )
}

@Composable
internal fun ConfirmDialog(
    title: String,
    text: String,
    confirmLabel: String,
    onDismiss: () -> Unit,
    onConfirm: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title, maxLines = 2, overflow = TextOverflow.Ellipsis) },
        text = { Text(text) },
        confirmButton = { Button(onClick = onConfirm) { Text(confirmLabel) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) } },
    )
}

/** Why [raw] cannot name a folder (renaming [current], or a new one when null), or null. */
internal fun folderNameError(
    raw: String,
    current: String?,
    folders: List<String>,
): String? {
    val name = raw.trim()
    return when {
        name.isEmpty() -> "Give it a name."
        current != null && name.equals(current, ignoreCase = true) -> null
        folders.any { it.equals(name, ignoreCase = true) } -> "There is already a folder called \"$name\"."
        else -> null
    }
}

internal fun vizPlaylistIndexOf(
    playlist: List<VizPlaylistEntry>,
    presetName: String,
): Int = playlist.indexOfFirst { it.presetName == presetName }

/** The user preset a save under [rawName] would overwrite, or null when the name is free. */
internal fun presetReplaceTarget(
    rawName: String,
    presets: List<Preset>,
): String? {
    val name = PresetFiltering.cleanName(rawName).ifEmpty { "Preset" }
    return presets.firstOrNull { !BuiltInPresets.isBuiltIn(it.name) && it.name == name }?.name
}

/** Built-in presets are made for a style family's base id; every variant of the family shares them. */
internal fun builtInPresetSceneFamily(activeSceneId: String): String =
    when {
        VisualStyleCatalog.isCymatics(activeSceneId) -> SceneIds.CYMATICS
        VisualStyleCatalog.isFluid(activeSceneId) -> SceneIds.FLUID
        else -> activeSceneId
    }

internal fun builtInPresetMatchesScene(
    presetSceneId: String,
    activeSceneId: String,
): Boolean = presetSceneId == activeSceneId || presetSceneId == builtInPresetSceneFamily(activeSceneId)

internal fun clipboardText(context: android.content.Context): String? =
    runCatching {
        context
            .getSystemService(android.content.ClipboardManager::class.java)
            ?.primaryClip
            ?.takeIf { it.itemCount > 0 }
            ?.getItemAt(0)
            ?.coerceToText(context)
            ?.toString()
    }.getOrNull()

/**
 * Shares a preset as a `geode://preset` link when it fits in one, else as the preset's JSON file
 * through the presets FileProvider. A built-in has no file, so one too long for a link shares nothing.
 */
internal fun sharePreset(
    context: android.content.Context,
    viewModel: VisualsViewModel,
    name: String,
) {
    val link = viewModel.presetShareLink(name)
    val send =
        if (link != null) {
            android.content.Intent(android.content.Intent.ACTION_SEND).apply {
                type = "text/plain"
                putExtra(android.content.Intent.EXTRA_SUBJECT, "BeatRealms preset: $name")
                putExtra(android.content.Intent.EXTRA_TEXT, link)
            }
        } else {
            val file = viewModel.presetFile(name) ?: return
            val uri =
                androidx.core.content.FileProvider
                    .getUriForFile(context, context.packageName + ".presets", file)
            android.content.Intent(android.content.Intent.ACTION_SEND).apply {
                type = "application/json"
                putExtra(android.content.Intent.EXTRA_STREAM, uri)
                addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
        }
    runCatching { context.startActivity(android.content.Intent.createChooser(send, "Share preset")) }
}

private const val NO_FOLDER = "No folder"
private val SHEET_ROW_HEIGHT = 56.dp
