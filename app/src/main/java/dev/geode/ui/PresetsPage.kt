package dev.geode.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.PlaylistPlay
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.CreateNewFolder
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.AssistChip
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.model.PresetEntry
import dev.geode.model.PresetFilter
import dev.geode.model.PresetFiltering
import dev.geode.model.StyleEntry
import dev.geode.render.VisualizerView
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.StyleFamilies
import dev.geode.ui.pro.ProBadge
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.performStoneHaptic

/**
 * Visuals › Presets: the user's saved looks and the built-ins, as pictures.
 *
 * It was a list with six icon buttons on every row (apply, share, playlist, move, delete, and a
 * folder header's rename), a second list of built-ins under it and a save form at the bottom, so
 * the one thing people came for, putting a look on the stage, was one small icon among many.
 * Now a card is the preset: a tap applies it, and everything else sits behind a long press (or
 * the TalkBack action "Preset actions") in one sheet. Folders became filter chips beside All,
 * For this style, Mine and Built-in, so finding a preset no longer means scrolling past the rest.
 *
 * One way to save, at the top: "Save current look" opens one dialog with the name and the
 * folder, and calls the same [VisualsViewModel.savePreset] as Customize's "Save as preset", with
 * the stage's own shader text so a custom GLSL look is kept too.
 *
 * A preset of a Pro style shows the Pro badge; tapping it opens the paywall through the session's
 * gate, as the old Apply button did, rather than hiding the card: seeing what Pro holds is the
 * case for it.
 */
@Composable
internal fun PresetsTab(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val gate by viewModel.gate.collectAsStateWithLifecycle()
    val presetFolders by visualsViewModel.presetFolders.collectAsStateWithLifecycle()
    val folders = presetFolders.folders
    val context = LocalContext.current
    var filterKey by rememberSaveable { mutableStateOf(FILTER_ALL) }
    // The card the user tapped last, so it keeps the check over another preset with the same look.
    var lastTapped by rememberSaveable { mutableStateOf<String?>(null) }
    var dialog by remember { mutableStateOf<PresetDialog?>(null) }
    var menuFor by remember { mutableStateOf<String?>(null) }
    var importNote by remember { mutableStateOf<String?>(null) }
    var showTemplates by remember { mutableStateOf(false) }
    var showBackground by remember { mutableStateOf(false) }
    val presetFilePicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) {
                visualsViewModel.importPresetFile(uri) { name ->
                    importNote = name?.let { "Added \"$it\" to Mine." } ?: "That file is not a MuzViz preset."
                }
            }
        }
    val byName = remember(viz.presets) { viz.presets.associateBy { it.name } }
    val entries =
        remember(viz.presets, viz.sceneId, viz.params, presetFolders, lastTapped) {
            viz.presets.map { p ->
                val looks = p.sceneId == viz.sceneId && p.params == viz.params
                PresetEntry(
                    name = p.name,
                    sceneId = p.sceneId,
                    builtIn = BuiltInPresets.isBuiltIn(p.name),
                    folder = presetFolders.folderOf(p.name),
                    forThisStyle = builtInPresetMatchesScene(p.sceneId, viz.sceneId),
                    // Every MilkDrop preset shares the default params; only the file tells them
                    // apart, so a MilkDrop card is marked only when the user tapped it.
                    onStage = looks && (p.sceneId != SceneIds.MILKDROP || p.name == lastTapped),
                )
            }
        }
    val filters = remember(folders) { PresetFiltering.filters(folders) }
    val filter = filters.firstOrNull { filterKeyOf(it) == filterKey } ?: PresetFilter.All
    val shown = remember(entries, filter) { PresetFiltering.apply(entries, filter) }
    val applied = remember(entries, lastTapped) { PresetFiltering.applied(entries, lastTapped) }
    val openFolder = (filter as? PresetFilter.Folder)?.name

    Column(Modifier.fillMaxSize()) {
        PresetsToolbar(
            openFolder = openFolder,
            onSave = { dialog = PresetDialog.Save(openFolder.orEmpty()) },
            onPaste = {
                val pasted = clipboardText(context)
                importNote =
                    when {
                        pasted.isNullOrBlank() -> "The clipboard is empty. Copy a shared preset link first."
                        else ->
                            visualsViewModel.importPresetLink(pasted)?.let { "Added \"$it\" to Mine." }
                                ?: "The clipboard does not hold a MuzViz preset link."
                    }
            },
            onOpenFile = { presetFilePicker.launch(arrayOf("*/*")) },
            onRenameFolder = { openFolder?.let { dialog = PresetDialog.RenameFolder(it) } },
            onTemplates = { showTemplates = true },
            onBackground = { showBackground = true },
        )
        importNote?.let { note ->
            Text(
                note,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier =
                    Modifier
                        .padding(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.XS)
                        .semantics { liveRegion = LiveRegionMode.Polite },
            )
        }
        PresetFilterRow(
            filters = filters,
            selected = filter,
            onSelect = { filterKey = filterKeyOf(it) },
            onRenameFolder = { dialog = PresetDialog.RenameFolder(it) },
            onNewFolder = { dialog = PresetDialog.NewFolder() },
        )
        if (shown.isEmpty()) {
            EmptyPresets(filter)
        } else {
            LazyVerticalGrid(
                columns = GridCells.Adaptive(CARD_MIN_WIDTH),
                contentPadding = PaddingValues(GeodeSpacing.M),
                horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
                verticalArrangement = Arrangement.spacedBy(GeodeSpacing.M),
                modifier = Modifier.fillMaxSize(),
            ) {
                items(shown, key = { it.name }) { entry ->
                    PresetCard(
                        entry = entry,
                        applied = entry.name == applied,
                        locked = !gate.allowsStyle(entry.sceneId),
                        inPlaylist = vizPlaylistIndexOf(viz.vizPlaylist, entry.name) >= 0,
                        onTap = {
                            byName[entry.name]?.let { p ->
                                lastTapped = p.name
                                viewModel.applyPreset(p)
                            }
                        },
                        onMenu = { menuFor = entry.name },
                    )
                }
            }
        }
    }

    // A preset deleted or renamed from elsewhere while its sheet was open just closes the sheet.
    menuFor?.let(byName::get)?.let { preset ->
        val name = preset.name
        val playlistIndex = vizPlaylistIndexOf(viz.vizPlaylist, name)
        PresetActionsSheet(
            preset = preset,
            builtIn = BuiltInPresets.isBuiltIn(name),
            inPlaylist = playlistIndex >= 0,
            onDismiss = { menuFor = null },
            onAction = { action ->
                menuFor = null
                when (action) {
                    PresetAction.PLAYLIST ->
                        if (playlistIndex >= 0) {
                            viewModel.removeVizPlaylistAt(playlistIndex)
                        } else {
                            viewModel.addToVizPlaylist(
                                VizPlaylistEntry(sceneId = preset.sceneId, presetName = name, label = name),
                            )
                        }
                    PresetAction.SHARE -> sharePreset(context, visualsViewModel, name)
                    PresetAction.DUPLICATE ->
                        visualsViewModel.duplicatePreset(name)?.let { importNote = "Made a copy: \"$it\"." }
                    PresetAction.RENAME -> dialog = PresetDialog.Rename(name)
                    PresetAction.MOVE -> dialog = PresetDialog.Move(name)
                    PresetAction.REPLACE -> dialog = PresetDialog.Replace(name)
                    PresetAction.DELETE -> dialog = PresetDialog.Delete(name)
                }
            },
        )
    }

    val saveCurrent: (String, String) -> Unit = { name, folder ->
        visualsViewModel.savePreset(
            name,
            visualizerView.visualizerRenderer.customShaderFor(viewModel.vizState.value.sceneId),
            folder,
        )
        // What was just saved is what the stage shows, so its card gets the check.
        lastTapped = PresetFiltering.cleanName(name)
    }
    val close = { dialog = null }
    when (val d = dialog) {
        null -> Unit
        is PresetDialog.Save ->
            SavePresetDialog(
                initialFolder = d.folder,
                folders = folders,
                presets = viz.presets,
                onDismiss = close,
                onSave = { name, folder ->
                    saveCurrent(name, folder)
                    close()
                },
            )
        is PresetDialog.Rename ->
            NameDialog(
                title = "Rename preset",
                initial = d.name,
                confirmLabel = "Rename",
                error = { PresetFiltering.renameError(it, d.name, viz.presets.map { p -> p.name }) },
                onDismiss = close,
                onConfirm = { to ->
                    visualsViewModel.renamePreset(d.name, to)?.let { if (lastTapped == d.name) lastTapped = it }
                    close()
                },
            )
        is PresetDialog.Move ->
            MovePresetDialog(
                name = d.name,
                current = presetFolders.folderOf(d.name),
                folders = folders,
                onDismiss = close,
                onMove = { folder ->
                    visualsViewModel.movePresetToFolder(d.name, folder)
                    close()
                },
                onNewFolder = { dialog = PresetDialog.NewFolder(thenMove = d.name) },
            )
        is PresetDialog.Replace ->
            ConfirmDialog(
                title = "Replace \"${d.name}\"?",
                text =
                    "It takes the look on the stage now. Its old look is gone for good; there is no undo. " +
                        "Share it first if you might want it back.",
                confirmLabel = "Replace",
                onDismiss = close,
                onConfirm = {
                    saveCurrent(d.name, presetFolders.folderOf(d.name))
                    close()
                },
            )
        is PresetDialog.Delete ->
            ConfirmDialog(
                title = "Delete \"${d.name}\"?",
                text =
                    "Removes this preset and its file for good; there is no undo. " +
                        "Share it first if you might want it back.",
                confirmLabel = "Delete",
                onDismiss = close,
                onConfirm = {
                    visualsViewModel.deletePreset(d.name)
                    close()
                },
            )
        is PresetDialog.NewFolder ->
            NameDialog(
                title = "New folder",
                initial = "",
                confirmLabel = "Make folder",
                error = { folderNameError(it, null, folders) },
                onDismiss = close,
                onConfirm = { raw ->
                    val name = raw.trim()
                    visualsViewModel.addPresetFolder(name)
                    if (d.thenMove != null) {
                        visualsViewModel.movePresetToFolder(d.thenMove, name)
                    } else {
                        filterKey = filterKeyOf(PresetFilter.Folder(name))
                    }
                    close()
                },
            )
        is PresetDialog.RenameFolder ->
            NameDialog(
                title = "Rename folder",
                initial = d.name,
                confirmLabel = "Rename",
                error = { folderNameError(it, d.name, folders) },
                onDismiss = close,
                onConfirm = { raw ->
                    val to = raw.trim()
                    visualsViewModel.renamePresetFolder(d.name, to)
                    if (openFolder == d.name) filterKey = filterKeyOf(PresetFilter.Folder(to))
                    close()
                },
            )
    }
    if (showTemplates) {
        TemplatesSheet(viewModel, visualizerView, onDismiss = { showTemplates = false })
    }
    if (showBackground) {
        BackgroundSheet(onDismiss = { showBackground = false })
    }
}

/** What the preset sheet offers; [PresetsTab] decides what each one does. */
internal enum class PresetAction { PLAYLIST, SHARE, DUPLICATE, RENAME, MOVE, REPLACE, DELETE }

/** What the page asks the user about, one at a time. */
private sealed interface PresetDialog {
    data class Save(
        val folder: String,
    ) : PresetDialog

    data class Rename(
        val name: String,
    ) : PresetDialog

    data class Move(
        val name: String,
    ) : PresetDialog

    data class Replace(
        val name: String,
    ) : PresetDialog

    data class Delete(
        val name: String,
    ) : PresetDialog

    /** [thenMove] is the preset that asked for the folder from Move, which goes into it at once. */
    data class NewFolder(
        val thenMove: String? = null,
    ) : PresetDialog

    data class RenameFolder(
        val name: String,
    ) : PresetDialog
}

/**
 * Save first and full size: it is the page's main action. Import and the page's less used
 * doors (renaming the open folder, video templates, the background image) sit in menus so the
 * row never wraps to a second line on a phone.
 */
@Composable
private fun PresetsToolbar(
    openFolder: String?,
    onSave: () -> Unit,
    onPaste: () -> Unit,
    onOpenFile: () -> Unit,
    onRenameFolder: () -> Unit,
    onTemplates: () -> Unit,
    onBackground: () -> Unit,
) {
    var importOpen by remember { mutableStateOf(false) }
    var moreOpen by remember { mutableStateOf(false) }
    Row(
        Modifier.fillMaxWidth().padding(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.XS),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        CrystalButton(onClick = onSave, modifier = Modifier.weight(1f)) {
            Icon(Icons.Filled.Add, contentDescription = null, modifier = Modifier.size(18.dp))
            Text("Save current look", maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        Box {
            CrystalButton(filled = false, onClick = { importOpen = true }) { Text("Import") }
            DropdownMenu(expanded = importOpen, onDismissRequest = { importOpen = false }) {
                DropdownMenuItem(
                    text = { Text("Paste a shared preset link") },
                    onClick = {
                        importOpen = false
                        onPaste()
                    },
                )
                DropdownMenuItem(
                    text = { Text("Open a preset file") },
                    onClick = {
                        importOpen = false
                        onOpenFile()
                    },
                )
            }
        }
        Box {
            IconButton(onClick = { moreOpen = true }) {
                Icon(Icons.Filled.MoreVert, contentDescription = "More preset options")
            }
            DropdownMenu(expanded = moreOpen, onDismissRequest = { moreOpen = false }) {
                if (openFolder != null) {
                    DropdownMenuItem(
                        text = { Text("Rename this folder") },
                        onClick = {
                            moreOpen = false
                            onRenameFolder()
                        },
                    )
                }
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.template_entry_point)) },
                    onClick = {
                        moreOpen = false
                        onTemplates()
                    },
                )
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.background_entry_point)) },
                    onClick = {
                        moreOpen = false
                        onBackground()
                    },
                )
            }
        }
    }
}

/** The shelves, then a chip per folder, then "New folder" so a folder is made where folders are. */
@Composable
private fun PresetFilterRow(
    filters: List<PresetFilter>,
    selected: PresetFilter,
    onSelect: (PresetFilter) -> Unit,
    onRenameFolder: (String) -> Unit,
    onNewFolder: () -> Unit,
) {
    LazyRow(
        contentPadding = PaddingValues(horizontal = GeodeSpacing.L),
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        items(filters, key = ::filterKeyOf) { f ->
            val folder = (f as? PresetFilter.Folder)?.name
            FilterChip(
                selected = f == selected,
                onClick = { onSelect(f) },
                label = { Text(filterLabel(f)) },
                leadingIcon =
                    if (folder != null) {
                        { Icon(Icons.Filled.Folder, contentDescription = null, modifier = Modifier.size(18.dp)) }
                    } else {
                        null
                    },
                modifier =
                    Modifier
                        .heightIn(min = GeodeSpacing.TouchTarget)
                        .then(
                            if (folder == null) {
                                Modifier
                            } else {
                                Modifier.semantics {
                                    customActions =
                                        listOf(
                                            CustomAccessibilityAction("Rename folder") {
                                                onRenameFolder(folder)
                                                true
                                            },
                                        )
                                }
                            },
                        ),
            )
        }
        item(key = "new_folder") {
            AssistChip(
                onClick = onNewFolder,
                label = { Text("New folder") },
                leadingIcon = {
                    Icon(Icons.Filled.CreateNewFolder, contentDescription = null, modifier = Modifier.size(18.dp))
                },
                modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget),
            )
        }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun PresetCard(
    entry: PresetEntry,
    applied: Boolean,
    locked: Boolean,
    inPlaylist: Boolean,
    onTap: () -> Unit,
    onMenu: () -> Unit,
) {
    val view = LocalView.current
    val cs = MaterialTheme.colorScheme
    val shape = RoundedCornerShape(GeodeRadius.Medium)
    val styleLabel = sceneDisplayLabel(entry.sceneId)
    val state = listOfNotNull("Applied".takeIf { applied }, "In the visual playlist".takeIf { inPlaylist })
    Column(
        Modifier
            .clip(shape)
            .combinedClickable(
                role = Role.Button,
                onClick = onTap,
                onClickLabel = "Apply",
                onLongClick = {
                    view.performStoneHaptic(StoneHapticCue.CONFIRM)
                    onMenu()
                },
                onLongClickLabel = ACTIONS_LABEL,
            ).semantics {
                if (state.isNotEmpty()) stateDescription = state.joinToString()
                customActions =
                    listOf(
                        CustomAccessibilityAction(ACTIONS_LABEL) {
                            onMenu()
                            true
                        },
                    )
            },
    ) {
        Box(
            Modifier
                .fillMaxWidth()
                .aspectRatio(1f)
                .clip(shape)
                .then(if (applied) Modifier.border(2.dp, cs.primary, shape) else Modifier),
        ) {
            StyleThumbnail(thumbnailEntry(entry, styleLabel), Modifier.fillMaxSize())
            if (locked) ProBadge(Modifier.align(Alignment.TopEnd).padding(GeodeSpacing.XS))
            if (inPlaylist) {
                val scrim = cs.scrim.copy(alpha = SCRIM_ALPHA)
                CardMark(Icons.AutoMirrored.Filled.PlaylistPlay, Color.White, scrim, Alignment.TopStart)
            }
            if (applied) CardMark(Icons.Filled.Check, cs.onPrimary, cs.primary, Alignment.BottomEnd)
        }
        Text(
            entry.name,
            Modifier.padding(top = GeodeSpacing.XS, start = GeodeSpacing.XXS, end = GeodeSpacing.XXS),
            style = MaterialTheme.typography.bodyMedium,
            color = if (applied) accentTextColor() else cs.onSurface,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            styleLabel,
            Modifier.padding(horizontal = GeodeSpacing.XXS),
            style = MaterialTheme.typography.bodySmall,
            color = cs.onSurfaceVariant,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

/** A small round mark on a card's corner. Decorative: the card's state description says it. */
@Composable
private fun BoxScope.CardMark(
    icon: ImageVector,
    tint: Color,
    fill: Color,
    at: Alignment,
) {
    Icon(
        icon,
        contentDescription = null,
        tint = tint,
        modifier =
            Modifier
                .align(at)
                .padding(GeodeSpacing.XS)
                .background(fill, RoundedCornerShape(50))
                .padding(GeodeSpacing.XXS)
                .size(16.dp),
    )
}

/**
 * The still a preset card shows: its style's. A MilkDrop preset's look lives in its own file,
 * which no still can show, so it gets the fallback art under an id of its own; the gradient then
 * differs from one MilkDrop preset to the next instead of making them all one colour.
 */
private fun thumbnailEntry(
    entry: PresetEntry,
    styleLabel: String,
): StyleEntry =
    if (entry.sceneId == SceneIds.MILKDROP) {
        StyleEntry(id = "milkdrop-preset:${entry.name}", label = entry.name, kindLabel = styleLabel, familyLabel = null)
    } else {
        StyleEntry(
            id = entry.sceneId,
            label = styleLabel,
            kindLabel = styleLabel,
            familyLabel = StyleFamilies.of(entry.sceneId)?.label,
        )
    }

@Composable
private fun EmptyPresets(filter: PresetFilter) {
    val text =
        when (filter) {
            PresetFilter.All, PresetFilter.Mine ->
                "You have not saved a preset yet. Set up a look you like, then tap Save current look."
            PresetFilter.ThisStyle -> "No presets for this style yet. Save current look makes the first one."
            PresetFilter.BuiltIn -> "No built-in presets here."
            is PresetFilter.Folder -> "This folder is empty. Press and hold a preset, then choose Move to folder."
        }
    Box(Modifier.fillMaxSize().padding(GeodeSpacing.XL), contentAlignment = Alignment.Center) {
        Text(text, style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

private fun filterKeyOf(filter: PresetFilter): String =
    when (filter) {
        PresetFilter.All -> FILTER_ALL
        PresetFilter.ThisStyle -> "style"
        PresetFilter.Mine -> "mine"
        PresetFilter.BuiltIn -> "builtin"
        is PresetFilter.Folder -> "folder:${filter.name}"
    }

private fun filterLabel(filter: PresetFilter): String =
    when (filter) {
        PresetFilter.All -> "All"
        PresetFilter.ThisStyle -> "For this style"
        PresetFilter.Mine -> "Mine"
        PresetFilter.BuiltIn -> "Built-in"
        is PresetFilter.Folder -> filter.name
    }

private const val FILTER_ALL = "all"
private const val ACTIONS_LABEL = "Preset actions"
private val CARD_MIN_WIDTH = 112.dp
private const val SCRIM_ALPHA = 0.55f
