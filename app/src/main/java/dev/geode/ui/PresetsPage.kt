package dev.geode.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
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
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.CreateNewFolder
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalButton
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalResources
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
import dev.geode.data.BuiltInPresets
import dev.geode.engine.bridge.GeodeNative
import dev.geode.model.PresetEntry
import dev.geode.model.PresetFilter
import dev.geode.model.PresetFiltering
import dev.geode.model.VizPlaylistEntry
import dev.geode.stage.StageView
import dev.geode.ui.theme.GeodeGlass
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.performStoneHaptic
import dev.geode.ui.theme.selectedGlow

/**
 * Visuals › Presets: the user's saved looks and the built-ins, as a text menu like Styles.
 *
 * No pictures (owner): no style has a rendered still, so every card showed a generated gradient
 * that said nothing about the look. A row is the preset: its name, its style under it, whether it
 * is built-in or the user's own, and a lit row with a check for the one on the stage. A tap applies
 * it, and everything else sits behind a long press (or the TalkBack action "Preset actions") in
 * one sheet, never as icons on the row. One column on a phone, more where the options are wide.
 * Toolbar, chips and rows share one 16 dp text edge. Filtering is one scrolling row: the four
 * fixed shelves (All, For this style, Mine, Built-in) as chips, then one "Folders ▾" chip whose
 * menu lists every folder and "New folder", so the row stays one row however many folders exist.
 *
 * One way to save, at the top: "Save look" opens one dialog with the name and the
 * folder, and calls the same [VisualsViewModel.savePreset] as Customize's "Save as preset", with
 * the stage's own shader text so a custom GLSL look is kept too.
 *
 * A preset of a Pro style shows the ♛; tapping it opens the paywall through the session's gate
 * rather than hiding the row: seeing what Pro holds is the case for it.
 */
@Composable
internal fun PresetsTab(
    viewModel: PlayerViewModel,
    visualizerView: StageView,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val gate by viewModel.gate.collectAsStateWithLifecycle()
    val presetFolders by visualsViewModel.presetFolders.collectAsStateWithLifecycle()
    val folders = presetFolders.folders
    val context = LocalContext.current
    val resources = LocalResources.current
    var filterKey by rememberSaveable { mutableStateOf(FILTER_ALL) }
    // The row the user tapped last, so it keeps the check over another preset with the same look.
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
                    importNote =
                        name?.let { resources.getString(R.string.presets_added, it) }
                            ?: resources.getString(R.string.presets_file_not_preset)
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
                    // apart, so a MilkDrop row is marked only when the user tapped it.
                    onStage = looks && (p.sceneId != GeodeNative.STYLE_MILKDROP || p.name == lastTapped),
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
                        pasted.isNullOrBlank() -> resources.getString(R.string.presets_clipboard_empty)
                        else ->
                            visualsViewModel.importPresetLink(pasted)?.let { resources.getString(R.string.presets_added, it) }
                                ?: resources.getString(R.string.presets_clipboard_not_link)
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
                columns = GridCells.Adaptive(PRESET_COLUMN_MIN),
                // The bottom clears the docked mini player's progress line under the page.
                contentPadding = PaddingValues(top = GeodeSpacing.XS, bottom = GeodeSpacing.L),
                verticalArrangement = Arrangement.spacedBy(GeodeSpacing.XS),
                modifier = Modifier.fillMaxSize(),
            ) {
                items(shown, key = { it.name }, contentType = { "preset" }) { entry ->
                    PresetRow(
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
                        visualsViewModel.duplicatePreset(name)?.let { importNote = resources.getString(R.string.presets_copy_made, it) }
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
            visualizerView.stage.customShaderFor(viewModel.vizState.value.sceneId),
            folder,
        )
        // What was just saved is what the stage shows, so its row gets the check.
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
                title = stringResource(R.string.presets_rename_title),
                initial = d.name,
                confirmLabel = stringResource(R.string.action_rename),
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
                title = stringResource(R.string.presets_replace_title, d.name),
                text = stringResource(R.string.presets_replace_text),
                confirmLabel = stringResource(R.string.presets_replace),
                onDismiss = close,
                onConfirm = {
                    saveCurrent(d.name, presetFolders.folderOf(d.name))
                    close()
                },
            )
        is PresetDialog.Delete ->
            ConfirmDialog(
                title = stringResource(R.string.presets_delete_title, d.name),
                text = stringResource(R.string.presets_delete_text),
                confirmLabel = stringResource(R.string.action_delete),
                onDismiss = close,
                onConfirm = {
                    visualsViewModel.deletePreset(d.name)
                    close()
                },
            )
        is PresetDialog.NewFolder ->
            NameDialog(
                title = stringResource(R.string.presets_new_folder),
                initial = "",
                confirmLabel = stringResource(R.string.presets_make_folder),
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
                title = stringResource(R.string.presets_rename_folder),
                initial = d.name,
                confirmLabel = stringResource(R.string.action_rename),
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
internal fun PresetsToolbar(
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
        // The ⋮ is a 48 dp target whose glyph sits 12 dp in, so the row ends 4 dp from the edge.
        Modifier.fillMaxWidth().padding(start = GeodeSpacing.L, end = GeodeSpacing.XS, top = GeodeSpacing.XS),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        Button(
            onClick = onSave,
            modifier = Modifier.weight(1f).heightIn(min = GeodeSpacing.TouchTarget),
            contentPadding = ButtonDefaults.ButtonWithIconContentPadding,
        ) {
            Icon(Icons.Filled.Add, contentDescription = null, modifier = Modifier.size(18.dp))
            Spacer(Modifier.size(ButtonDefaults.IconSpacing))
            // Two words, so one line on a 360 dp phone; at a large font it wraps rather than cut (#866).
            Text(stringResource(R.string.presets_save_look))
        }
        Box {
            FilledTonalButton(
                onClick = { importOpen = true },
                modifier = Modifier.heightIn(min = GeodeSpacing.TouchTarget),
            ) { Text(stringResource(R.string.presets_import), maxLines = 1) }
            DropdownMenu(expanded = importOpen, onDismissRequest = { importOpen = false }) {
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.presets_import_paste)) },
                    onClick = {
                        importOpen = false
                        onPaste()
                    },
                )
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.presets_import_file)) },
                    onClick = {
                        importOpen = false
                        onOpenFile()
                    },
                )
            }
        }
        Box {
            IconButton(onClick = { moreOpen = true }) {
                Icon(Icons.Filled.MoreVert, contentDescription = stringResource(R.string.presets_more_options))
            }
            DropdownMenu(expanded = moreOpen, onDismissRequest = { moreOpen = false }) {
                if (openFolder != null) {
                    DropdownMenuItem(
                        text = { Text(stringResource(R.string.presets_rename_this_folder)) },
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

/**
 * The shelves as chips, then one "Folders ▾" chip: folders are the user's and unbounded, so they
 * live in its menu (a check on the open one, "New folder" last) instead of widening the row. The
 * chip names the open folder while one is shown. The chips keep Material's 32 dp look; the
 * 48 dp touch target around each is the chip's own (`minimumInteractiveComponentSize`).
 */
@Composable
private fun PresetFilterRow(
    filters: List<PresetFilter>,
    selected: PresetFilter,
    onSelect: (PresetFilter) -> Unit,
    onRenameFolder: (String) -> Unit,
    onNewFolder: () -> Unit,
) {
    val shelves = filters.filterNot { it is PresetFilter.Folder }
    val folders = filters.filterIsInstance<PresetFilter.Folder>()
    LazyRow(
        contentPadding = PaddingValues(horizontal = GeodeSpacing.L),
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        items(shelves, key = ::filterKeyOf) { f ->
            FilterChip(
                selected = f == selected,
                onClick = { onSelect(f) },
                label = { Text(filterLabel(f)) },
            )
        }
        item(key = "folders") {
            FolderFilterChip(
                folders = folders,
                open = (selected as? PresetFilter.Folder)?.name,
                onSelect = onSelect,
                onRenameFolder = onRenameFolder,
                onNewFolder = onNewFolder,
            )
        }
    }
}

@Composable
private fun FolderFilterChip(
    folders: List<PresetFilter.Folder>,
    open: String?,
    onSelect: (PresetFilter) -> Unit,
    onRenameFolder: (String) -> Unit,
    onNewFolder: () -> Unit,
) {
    var menuOpen by remember { mutableStateOf(false) }
    Box {
        FilterChip(
            selected = open != null,
            onClick = { menuOpen = true },
            label = { Text(open ?: stringResource(R.string.presets_folders), maxLines = 1, overflow = TextOverflow.Ellipsis) },
            leadingIcon = { Icon(Icons.Filled.Folder, contentDescription = null, modifier = Modifier.size(18.dp)) },
            trailingIcon = { Icon(Icons.Filled.ArrowDropDown, contentDescription = null, modifier = Modifier.size(18.dp)) },
        )
        DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
            folders.forEach { f ->
                DropdownMenuItem(
                    text = { Text(f.name, maxLines = 1, overflow = TextOverflow.Ellipsis) },
                    leadingIcon = {
                        Icon(if (f.name == open) Icons.Filled.Check else Icons.Filled.Folder, contentDescription = null)
                    },
                    onClick = {
                        menuOpen = false
                        onSelect(f)
                    },
                    modifier =
                        Modifier.semantics {
                            customActions =
                                listOf(
                                    CustomAccessibilityAction("Rename folder") {
                                        menuOpen = false
                                        onRenameFolder(f.name)
                                        true
                                    },
                                )
                        },
                )
            }
            DropdownMenuItem(
                text = { Text(stringResource(R.string.presets_new_folder)) },
                leadingIcon = { Icon(Icons.Filled.CreateNewFolder, contentDescription = null) },
                onClick = {
                    menuOpen = false
                    onNewFolder()
                },
            )
        }
    }
}

/**
 * One preset: its name over its style, then what kind it is (built-in or the user's own), a ♛
 * when its style is Pro, a mark when it is in the visual playlist and a check when it is the look
 * on the stage. The applied row is lit as the chosen style is in Styles (raised glass, accent
 * name) with the look's one glow behind it. The text starts 16 dp in, with the toolbar and chips.
 *
 * A tap applies; a long press, or TalkBack's "Preset actions", opens the sheet. The marks are
 * decorative: the row's state description says applied and in the playlist.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
internal fun PresetRow(
    entry: PresetEntry,
    applied: Boolean,
    locked: Boolean,
    inPlaylist: Boolean,
    onTap: () -> Unit,
    onMenu: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val view = LocalView.current
    val cs = MaterialTheme.colorScheme
    val shape = RoundedCornerShape(GeodeRadius.Medium)
    val actionsLabel = stringResource(R.string.presets_actions)
    val appliedText = stringResource(R.string.presets_state_applied)
    val playlistText = stringResource(R.string.presets_state_in_playlist)
    val state = listOfNotNull(appliedText.takeIf { applied }, playlistText.takeIf { inPlaylist })
    Row(
        modifier
            .fillMaxWidth()
            .padding(horizontal = GeodeSpacing.S)
            .selectedGlow(applied, shape)
            .clip(shape)
            .then(if (applied) Modifier.background(Color.White.copy(alpha = GeodeGlass.FILL_RAISED)) else Modifier)
            .combinedClickable(
                role = Role.Button,
                onClick = onTap,
                onClickLabel = stringResource(R.string.presets_apply),
                onLongClick = {
                    view.performStoneHaptic(StoneHapticCue.CONFIRM)
                    onMenu()
                },
                onLongClickLabel = actionsLabel,
            ).semantics {
                if (state.isNotEmpty()) stateDescription = state.joinToString()
                customActions =
                    listOf(
                        CustomAccessibilityAction(actionsLabel) {
                            onMenu()
                            true
                        },
                    )
            }.heightIn(min = PRESET_ROW_HEIGHT)
            .padding(horizontal = GeodeSpacing.S, vertical = GeodeSpacing.XS),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        Column(Modifier.weight(1f)) {
            Text(
                entry.name,
                style = MaterialTheme.typography.bodyMedium,
                color = if (applied) accentTextColor() else cs.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                sceneDisplayLabel(entry.sceneId),
                style = MaterialTheme.typography.bodySmall,
                color = cs.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        Text(
            stringResource(if (entry.builtIn) R.string.presets_kind_built_in else R.string.presets_kind_mine),
            style = MaterialTheme.typography.labelSmall,
            color = cs.onSurfaceVariant,
            maxLines = 1,
        )
        // The app's one ♛ (shared with Styles and Customize), so Pro reads the same on every page.
        if (locked) ProCrown()
        if (inPlaylist) {
            Icon(
                Icons.AutoMirrored.Filled.PlaylistPlay,
                contentDescription = null,
                tint = cs.onSurfaceVariant,
                modifier = Modifier.size(ROW_MARK),
            )
        }
        if (applied) StoneIconArt(StoneIcon.CHECK, contentDescription = null, modifier = Modifier.size(ROW_MARK), tint = accentTextColor())
    }
}

@Composable
private fun EmptyPresets(filter: PresetFilter) {
    val text =
        when (filter) {
            PresetFilter.All, PresetFilter.Mine ->
                stringResource(R.string.presets_empty_mine)
            PresetFilter.ThisStyle -> stringResource(R.string.presets_empty_style)
            PresetFilter.BuiltIn -> stringResource(R.string.presets_empty_built_in)
            is PresetFilter.Folder -> stringResource(R.string.presets_empty_folder)
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

@Composable
private fun filterLabel(filter: PresetFilter): String =
    when (filter) {
        PresetFilter.All -> stringResource(R.string.presets_filter_all)
        PresetFilter.ThisStyle -> stringResource(R.string.presets_filter_style)
        PresetFilter.Mine -> stringResource(R.string.presets_filter_mine)
        PresetFilter.BuiltIn -> stringResource(R.string.presets_filter_built_in)
        is PresetFilter.Folder -> filter.name
    }

private const val FILTER_ALL = "all"

/** One column in a phone's sheet (up to ~400 dp of room), two or more where the options are wide. */
private val PRESET_COLUMN_MIN = 280.dp

/** Two lines of text in more than a 48 dp target. */
private val PRESET_ROW_HEIGHT = 56.dp
private val ROW_MARK = 18.dp
