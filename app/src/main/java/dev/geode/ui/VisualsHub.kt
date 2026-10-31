package dev.geode.ui

import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.Orientation
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.draggable
import androidx.compose.foundation.gestures.rememberDraggableState
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Casino
import androidx.compose.material.icons.filled.FiberManualRecord
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material.icons.filled.StopCircle
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.Saver
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshots.SnapshotStateList
import androidx.compose.runtime.toMutableStateList
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation3.runtime.NavEntry
import androidx.navigation3.ui.NavDisplay
import dev.geode.R
import dev.geode.data.MacroStore
import dev.geode.data.Preset
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.render.VisualizerView
import dev.geode.render.scene.BuildStyleReactionsUseCase
import dev.geode.render.scene.CustomizeTab
import dev.geode.render.scene.SceneCapabilities
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.StyleFamilies
import dev.geode.render.scene.VisualStyleCatalog
import dev.geode.ui.pro.ProBadge
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlin.math.abs

/**
 * The pages of the Visuals panel, in tab order. [STYLES] is always the root of the back stack and
 * every other page sits on it, so system back steps toward the styles before it leaves Visuals.
 * [TEXTURES] has no tab: MilkDrop opens it, since textures only matter to MilkDrop presets.
 */
private enum class VisualsPage(
    val title: String,
) {
    STYLES("Styles"),
    CUSTOMIZE("Customize"),
    PRESETS("Presets"),
    TEXTURES("Textures"),
}

private val VisualsTabs = listOf(VisualsPage.STYLES, VisualsPage.CUSTOMIZE, VisualsPage.PRESETS)

/** Saved by name, like the shell's destination, so reordering the pages cannot restore the wrong one. */
private val VisualsBackStackSaver =
    listSaver<SnapshotStateList<VisualsPage>, String>(
        save = { stack -> stack.map { it.name } },
        restore = { names ->
            val pages = names.mapNotNull { n -> VisualsPage.entries.firstOrNull { it.name == n } }
            // Styles is always the root; a stack saved by an older build without it starts over there.
            (if (pages.firstOrNull() == VisualsPage.STYLES) pages else listOf(VisualsPage.STYLES)).toMutableStateList()
        },
    )

/**
 * Visuals is a stage: the live canvas on top, where every pick and slider shows as it happens,
 * and a panel under it whose pages (Styles first, then Customize and Presets) are a Navigation 3
 * back stack. It used to open on a form of preset text fields with no visuals on screen at all;
 * the art was one "View live" away and Customize had its own black preview strip.
 *
 * The stage takes gestures: a horizontal swipe steps to the next or previous style the user may
 * use, and a hold hides the panel so the stage fills the screen (another hold brings it back).
 * TalkBack gets both as custom actions. The handle between stage and panel trades one for the
 * other. No per-entry ViewModel decorator: every ViewModel here stays in the Activity's store,
 * which `PlayerSessionProvider` depends on (ADR 0001 § Consequences).
 */
@Composable
fun VisualsHub(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    onOpenNowPlaying: () -> Unit,
    onOpenStudioRender: () -> Unit,
    canvasFree: Boolean = false,
) {
    val studioViewModel: StudioViewModel = geodeViewModel()
    val takes by studioViewModel.takeState.collectAsStateWithLifecycle()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val gate by viewModel.gate.collectAsStateWithLifecycle()
    val backStack = rememberSaveable(saver = VisualsBackStackSaver) { mutableStateListOf(VisualsPage.STYLES) }
    val openTab: (VisualsPage) -> Unit = { page ->
        while (backStack.size > 1) backStack.removeAt(backStack.lastIndex)
        if (page != VisualsPage.STYLES) backStack.add(page)
    }
    var panelShown by rememberSaveable { mutableStateOf(true) }
    // Out here, not per page, so the size the user dragged to holds across every page.
    var stageHeightDp by rememberSaveable { mutableFloatStateOf(STAGE_DEFAULT_DP) }
    val stepStyle: (Int) -> Unit = { step ->
        nextStageStyle(stageStyleOrder(), viz.sceneId, step) { !gate.allowsStyle(it) }?.let(viewModel::selectScene)
    }
    // Hiding the panel is a view state, not a page: back brings the panel back before anything else.
    BackHandler(enabled = !panelShown) { panelShown = true }
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val maxStageDp = (maxHeight * STAGE_MAX_FRACTION).value
        // A short window (split screen, a phone on its side) can have less room than the floor.
        val minStageDp = minOf(STAGE_MIN_DP, maxStageDp)
        Column(Modifier.fillMaxSize()) {
            val stageSize =
                if (panelShown) {
                    Modifier.fillMaxWidth().height(stageHeightDp.coerceIn(minStageDp, maxStageDp).dp)
                } else {
                    Modifier.fillMaxWidth().weight(1f)
                }
            Box(stageSize) {
                // The one VisualizerView has one parent: while the fullscreen view or a second
                // screen holds it, the stage shows the shell's background instead.
                if (canvasFree) VisualizerCanvasHost(visualizerView, Modifier.fillMaxSize())
                Box(
                    Modifier
                        .fillMaxSize()
                        .stageGestures(onHold = { panelShown = !panelShown }, onSwipe = stepStyle),
                )
                StageHeader(
                    styleName = sceneDisplayLabel(viz.sceneId),
                    recordingLabel =
                        when {
                            takes.recording -> "● Recording  ${formatTakeTime(takes.recordedMs)}"
                            takes.replaying != null -> "▶ ${takes.replaying}"
                            else -> null
                        },
                    recording = takes.recording,
                    onRecord = { if (takes.recording) studioViewModel.stopRecording() else studioViewModel.startRecording() },
                    onOpenStudioRender = onOpenStudioRender,
                    onOpenNowPlaying = onOpenNowPlaying,
                )
                Text(
                    stringResource(if (panelShown) R.string.visuals_stage_hint else R.string.visuals_stage_hint_hidden),
                    Modifier.align(Alignment.BottomCenter).padding(bottom = 6.dp),
                    style =
                        MaterialTheme.typography.labelSmall.copy(
                            shadow = Shadow(color = MaterialTheme.colorScheme.scrim, blurRadius = 8f),
                        ),
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.8f),
                )
            }
            if (panelShown) {
                PreviewResizeHandle(onResize = { stageHeightDp = (stageHeightDp + it).coerceIn(minStageDp, maxStageDp) })
                val top = backStack.last()
                CrystalTabs(
                    titles = VisualsTabs.map { it.title },
                    // Textures belongs to Styles (MilkDrop opens it), so Styles stays lit there.
                    selected = VisualsTabs.indexOf(top).takeIf { it >= 0 } ?: 0,
                    onSelect = { openTab(VisualsTabs[it]) },
                )
                NavDisplay(
                    backStack = backStack,
                    onBack = { backStack.removeLastOrNull() },
                    modifier = Modifier.weight(1f),
                    entryProvider = { page ->
                        NavEntry(page) {
                            when (page) {
                                VisualsPage.STYLES ->
                                    StylesTab(viewModel, visualizerView, onOpenTextures = { backStack.add(VisualsPage.TEXTURES) })
                                VisualsPage.CUSTOMIZE -> CustomizePanel(viewModel, visualizerView)
                                VisualsPage.PRESETS -> PresetsTreeTab(viewModel, visualizerView)
                                VisualsPage.TEXTURES -> TexturesHubTab(viewModel, visualizerView)
                            }
                        }
                    },
                )
            }
        }
    }
}

/** The stage's top row: what is on screen, and the record, render and fullscreen actions. */
@Composable
private fun StageHeader(
    styleName: String,
    recordingLabel: String?,
    recording: Boolean,
    onRecord: () -> Unit,
    onOpenStudioRender: () -> Unit,
    onOpenNowPlaying: () -> Unit,
) {
    val scrim = MaterialTheme.colorScheme.scrim
    Row(
        Modifier
            .fillMaxWidth()
            .background(Brush.verticalGradient(listOf(scrim.copy(alpha = 0.7f), scrim.copy(alpha = 0f))))
            .padding(start = 16.dp, end = 8.dp, top = 8.dp, bottom = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Column(Modifier.weight(1f)) {
            CrystalOverline(
                recordingLabel ?: stringResource(R.string.nav_visuals),
                color = if (recording) MaterialTheme.colorScheme.error else accentTextColor(),
            )
            Text(styleName, style = MaterialTheme.typography.titleLarge, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        IconButton(onClick = onRecord) {
            Icon(
                if (recording) Icons.Filled.StopCircle else Icons.Filled.FiberManualRecord,
                if (recording) "Stop recording this take" else "Record a take",
                tint = if (recording) MaterialTheme.colorScheme.error else LocalContentColor.current,
            )
        }
        // Export, loop render and the takes list live in Studio; this only takes you there, even
        // when the Studio tab is hidden from the bar for someone who came to listen.
        IconButton(onClick = onOpenStudioRender) {
            StoneIconArt(StoneIcon.STUDIO, stringResource(R.string.visuals_open_render))
        }
        CrystalButton(compact = true, filled = false, onClick = onOpenNowPlaying) { Text("View live") }
    }
}

/**
 * A hold toggles the panel; a horizontal swipe past [STAGE_SWIPE_DP] steps the style (left is
 * next, like turning a page). The callbacks go through [rememberUpdatedState] because
 * `pointerInput(Unit)` keeps its first lambda for as long as the stage is on screen.
 */
@Composable
private fun Modifier.stageGestures(
    onHold: () -> Unit,
    onSwipe: (Int) -> Unit,
): Modifier {
    val hold by rememberUpdatedState(onHold)
    val swipe by rememberUpdatedState(onSwipe)
    val next = stringResource(R.string.a11y_next_style)
    val previous = stringResource(R.string.a11y_previous_style)
    val toggle = stringResource(R.string.a11y_toggle_visuals_panel)
    return this
        .pointerInput(Unit) { detectTapGestures(onLongPress = { hold() }) }
        .pointerInput(Unit) {
            var dx = 0f
            detectHorizontalDragGestures(
                onDragStart = { dx = 0f },
                onDragEnd = { if (abs(dx) > STAGE_SWIPE_DP.dp.toPx()) swipe(if (dx < 0) 1 else -1) },
            ) { _, delta -> dx += delta }
        }.semantics {
            customActions =
                listOf(
                    CustomAccessibilityAction(next) {
                        swipe(1)
                        true
                    },
                    CustomAccessibilityAction(previous) {
                        swipe(-1)
                        true
                    },
                    CustomAccessibilityAction(toggle) {
                        hold()
                        true
                    },
                )
        }
}

/**
 * Every style the stage swipes through, in the order the Styles page lists them. MilkDrop is left
 * out: its presets are a library of their own, picked from the MilkDrop page.
 */
internal fun stageStyleOrder(): List<String> =
    (
        StyleFamilies.ids +
            VisualStyleCatalog.silkIds + VisualStyleCatalog.lifeIds + VisualStyleCatalog.mycoIds + VisualStyleCatalog.acidIds +
            SceneCapabilities.SHADER_SCENES.keys.filterNot { it in StyleFamilies.ids } +
            VisualStyleCatalog.fluidIds + listOf(SceneIds.CURLFLOW, SceneIds.WATER) + VisualStyleCatalog.cymaticsIds
    ).distinct()

/**
 * The style [step] places (+1 next, -1 previous) from [current] in [order], wrapping at the ends
 * and skipping [locked] ones: a swipe that opened the paywall would stop the browse dead. A style
 * outside [order] (MilkDrop) starts from the first. Null when nothing else is open to the user.
 */
internal fun nextStageStyle(
    order: List<String>,
    current: String,
    step: Int,
    locked: (String) -> Boolean,
): String? {
    if (order.isEmpty()) return null
    val here = order.indexOf(current)
    val start =
        when {
            here >= 0 -> here
            step > 0 -> -1
            else -> 0
        }
    for (i in 1..order.size) {
        val id = order[Math.floorMod(start + step * i, order.size)]
        if (id != current && !locked(id)) return id
    }
    return null
}

internal fun vizPlaylistIndexOf(
    playlist: List<VizPlaylistEntry>,
    presetName: String,
): Int = playlist.indexOfFirst { it.presetName == presetName }

internal fun presetReplaceTarget(
    rawName: String,
    presets: List<Preset>,
): String? {
    val name = rawName.replace(" · ", " - ").trim().ifEmpty { "Preset" }
    return presets.firstOrNull { !BuiltInPresets.isBuiltIn(it.name) && it.name == name }?.name
}

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

@Composable
private fun PresetsTreeTab(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val presetFolders by visualsViewModel.presetFolders.collectAsStateWithLifecycle()
    val folders = presetFolders.folders
    var newFolder by remember { mutableStateOf("") }
    var saveName by remember { mutableStateOf("") }
    var saveFolder by rememberSaveable { mutableStateOf("") }
    var renamingFolder by remember { mutableStateOf<String?>(null) }
    var folderRenameText by remember { mutableStateOf("") }
    var movingPreset by remember { mutableStateOf<String?>(null) }
    var deletingPreset by remember { mutableStateOf<String?>(null) }
    var replacingPreset by remember { mutableStateOf<String?>(null) }
    var showTemplates by remember { mutableStateOf(false) }
    var showBackground by remember { mutableStateOf(false) }
    val userPresets = viz.presets.filterNot { BuiltInPresets.isBuiltIn(it.name) }.distinctBy { it.name }
    val byFolder = userPresets.groupBy { presetFolders.folderOf(it.name) }
    val context = androidx.compose.ui.platform.LocalContext.current
    var importNote by remember { mutableStateOf<String?>(null) }
    val presetFilePicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) {
                visualsViewModel.importPresetFile(uri) { name ->
                    importNote = name?.let { "Imported \"$it\"." } ?: "That file is not a Geode preset."
                }
            }
        }

    LazyColumn(Modifier.fillMaxSize().padding(horizontal = 16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        item {
            // Wraps instead of squeezing: four labels never fit one phone-width row, and a Row
            // crushed the third into one letter per line and pushed the fourth off screen.
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.padding(bottom = 6.dp),
            ) {
                CrystalButton(compact = true, filled = false, onClick = {
                    val pasted = clipboardText(context)
                    importNote =
                        when {
                            pasted.isNullOrBlank() -> "The clipboard is empty."
                            else ->
                                visualsViewModel.importPresetLink(pasted)?.let { "Imported \"$it\"." }
                                    ?: "That clipboard text is not a Geode preset link."
                        }
                }) { Text("Paste a shared preset") }
                CrystalButton(compact = true, filled = false, onClick = {
                    presetFilePicker.launch(arrayOf("*/*"))
                }) { Text("Open a preset file") }
                CrystalButton(compact = true, filled = false, onClick = { showTemplates = true }) {
                    Text(stringResource(R.string.template_entry_point))
                }
                CrystalButton(compact = true, filled = false, onClick = { showBackground = true }) {
                    Text(stringResource(R.string.background_entry_point))
                }
            }
            importNote?.let { note ->
                Text(
                    note,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(bottom = 6.dp),
                )
            }
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(
                    value = newFolder,
                    onValueChange = { newFolder = it },
                    modifier = Modifier.weight(1f),
                    placeholder = { Text("New folder name") },
                    singleLine = true,
                )
                CrystalButton(onClick = {
                    if (newFolder.isNotBlank()) {
                        visualsViewModel.addPresetFolder(newFolder.trim())
                        newFolder = ""
                    }
                }) { Text("Add") }
            }
        }
        (listOf("") + folders).forEach { folder ->
            val inFolder = byFolder[folder].orEmpty()
            if (folder.isNotEmpty() || inFolder.isNotEmpty()) {
                item(key = "hdr_$folder") {
                    Row(
                        Modifier.fillMaxWidth().padding(top = 8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            if (folder.isEmpty()) "Presets" else "📁 $folder",
                            style = MaterialTheme.typography.titleMedium,
                            color = accentTextColor(),
                            modifier = Modifier.weight(1f),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                        if (folder.isNotEmpty()) {
                            IconButton(onClick = {
                                renamingFolder = folder
                                folderRenameText = folder
                            }) { StoneIconArt(StoneIcon.EDIT, "Rename this folder") }
                        }
                    }
                }
            }
            items(inFolder, key = { "p_${it.name}" }) { p ->
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text(p.name, Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
                    IconButton(onClick = { applyPresetLive(viewModel, visualizerView, p) }) {
                        StoneIconArt(StoneIcon.PLAY, "Apply", tint = MaterialTheme.colorScheme.primary)
                    }
                    IconButton(onClick = { sharePreset(context, visualsViewModel, p.name) }) {
                        StoneIconArt(StoneIcon.SHARE, "Share this preset")
                    }
                    val playlistIndex = vizPlaylistIndexOf(viz.vizPlaylist, p.name)
                    val inPlaylist = playlistIndex >= 0
                    IconButton(
                        onClick = {
                            if (inPlaylist) {
                                viewModel.removeVizPlaylistAt(playlistIndex)
                            } else {
                                viewModel.addToVizPlaylist(
                                    VizPlaylistEntry(sceneId = p.sceneId, presetName = p.name, label = p.name),
                                )
                            }
                        },
                    ) {
                        StoneIconArt(
                            StoneIcon.FAVORITE,
                            if (inPlaylist) "Remove from visual playlist" else "Add to visual playlist",
                            tint = if (inPlaylist) MaterialTheme.colorScheme.primary else LocalContentColor.current,
                        )
                    }
                    IconButton(onClick = { movingPreset = p.name }) {
                        StoneIconArt(StoneIcon.FOLDER, "Move to another folder")
                    }
                    IconButton(onClick = { deletingPreset = p.name }) {
                        StoneIconArt(StoneIcon.DELETE, "Remove", tint = MaterialTheme.colorScheme.error)
                    }
                }
            }
        }
        item {
            Text(
                "Built-in",
                style = MaterialTheme.typography.titleMedium,
                color = accentTextColor(),
                modifier = Modifier.padding(top = 8.dp),
            )
        }
        items(
            viz.presets.filter { BuiltInPresets.isBuiltIn(it.name) && builtInPresetMatchesScene(it.sceneId, viz.sceneId) },
            key = { "b_${it.name}" },
        ) { p ->
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text(p.name, Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
                IconButton(onClick = { applyPresetLive(viewModel, visualizerView, p) }) {
                    StoneIconArt(StoneIcon.PLAY, "Apply", tint = MaterialTheme.colorScheme.primary)
                }
            }
        }
        item {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.padding(vertical = 10.dp),
            ) {
                OutlinedTextField(
                    value = saveName,
                    onValueChange = { saveName = it },
                    modifier = Modifier.weight(1f),
                    placeholder = { Text("Save current as…") },
                    singleLine = true,
                )
                CrystalButton(onClick = {
                    if (saveName.isNotBlank()) {
                        val existing = presetReplaceTarget(saveName, viz.presets)
                        if (existing != null) {
                            replacingPreset = existing
                        } else {
                            visualsViewModel.savePreset(
                                saveName.trim(),
                                visualizerView.visualizerRenderer.customShaderFor(viewModel.vizState.value.sceneId),
                                saveFolder,
                            )
                            saveName = ""
                        }
                    }
                }) { Text("Save") }
            }
            if (folders.isNotEmpty()) {
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp), modifier = Modifier.padding(bottom = 12.dp)) {
                    (listOf("") + folders).forEach { f ->
                        CrystalButton(compact = true, filled = saveFolder == f, onClick = { saveFolder = f }) {
                            Text(f.ifEmpty { "root" }, style = MaterialTheme.typography.bodySmall)
                        }
                    }
                }
            }
        }
    }
    renamingFolder?.let { old ->
        val proposed = folderRenameText.trim()
        val collides = !proposed.equals(old, ignoreCase = true) && folders.any { it.equals(proposed, ignoreCase = true) }
        androidx.compose.material3.AlertDialog(
            onDismissRequest = { renamingFolder = null },
            title = { Text("Rename folder") },
            text = {
                Column {
                    OutlinedTextField(
                        value = folderRenameText,
                        onValueChange = { folderRenameText = it },
                        singleLine = true,
                    )
                    if (collides) {
                        Text(
                            "There is already a folder called \"$proposed\".",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.error,
                        )
                    }
                }
            },
            confirmButton = {
                CrystalButton(enabled = proposed.isNotBlank() && !collides, onClick = {
                    visualsViewModel.renamePresetFolder(old, proposed)
                    if (saveFolder == old) saveFolder = proposed
                    renamingFolder = null
                }) { Text("Rename") }
            },
            dismissButton = { TextButton(onClick = { renamingFolder = null }) { Text("Cancel") } },
        )
    }
    movingPreset?.let { name ->
        val current = presetFolders.folderOf(name)
        androidx.compose.material3.AlertDialog(
            onDismissRequest = { movingPreset = null },
            title = { Text("Move \"$name\"") },
            text = {
                androidx.compose.foundation.layout.FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalArrangement = Arrangement.spacedBy(2.dp),
                ) {
                    (listOf("") + folders).forEach { f ->
                        CrystalButton(compact = true, filled = f == current, onClick = {
                            visualsViewModel.movePresetToFolder(name, f)
                            movingPreset = null
                        }) { Text(f.ifEmpty { "root" }, style = MaterialTheme.typography.bodySmall) }
                    }
                }
            },
            confirmButton = { TextButton(onClick = { movingPreset = null }) { Text("Close") } },
        )
    }
    replacingPreset?.let { name ->
        androidx.compose.material3.AlertDialog(
            onDismissRequest = { replacingPreset = null },
            title = { Text("Replace \"$name\"?") },
            text = {
                Text(
                    "A preset with this name already exists. Saving replaces its look " +
                        "for good — there is no undo. Share it first if you might want it back.",
                )
            },
            confirmButton = {
                CrystalButton(onClick = {
                    visualsViewModel.savePreset(
                        saveName.trim(),
                        visualizerView.visualizerRenderer.customShaderFor(viewModel.vizState.value.sceneId),
                        saveFolder,
                    )
                    saveName = ""
                    replacingPreset = null
                }) { Text("Replace") }
            },
            dismissButton = { TextButton(onClick = { replacingPreset = null }) { Text("Cancel") } },
        )
    }
    deletingPreset?.let { name ->
        androidx.compose.material3.AlertDialog(
            onDismissRequest = { deletingPreset = null },
            title = { Text("Delete \"$name\"?") },
            text = {
                Text(
                    "Removes this preset and its file for good — there is no undo. " +
                        "Share it first if you might want it back.",
                )
            },
            confirmButton = {
                CrystalButton(onClick = {
                    visualsViewModel.deletePreset(name)
                    deletingPreset = null
                }) { Text("Delete") }
            },
            dismissButton = {
                TextButton(onClick = { deletingPreset = null }) { Text("Cancel") }
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

private fun sharePreset(
    context: android.content.Context,
    viewModel: VisualsViewModel,
    name: String,
) {
    val link = viewModel.presetShareLink(name)
    val send =
        if (link != null) {
            android.content.Intent(android.content.Intent.ACTION_SEND).apply {
                type = "text/plain"
                putExtra(android.content.Intent.EXTRA_SUBJECT, "MuzViz preset: $name")
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

private fun applyPresetLive(
    viewModel: PlayerViewModel,
    @Suppress("UNUSED_PARAMETER") visualizerView: VisualizerView,
    p: Preset,
) {
    viewModel.applyPreset(p)
}

@Composable
private fun StylesTab(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    onOpenTextures: () -> Unit,
) {
    var sub by rememberSaveable { mutableStateOf(0) }
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val pickScene: (String) -> Unit = { viewModel.selectScene(it) }
    val gate by viewModel.gate.collectAsStateWithLifecycle()
    val locked: (String) -> Boolean = { !gate.allowsStyle(it) }
    Column(Modifier.fillMaxSize()) {
        CrystalTabs(
            titles = listOf("Families", "Silk", "Life", "Mycelium", "Acid", "Shaders", "Fluid", "Cymatics", "MilkDrop"),
            selected = sub,
            onSelect = { sub = it },
        )
        when (sub) {
            0 -> FamilyList(viz.sceneId, locked, pickScene)
            1 -> SceneList(VisualStyleCatalog.silkIds, viz.sceneId, locked, pickScene)
            2 -> SceneList(VisualStyleCatalog.lifeIds, viz.sceneId, locked, pickScene)
            3 -> SceneList(VisualStyleCatalog.mycoIds, viz.sceneId, locked, pickScene)
            4 -> SceneList(VisualStyleCatalog.acidIds, viz.sceneId, locked, pickScene)
            5 -> SceneList(SceneCapabilities.SHADER_SCENES.keys.filterNot { it in StyleFamilies.ids }, viz.sceneId, locked, pickScene)
            6 -> SceneList(VisualStyleCatalog.fluidIds + listOf(SceneIds.CURLFLOW, SceneIds.WATER), viz.sceneId, locked, pickScene)
            7 -> SceneList(VisualStyleCatalog.cymaticsIds, viz.sceneId, locked, pickScene)
            8 -> MilkDropTab(viewModel, visualizerView, onOpenTextures)
        }
    }
}

internal fun sceneDisplayLabel(id: String): String {
    val catalogued = VisualStyleCatalog.label(id)
    if (catalogued != id) return catalogued
    return id.split('_').joinToString(" ") { word -> word.replaceFirstChar { it.uppercase() } }
}

@Composable
private fun SceneList(
    ids: List<String>,
    current: String,
    locked: (String) -> Boolean,
    onPick: (String) -> Unit,
) {
    LazyColumn(Modifier.fillMaxSize()) {
        items(ids) { id ->
            val sel = id == current
            Row(
                Modifier.fillMaxWidth().clickable { onPick(id) }.padding(horizontal = 16.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(Modifier.size(6.dp), contentAlignment = Alignment.Center) {
                    if (sel) CrystalGem(MaterialTheme.colorScheme.primary, size = 6.dp)
                }
                Text(
                    sceneDisplayLabel(id),
                    Modifier.padding(start = 10.dp).weight(1f),
                    color = if (sel) accentTextColor() else LocalContentColor.current,
                )
                // Says up front what a tap would otherwise discover through the paywall.
                if (locked(id)) ProBadge()
            }
        }
    }
}

/**
 * The style families, each under its own heading, in family order. Every family's styles in one flat
 * list would bury the family a style belongs to, and the family is what tells a free user why one
 * style in twelve is theirs and the other eleven carry a Pro badge.
 */
@Composable
private fun FamilyList(
    current: String,
    locked: (String) -> Boolean,
    onPick: (String) -> Unit,
) {
    LazyColumn(Modifier.fillMaxSize()) {
        StyleFamilies.all.forEach { family ->
            item(key = "family_${family.name}") {
                Text(
                    family.label,
                    Modifier.padding(start = 16.dp, top = 14.dp, bottom = 2.dp),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            items(family.ids, key = { it }) { id ->
                val sel = id == current
                Row(
                    Modifier.fillMaxWidth().clickable { onPick(id) }.padding(horizontal = 16.dp, vertical = 10.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Box(Modifier.size(6.dp), contentAlignment = Alignment.Center) {
                        if (sel) CrystalGem(MaterialTheme.colorScheme.primary, size = 6.dp)
                    }
                    Text(
                        sceneDisplayLabel(id),
                        Modifier.padding(start = 10.dp).weight(1f),
                        color = if (sel) accentTextColor() else LocalContentColor.current,
                    )
                    if (locked(id)) ProBadge()
                }
            }
        }
    }
}

@Composable
private fun MilkDropTab(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    onOpenTextures: () -> Unit,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    var refresh by remember { mutableStateOf(0) }
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    // Off the main thread: userMilkPresets() runs MilkStarterPack.install() - a marker read, an
    // assets.list, an exists() per asset and up to six asset->file copies on first run - then
    // listFiles() plus a lastModified() stat per preset for the sort. Inside composition that
    // measured 45-53 ms of main-thread disk I/O on every launch. Same key as before, so the
    // refresh++ below still relists; the list simply arrives a frame later instead of stalling one.
    val milkFiles by produceState(emptyList<java.io.File>(), refresh) {
        value = withContext(Dispatchers.IO) { visualsViewModel.userMilkPresets() }
    }
    val loaded by viewModel.activeMilkPath.collectAsStateWithLifecycle()
    var packReport by remember { mutableStateOf<dev.geode.data.MilkPackImporter.Report?>(null) }
    val importedTextures by visualsViewModel.textures.collectAsStateWithLifecycle()
    var linkRefresh by remember { mutableStateOf(0) }
    // What the linker decided for the preset on screen, kept live so a texture import or a
    // manual choice is visible without leaving the tab.
    // Also disk-bound: resolutionFor() stats the preset and its .links/<stem> record, so it belongs
    // off the main thread for the same reason as milkFiles above. Keys are unchanged.
    val textureLinks by produceState(
        emptyList<dev.geode.data.MilkTextureLink>(),
        loaded,
        refresh,
        linkRefresh,
        importedTextures,
    ) {
        value =
            withContext(Dispatchers.IO) {
                loaded?.let { runCatching { viewModel.milkTextureLinksFor(it) }.getOrDefault(emptyList()) }.orEmpty()
            }
    }
    var pickTextureFor by remember { mutableStateOf<String?>(null) }
    val milkFolderPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
            if (uri != null) {
                viewModel.importMilkFolderAsync(uri) { report ->
                    packReport = report
                    refresh++
                }
            }
        }
    val milkPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) {
                viewModel.importMilkPresetAsync(uri) { path ->
                    if (path != null) {
                        selectMilk(viewModel, visualizerView, path)
                        refresh++
                    }
                }
            }
        }
    if (!visualizerView.visualizerRenderer.milkdropAvailable) {
        Text("MilkDrop engine unavailable on this device.", Modifier.padding(16.dp))
        return
    }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            CrystalButton(onClick = { milkPicker.launch(arrayOf("*/*")) }) { Text("Load .milk file") }
            CrystalButton(filled = false, onClick = { milkFolderPicker.launch(null) }) { Text("Import folder…") }
            CrystalButton(filled = false, onClick = onOpenTextures) { Text("Textures…") }
        }
        packReport?.let { r ->
            Text(
                buildString {
                    append("Imported ${r.presets} presets and ${r.textures} textures")
                    if (r.skipped > 0) append(", ${r.skipped} skipped (already present or unreadable)")
                    append('.')
                    if (r.presetsMissingTextures > 0) {
                        append(
                            " ${r.presetsMissingTextures} wanted textures you don't have and were " +
                                "linked to stand-ins - load one to review or replace them.",
                        )
                    }
                },
                style = MaterialTheme.typography.labelSmall,
                color =
                    if (r.presetsMissingTextures > 0) {
                        MaterialTheme.colorScheme.error
                    } else {
                        MaterialTheme.colorScheme.onSurfaceVariant
                    },
            )
        }
        if (viz.sceneId == SceneIds.MILKDROP) {
            viz.shaderError?.let {
                Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.labelSmall)
            }
            MilkTextureLinkPanel(
                links = textureLinks,
                onPick = { expected -> pickTextureFor = expected },
            )
            val pickingFor = pickTextureFor
            val loadedPath = loaded
            if (pickingFor != null && loadedPath != null) {
                MilkTexturePickerDialog(
                    expected = pickingFor,
                    textures = importedTextures,
                    onChoose = { chosen ->
                        viewModel.assignMilkTextureAsync(loadedPath, pickingFor, chosen) {
                            pickTextureFor = null
                            linkRefresh++
                            // Reload so the new link is what projectM reads - search paths are
                            // re-set on every preset load, so this is the whole refresh.
                            selectMilk(viewModel, visualizerView, loadedPath)
                        }
                    },
                    onDismiss = { pickTextureFor = null },
                )
            }
        }
        Text("Your .milk presets", style = MaterialTheme.typography.titleMedium, color = accentTextColor())
        if (milkFiles.isEmpty()) {
            Text("None yet — load a .milk file or save one from the milkdrop scene.", style = MaterialTheme.typography.bodySmall)
        }
        milkFiles.forEach { f ->
            val active = f.absolutePath == loaded
            Row(
                Modifier
                    .fillMaxWidth()
                    .clickable {
                        selectMilk(viewModel, visualizerView, f.absolutePath)
                        refresh++
                    }.padding(vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(Modifier.size(6.dp), contentAlignment = Alignment.Center) {
                    if (active) CrystalGem(MaterialTheme.colorScheme.primary, size = 6.dp)
                }
                Text(
                    f.nameWithoutExtension,
                    Modifier.padding(start = 10.dp),
                    color = if (active) accentTextColor() else LocalContentColor.current,
                )
            }
        }
    }
}

/**
 * The loaded preset's texture resolution, shown only when there is something to say.
 *
 * Exact matches are silent - a working preset needs no panel. What earns a row is anything the
 * linker had to decide: a rename undone, a stand-in chosen, or a choice the person already made.
 * Every row is tappable, because the row IS the affordance for "load this preset with a
 * different texture than it expects".
 */
@Composable
private fun MilkTextureLinkPanel(
    links: List<dev.geode.data.MilkTextureLink>,
    onPick: (String) -> Unit,
) {
    val interesting = links.filter { it.kind != dev.geode.data.MilkTextureLinkKind.MATCHED }
    if (interesting.isEmpty()) return
    Text("Textures for this preset", style = MaterialTheme.typography.labelMedium, color = accentTextColor())
    interesting.forEach { link ->
        val note =
            when (link.kind) {
                dev.geode.data.MilkTextureLinkKind.MATCHED -> ""
                dev.geode.data.MilkTextureLinkKind.RENAMED -> "linked - import renamed it"
                dev.geode.data.MilkTextureLinkKind.OVERRIDDEN -> "your choice"
                dev.geode.data.MilkTextureLinkKind.SUBSTITUTED -> "stand-in - tap to change"
                dev.geode.data.MilkTextureLinkKind.MISSING -> "missing - import an image via Textures…"
            }
        Row(
            Modifier.fillMaxWidth().clickable { onPick(link.expected) }.padding(vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(link.expected, style = MaterialTheme.typography.labelSmall, modifier = Modifier.weight(1f))
            Text(
                link.texture?.let { "$it  ($note)" } ?: note,
                style = MaterialTheme.typography.labelSmall,
                color =
                    when (link.kind) {
                        dev.geode.data.MilkTextureLinkKind.MISSING -> MaterialTheme.colorScheme.error
                        dev.geode.data.MilkTextureLinkKind.SUBSTITUTED -> MaterialTheme.colorScheme.error
                        else -> MaterialTheme.colorScheme.onSurfaceVariant
                    },
            )
        }
    }
}

@Composable
private fun MilkTexturePickerDialog(
    expected: String,
    textures: List<dev.geode.data.MilkTexture>,
    onChoose: (String?) -> Unit,
    onDismiss: () -> Unit,
) {
    androidx.compose.material3.AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = {},
        dismissButton = { CrystalButton(filled = false, onClick = onDismiss) { Text("Cancel") } },
        title = { Text("Texture for \"$expected\"") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                Text(
                    "The preset asks for \"$expected\". Pick any imported image to load it with - " +
                        "the choice is remembered for this preset.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Spacer(Modifier.height(8.dp))
                textures.forEach { tex ->
                    Text(
                        tex.name,
                        Modifier.fillMaxWidth().clickable { onChoose(tex.name) }.padding(vertical = 8.dp),
                    )
                }
                Text(
                    "Use automatic matching",
                    Modifier.fillMaxWidth().clickable { onChoose(null) }.padding(vertical = 8.dp),
                    color = accentTextColor(),
                )
            }
        },
    )
}

private fun selectMilk(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    path: String,
) {
    viewModel.selectScene(SceneIds.MILKDROP)
    visualizerView.visualizerRenderer.loadMilkPreset(path)
}

@Composable
internal fun CustomizePanel(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    var sub by rememberSaveable { mutableStateOf(0) }
    var query by rememberSaveable { mutableStateOf("") }
    val isShader = SceneCapabilities.hasShaderLook(viz.sceneId)
    val tabs: List<CustomizeTab?> =
        CustomizeTab.entries.filter { tabAppliesTo(it, viz.sceneId) } + if (isShader) listOf(null) else emptyList()
    val titles = tabs.map { it?.title ?: "GLSL" }
    var shownTitle by rememberSaveable { mutableStateOf(titles.getOrNull(sub)) }
    LaunchedEffect(titles) {
        if (titles.getOrNull(sub) != shownTitle) {
            sub = titles.indexOf(shownTitle).coerceAtLeast(0)
            shownTitle = titles[sub]
        }
    }
    Column(Modifier.fillMaxSize()) {
        CrystalTabs(
            titles = titles,
            selected = sub,
            onSelect = {
                sub = it
                shownTitle = titles[it]
            },
        )
        // The toolbar scrolls with the controls: pinned above them it left a phone one row of sliders.
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp)) {
            CustomizeToolbar(viewModel, visualizerView, viz.params, tabs.getOrNull(sub), query) { query = it }
            val locked by visualsViewModel.lockedParams.collectAsStateWithLifecycle()
            androidx.compose.runtime.CompositionLocalProvider(
                LocalParamLocks provides (locked to visualsViewModel::toggleParamLock),
            ) {
                ParamSurface(sceneId = viz.sceneId, query = query) {
                    if (query.isBlank()) {
                        CustomizeTabBody(viewModel, visualizerView, tabs.getOrNull(sub))
                    } else {
                        // Search reaches EVERY tab, not just the one on screen: "where did the
                        // vignette slider go" is exactly the question search exists to answer.
                        tabs.filterNotNull().forEach { tab -> CustomizeTabBody(viewModel, visualizerView, tab) }
                    }
                }
            }
        }
    }
}

/**
 * The grip under the Visuals stage. Drags report deltas rather than a new height, so the
 * owner adds them to its own state: several drag events can land before a recomposition, and a
 * height read from the last composition would drop all but one of them. TalkBack gets the same
 * control as two actions, since a drag is out of its reach.
 */
@Composable
private fun PreviewResizeHandle(onResize: (deltaDp: Float) -> Unit) {
    val density = LocalDensity.current
    val label = stringResource(R.string.a11y_preview_resize)
    val larger = stringResource(R.string.a11y_preview_larger)
    val smaller = stringResource(R.string.a11y_preview_smaller)
    Box(
        Modifier
            .fillMaxWidth()
            .height(PREVIEW_HANDLE_HEIGHT)
            .draggable(
                rememberDraggableState { delta -> onResize(with(density) { delta.toDp() }.value) },
                Orientation.Vertical,
            ).semantics {
                contentDescription = label
                customActions =
                    listOf(
                        CustomAccessibilityAction(larger) {
                            onResize(PREVIEW_STEP_DP)
                            true
                        },
                        CustomAccessibilityAction(smaller) {
                            onResize(-PREVIEW_STEP_DP)
                            true
                        },
                    )
            },
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier
                .size(width = 40.dp, height = 4.dp)
                .background(MaterialTheme.colorScheme.onSurfaceVariant, RoundedCornerShape(2.dp)),
        )
    }
}

private const val STAGE_DEFAULT_DP = 300f

private const val STAGE_MIN_DP = 120f

private const val STAGE_MAX_FRACTION = 0.75f

private const val STAGE_SWIPE_DP = 72f

private const val PREVIEW_STEP_DP = 80f

private val PREVIEW_HANDLE_HEIGHT = 28.dp

/**
 * Whether a tab has anything to show for the style on screen.
 *
 * Events, the five spec tabs and Scene are always there (a recipe with nothing to move on this
 * style hides itself, not the tab); the per-family ones appear only with their family. Fluid stays because it also holds the two "all styles" sections (FlowField and the
 * shared water-ripple overlay), which every style can use.
 */
private fun tabAppliesTo(
    tab: CustomizeTab,
    sceneId: String,
): Boolean =
    when (tab) {
        CustomizeTab.MACROS,
        CustomizeTab.EVENTS,
        CustomizeTab.SHAPE,
        CustomizeTab.COLOR,
        CustomizeTab.MOTION,
        CustomizeTab.CAMERA,
        CustomizeTab.LAYERS,
        CustomizeTab.FX,
        CustomizeTab.REACTIVITY,
        CustomizeTab.SCENE,
        CustomizeTab.FLUID,
        -> true
        CustomizeTab.CYMATICS -> SceneCapabilities.isCymatics(sceneId)
    }

@Composable
private fun CustomizeTabBody(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    tab: CustomizeTab?,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val p = viz.params
    val onChange: (dev.geode.render.scene.SceneParams) -> Unit = visualsViewModel::editSceneParams
    // Every parameter row in every tab is a lane the music plays (ui/MusicLanes): the user's routes,
    // or the style's DNA until the first edit copies it. Editing the music side is Pro.
    val reactions by visualsViewModel.reactions.collectAsStateWithLifecycle()
    val lanePaywall: dev.geode.ui.pro.PaywallViewModel = geodeViewModel()
    val laneGate by lanePaywall.gate.collectAsStateWithLifecycle()
    val catalogue = remember { musicCatalogue() }
    val dna = remember(viz.sceneId, p.reactSeed) { BuildStyleReactionsUseCase()(viz.sceneId, p.reactSeed) }
    val lanes =
        catalogue?.let {
            MusicLanes(
                routes = reactions ?: dna,
                catalogue = it,
                canEdit = laneGate.allows(ProFeature.ADVANCED_CUSTOMIZATION),
                onRoutesChange = visualsViewModel::setReactions,
                onLocked = { lanePaywall.open(PaywallTrigger.Feature(ProFeature.ADVANCED_CUSTOMIZATION)) },
            )
        }
    CompositionLocalProvider(LocalMusicLanes provides lanes) {
        CustomizeTabContent(viewModel, visualizerView, visualsViewModel, tab, p, onChange)
    }
}

@Composable
private fun CustomizeTabContent(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    visualsViewModel: VisualsViewModel,
    tab: CustomizeTab?,
    p: dev.geode.render.scene.SceneParams,
    onChange: (dev.geode.render.scene.SceneParams) -> Unit,
) {
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    when (tab) {
        CustomizeTab.MACROS -> {
            val context = androidx.compose.ui.platform.LocalContext.current
            val store = remember { MacroStore.get(context) }
            val macros by store.state.collectAsStateWithLifecycle()
            val paywall: dev.geode.ui.pro.PaywallViewModel = geodeViewModel()
            val gate by paywall.gate.collectAsStateWithLifecycle()
            MacrosTab(
                macros,
                store,
                canSavePresets = gate.allows(ProFeature.ADVANCED_CUSTOMIZATION),
                onLockedSave = {
                    paywall.open(PaywallTrigger.Feature(ProFeature.ADVANCED_CUSTOMIZATION))
                },
            )
        }
        CustomizeTab.EVENTS -> {
            val reactions by visualsViewModel.reactions.collectAsStateWithLifecycle()
            EventsTab(
                p,
                onChange,
                reactions = reactions,
                onReactionsChange = visualsViewModel::setReactions,
                onRerollReactions = visualsViewModel::rerollReactions,
            )
        }
        CustomizeTab.MOTION -> MotionTab(p, onChange)
        CustomizeTab.CAMERA -> CameraTab(p, onChange)
        CustomizeTab.LAYERS -> LayersTab(p, onChange)
        CustomizeTab.SHAPE -> ShapeTab(p, onChange)
        CustomizeTab.SCENE ->
            SceneTab(
                p,
                onChange,
                transitionId = viz.transitionId,
                transitionDurationSec = viz.transitionDurationSec,
                onTransitionId = viewModel::setTransitionId,
                onTransitionDuration = viewModel::setTransitionDuration,
            )
        CustomizeTab.REACTIVITY -> {
            val lfos by visualsViewModel.lfos.collectAsStateWithLifecycle()
            val adsrs by visualsViewModel.adsrs.collectAsStateWithLifecycle()
            ReactivityTab(
                p,
                onChange,
                attack = viz.attack,
                decay = viz.decay,
                onReactivityChange = viewModel::setReactivity,
                lfos = lfos,
                onLfoChange = visualsViewModel::setLfo,
                adsr = adsrs,
                onAdsrChange = visualsViewModel::setAdsr,
            )
        }
        CustomizeTab.COLOR -> {
            val artNote by viewModel.artPaletteNote.collectAsStateWithLifecycle()
            ColorTab(
                p,
                onChange,
                onTakeArtworkPalette = viewModel::applyArtworkPalette,
                artworkNote = artNote,
            )
        }
        CustomizeTab.FX -> FxTab(p, onChange)
        CustomizeTab.FLUID ->
            FluidTab(
                p,
                onChange,
                injectionError = if (SceneCapabilities.isFluid(viz.sceneId)) viz.shaderError else null,
                onApplyInjectionShaders = { force, dye ->
                    visualizerView.visualizerRenderer.submitFluidInjectionShaders(force, dye)
                },
            )
        CustomizeTab.CYMATICS -> CymaticsTab(p, onChange)
        null -> GlslHubTab(viewModel, visualizerView)
    }
}

@Composable
private fun CustomizeToolbar(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    params: dev.geode.render.scene.SceneParams,
    tab: CustomizeTab?,
    query: String,
    onQuery: (String) -> Unit,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val history by visualsViewModel.paramHistory.collectAsStateWithLifecycle()
    var confirmReset by remember { mutableStateOf(false) }
    var savingPreset by remember { mutableStateOf(false) }
    var presetName by remember { mutableStateOf("") }
    val changed = remember(params) { CustomizeSummary.changedCount(params) }
    val context = androidx.compose.ui.platform.LocalContext.current
    val macroStore = remember { MacroStore.get(context) }
    val onMacros = tab == CustomizeTab.MACROS
    Column(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        OutlinedTextField(
            value = query,
            onValueChange = onQuery,
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            label = { Text("Search every parameter", style = MaterialTheme.typography.labelSmall) },
        )
        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(top = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            CrystalButton(
                compact = true,
                enabled = tab != null,
                onClick = { if (onMacros) macroStore.surprise() else tab?.let(visualsViewModel::randomizeParams) },
            ) {
                Icon(Icons.Filled.Casino, contentDescription = null, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(6.dp))
                Text(
                    when {
                        onMacros -> "Surprise"
                        tab == null -> "Randomize"
                        else -> "Randomize ${tab.title}"
                    },
                )
            }
            CrystalButton(compact = true, filled = false, enabled = history.canUndo, onClick = visualsViewModel::undoParams) {
                Text("Undo")
            }
            CrystalButton(compact = true, filled = false, enabled = history.canRedo, onClick = visualsViewModel::redoParams) {
                Text("Redo")
            }
            CrystalButton(
                compact = true,
                filled = false,
                enabled = tab != null,
                onClick = { if (onMacros) macroStore.resetKnobs() else tab?.let(visualsViewModel::resetCustomizeTab) },
            ) {
                Text(if (tab == null) "Reset tab" else "Reset ${tab.title}")
            }
            CrystalButton(compact = true, filled = false, enabled = changed > 0, onClick = { confirmReset = true }) {
                Text("Reset all")
            }
            CrystalButton(compact = true, filled = false, onClick = { savingPreset = true }) { Text("Save as preset") }
            Text(
                if (changed == 0) "defaults" else "$changed changed",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
    if (savingPreset) {
        androidx.compose.material3.AlertDialog(
            onDismissRequest = { savingPreset = false },
            title = { Text("Save as preset") },
            text = {
                OutlinedTextField(
                    value = presetName,
                    onValueChange = { presetName = it },
                    singleLine = true,
                    placeholder = { Text("Preset name") },
                )
            },
            confirmButton = {
                CrystalButton(enabled = presetName.isNotBlank(), onClick = {
                    visualsViewModel.savePreset(
                        presetName.trim(),
                        visualizerView.visualizerRenderer.customShaderFor(viewModel.vizState.value.sceneId),
                    )
                    presetName = ""
                    savingPreset = false
                }) { Text("Save") }
            },
            dismissButton = { TextButton(onClick = { savingPreset = false }) { Text("Cancel") } },
        )
    }
    if (confirmReset) {
        androidx.compose.material3.AlertDialog(
            onDismissRequest = { confirmReset = false },
            title = { Text("Reset customizations?") },
            text = {
                Text(
                    "Puts all $changed changed controls back to their defaults, across every tab. " +
                        "Undo brings them back, and saved presets are not touched.",
                )
            },
            confirmButton = {
                CrystalButton(onClick = {
                    visualsViewModel.resetAllCustomize()
                    confirmReset = false
                }) { Text("Reset") }
            },
            dismissButton = {
                TextButton(onClick = { confirmReset = false }) { Text("Cancel") }
            },
        )
    }
}

internal fun takeRenameError(
    from: String,
    proposed: String,
    existingNames: List<String>,
): String? =
    when {
        proposed.isEmpty() -> "A take needs a name."
        !proposed.equals(from, ignoreCase = true) &&
            existingNames.any { it.equals(proposed, ignoreCase = true) } ->
            "There is already a take called \"$proposed\"."
        else -> null
    }

private fun formatTakeTime(ms: Long): String {
    val total = (ms / 1000).coerceAtLeast(0)
    return "%d:%02d".format(total / 60, total % 60)
}

@Composable
internal fun TakesTab(viewModel: StudioViewModel) {
    val studioViewModel = viewModel
    val takes by studioViewModel.takeState.collectAsStateWithLifecycle()
    var renaming by remember { mutableStateOf<String?>(null) }
    var renameText by remember { mutableStateOf("") }
    var deleting by remember { mutableStateOf<String?>(null) }
    LazyColumn(
        Modifier.fillMaxSize().padding(horizontal = 16.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        item {
            Text(
                if (takes.recording) {
                    "Recording — ${takes.recordedEvents} keyframes, ${formatTakeTime(takes.recordedMs)}. " +
                        "Go and perform; press the stop button on the Visuals stage when you are done."
                } else {
                    "A take stores what the visuals were doing, moment by moment — every slider, " +
                        "colour and style change, as you made them. It replays over the live canvas " +
                        "and can be re-rendered at any quality later. Press ● on the Visuals stage to start."
                },
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(vertical = 8.dp),
            )
        }
        if (takes.replaying != null) {
            item {
                Column(Modifier.fillMaxWidth().padding(bottom = 8.dp)) {
                    Text(
                        "Replaying ${takes.replaying} — ${formatTakeTime(takes.replayMs)} / " +
                            formatTakeTime(takes.replayEndMs),
                        style = MaterialTheme.typography.labelMedium,
                        color = accentTextColor(),
                    )
                    LinearProgressIndicator(
                        progress = {
                            if (takes.replayEndMs > 0) {
                                (takes.replayMs.toFloat() / takes.replayEndMs).coerceIn(0f, 1f)
                            } else {
                                0f
                            }
                        },
                        modifier = Modifier.fillMaxWidth().height(2.dp).padding(top = 4.dp),
                        color = MaterialTheme.colorScheme.primary,
                    )
                }
            }
        }
        items(takes.takes, key = { "take_${it.name}" }) { take ->
            val playing = takes.replaying == take.name
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f).padding(vertical = 6.dp)) {
                    Text(take.name, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text(
                        "${formatTakeTime(take.durationMs)} · ${take.eventCount} keyframes · " +
                            "${take.sizeBytes / 1024} KB" +
                            if (takes.exportTake == take.name) " · exports this" else "",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
                IconButton(onClick = {
                    if (playing) studioViewModel.stopReplay() else studioViewModel.playTake(take.name)
                }) {
                    if (playing) {
                        Icon(Icons.Filled.Stop, "Stop replay", tint = MaterialTheme.colorScheme.primary)
                    } else {
                        StoneIconArt(StoneIcon.PLAY, "Replay this take", tint = MaterialTheme.colorScheme.primary)
                    }
                }
                IconButton(onClick = {
                    studioViewModel.setExportTake(if (takes.exportTake == take.name) null else take.name)
                }) {
                    StoneIconArt(
                        StoneIcon.FAVORITE,
                        "Render this take on the next export",
                        tint =
                            if (takes.exportTake == take.name) {
                                MaterialTheme.colorScheme.primary
                            } else {
                                LocalContentColor.current
                            },
                    )
                }
                IconButton(onClick = {
                    renaming = take.name
                    renameText = take.name
                }) { StoneIconArt(StoneIcon.EDIT, "Rename") }
                IconButton(onClick = { deleting = take.name }) {
                    StoneIconArt(StoneIcon.DELETE, "Delete", tint = MaterialTheme.colorScheme.error)
                }
            }
        }
        if (takes.takes.isEmpty() && !takes.recording) {
            item {
                Text("No takes recorded yet.", style = MaterialTheme.typography.bodySmall)
            }
        }
    }
    renaming?.let { old ->
        val proposed = renameText.trim()
        val renameError = takeRenameError(old, proposed, takes.takes.map { it.name })
        androidx.compose.material3.AlertDialog(
            onDismissRequest = { renaming = null },
            title = { Text("Rename take") },
            text = {
                Column {
                    OutlinedTextField(value = renameText, onValueChange = { renameText = it }, singleLine = true)
                    renameError?.let {
                        Text(
                            it,
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.error,
                        )
                    }
                }
            },
            confirmButton = {
                CrystalButton(enabled = renameError == null, onClick = {
                    studioViewModel.renameTake(old, proposed)
                    renaming = null
                }) { Text("Rename") }
            },
            dismissButton = { TextButton(onClick = { renaming = null }) { Text("Cancel") } },
        )
    }
    deleting?.let { name ->
        androidx.compose.material3.AlertDialog(
            onDismissRequest = { deleting = null },
            title = { Text("Delete take \"$name\"?") },
            text = {
                Text("Deletes this recorded performance for good — there is no undo.")
            },
            confirmButton = {
                CrystalButton(onClick = {
                    studioViewModel.deleteTake(name)
                    deleting = null
                }) { Text("Delete") }
            },
            dismissButton = {
                TextButton(onClick = { deleting = null }) { Text("Cancel") }
            },
        )
    }
}

@Composable
private fun TexturesHubTab(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val textures by visualsViewModel.textures.collectAsStateWithLifecycle()
    var deletingTexture by remember { mutableStateOf<String?>(null) }
    val picker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
            if (uris.isNotEmpty()) {
                visualsViewModel.importTextures(uris) { visualizerView.visualizerRenderer.reloadCurrentMilkPreset() }
            }
        }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        CrystalButton(onClick = { picker.launch(arrayOf("image/*")) }) { Text("Import images") }
        textures.forEach { tex ->
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text(tex.name, Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
                CrystalButton(compact = true, filled = false, onClick = {
                    visualsViewModel.useTexture(tex.name) { path -> selectMilk(viewModel, visualizerView, path) }
                }) { Text("Use") }
                IconButton(onClick = { deletingTexture = tex.name }) {
                    StoneIconArt(StoneIcon.DELETE, "Delete this texture", tint = MaterialTheme.colorScheme.error)
                }
            }
        }
        if (textures.isEmpty()) Text("No textures imported yet.", style = MaterialTheme.typography.bodySmall)
    }
    deletingTexture?.let { name ->
        androidx.compose.material3.AlertDialog(
            onDismissRequest = { deletingTexture = null },
            title = { Text("Delete texture \"$name\"?") },
            text = {
                Text(
                    "Removes this image for good — there is no undo. MilkDrop presets that " +
                        "reference it will show noise or black until it is imported again.",
                )
            },
            confirmButton = {
                CrystalButton(onClick = {
                    visualsViewModel.removeTexture(name)
                    deletingTexture = null
                }) { Text("Delete") }
            },
            dismissButton = {
                TextButton(onClick = { deletingTexture = null }) { Text("Cancel") }
            },
        )
    }
}

private const val MAX_SAVED_SHADER_DRAFT_CHARS = 8 * 1024

private val ShaderDraftSaver =
    Saver<String, String>(
        save = { draft -> draft.takeIf { it.length <= MAX_SAVED_SHADER_DRAFT_CHARS } },
        restore = { it },
    )

@Composable
private fun GlslHubTab(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
) {
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    var source by rememberSaveable(viz.sceneId, stateSaver = ShaderDraftSaver) {
        mutableStateOf(visualizerView.visualizerRenderer.customShaderFor(viz.sceneId) ?: "")
    }
    Column {
        Text(
            "Fragment source for this shader scene (view(), pal(), grade() and " +
                "the audio uniforms are available). When FlowField is enabled " +
                "the fluid velocity field is bound as `uniform sampler2D uFlow` " +
                "with `uniform float uFlowStrength` - declare and sample it for " +
                "fluid-driven distortion.",
            style = MaterialTheme.typography.labelSmall,
        )
        OutlinedTextField(
            value = source,
            onValueChange = { source = it },
            modifier = Modifier.fillMaxWidth().height(360.dp),
            textStyle = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
        )
        viz.shaderError?.let {
            Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.labelSmall)
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            CrystalButton(onClick = { viewModel.applyCustomShader(source) }) { Text("Apply shader") }
            TextButton(onClick = {
                source = visualizerView.visualizerRenderer.customShaderFor(viz.sceneId) ?: ""
            }) { Text("Revert") }
        }
    }
}
