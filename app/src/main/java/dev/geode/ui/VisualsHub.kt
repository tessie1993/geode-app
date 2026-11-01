package dev.geode.ui

import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.Orientation
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.draggable
import androidx.compose.foundation.gestures.rememberDraggableState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
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
import androidx.compose.material.icons.filled.FiberManualRecord
import androidx.compose.material.icons.filled.Fullscreen
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
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation3.runtime.NavEntry
import androidx.navigation3.ui.NavDisplay
import dev.geode.R
import dev.geode.pro.FeatureGate
import dev.geode.render.VisualizerView
import dev.geode.render.scene.SceneCapabilities
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.StyleFamilies
import dev.geode.render.scene.VisualStyleCatalog
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlin.math.abs

/**
 * Visuals is a stage: the live canvas on top, where every pick and slider shows as it happens,
 * and a panel under it whose pages (Styles first, then Customize, Presets and MilkDrop) are a
 * Navigation 3 back stack. Nothing is drawn over the stage but a hint: the style's name, Record,
 * Studio and the live view sit in the shell's mini player ([VisualsChrome.FULL]), which the stage
 * asks for through [onChrome] while it is composed.
 *
 * The stage takes gestures: a horizontal swipe steps to the next or previous style the user may
 * use, a double tap to the next look ([nextLook]: the visuals playlist, else the next style), a
 * swipe down hides the mini player and the navigation bar (a swipe up brings them back), and a
 * hold hides the panel so the stage fills the screen (another hold brings it back). Customize
 * opens with the bars hidden, since its sliders want the room, and a swipe up there brings a
 * transport-only player ([VisualsChrome.MINIMAL]). TalkBack gets every gesture as a custom action.
 * The handle between stage and panel trades one for the other.
 *
 * Back undoes the most recent of these first: hidden bars, then a hidden panel, then whatever the
 * page has open (an Advanced card, a Customize page), then the page itself, back to Styles. No
 * per-entry ViewModel decorator: every ViewModel here stays in the Activity's store, which
 * `PlayerSessionProvider` depends on (ADR 0001 § Consequences).
 */
@Composable
fun VisualsHub(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    onChrome: (VisualsChrome?) -> Unit,
    canvasFree: Boolean = false,
) {
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val gate by viewModel.gate.collectAsStateWithLifecycle()
    // Every navigation decision lives in VisualsNavState (VisualsNavigation.kt); this only draws.
    val nav = rememberVisualsNavState()
    val chrome = nav.chrome
    val onChromeNow by rememberUpdatedState(onChrome)
    DisposableEffect(chrome) {
        onChromeNow(chrome)
        onDispose { onChromeNow(null) }
    }
    // Out here, not per page, so the size the user dragged to holds across every page.
    var stageHeightDp by rememberSaveable { mutableFloatStateOf(STAGE_DEFAULT_DP) }
    val stepStyle: (Int) -> Unit = { step ->
        nextStageStyle(stageStyleOrder(), viz.sceneId, step) { !gate.allowsStyle(it) }?.let(viewModel::selectScene)
    }
    // Hiding the panel is a view state, not a page: back brings the panel back before any page.
    BackHandler(enabled = nav.panelBackEnabled, onBack = nav::backFromPanel)
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val fractionDp = (maxHeight * STAGE_MAX_FRACTION).value
        // A short window (split screen, a phone on its side) can have less room than the floor;
        // there the stage keeps what it can and the panel gives way.
        val minStageDp = minOf(STAGE_MIN_DP, fractionDp)
        // The panel keeps PANEL_MIN_DP before the stage grows: on a short phone the stage would
        // otherwise take all the room under it once the mini player sits there too, and the style
        // grid and its preview bar (the only way to choose a style) would get none.
        val maxStageDp = minOf(fractionDp, maxHeight.value - PANEL_MIN_DP).coerceAtLeast(minStageDp)
        Column(Modifier.fillMaxSize()) {
            val stageSize =
                if (nav.panelShown) {
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
                        .stageGestures(
                            onHold = nav::togglePanel,
                            onSwipe = stepStyle,
                            onDoubleTap = { nextLook(viewModel, viz.sceneId, gate) },
                            onBars = nav::showBars,
                            barsShown = chrome != VisualsChrome.HIDDEN,
                        ),
                )
                // On a scrim pill: the stage behind it can be any colour, white included.
                Text(
                    stringResource(
                        when {
                            !nav.panelShown -> R.string.visuals_stage_hint_hidden
                            chrome == VisualsChrome.HIDDEN -> R.string.visuals_stage_hint_bars
                            else -> R.string.visuals_stage_hint
                        },
                    ),
                    Modifier
                        .align(Alignment.BottomCenter)
                        .padding(bottom = 8.dp)
                        .background(MaterialTheme.colorScheme.scrim.copy(alpha = 0.6f), RoundedCornerShape(50))
                        .padding(horizontal = 12.dp, vertical = 4.dp),
                    style = MaterialTheme.typography.labelLarge,
                    color = Color.White,
                )
            }
            if (nav.panelShown) {
                PreviewResizeHandle(onResize = { stageHeightDp = (stageHeightDp + it).coerceIn(minStageDp, maxStageDp) })
                CrystalTabs(
                    titles = VisualsTabs.map { it.title },
                    selected = nav.selectedTab,
                    onSelect = { nav.openTab(VisualsTabs[it]) },
                )
                NavDisplay(
                    backStack = nav.backStack,
                    onBack = { nav.pop() },
                    modifier = Modifier.weight(1f),
                    entryProvider = { page ->
                        NavEntry(page) {
                            when (page) {
                                VisualsPage.STYLES -> StylesTab(viewModel)
                                VisualsPage.CUSTOMIZE -> CustomizePanel(viewModel, visualizerView)
                                VisualsPage.PRESETS -> PresetsTab(viewModel, visualizerView)
                                VisualsPage.MILKDROP ->
                                    MilkDropTab(viewModel, visualizerView, onOpenTextures = { nav.push(VisualsPage.TEXTURES) })
                                VisualsPage.TEXTURES -> TexturesHubTab(viewModel, visualizerView)
                            }
                        }
                    },
                )
            }
        }
    }
    // Composed after NavDisplay so it wins over the pages' own back: hidden bars come back first.
    BackHandler(enabled = nav.barsBackEnabled, onBack = nav::backFromBars)
}

/**
 * The double tap's "next": the user's visuals playlist when it holds two or more looks (the
 * playlist they built is the order they asked for), else the next style the stage swipe would
 * reach. The fullscreen live view uses the same rule.
 */
internal fun nextLook(
    viewModel: PlayerViewModel,
    sceneId: String,
    gate: FeatureGate,
) {
    if (viewModel.stepVizPlaylist()) return
    nextStageStyle(stageStyleOrder(), sceneId, 1) { !gate.allowsStyle(it) }?.let(viewModel::selectScene)
}

/**
 * The mini player's second line on the Visuals stage: the style on screen, or the take being
 * recorded or replayed, which used to sit over the stage itself.
 */
@Composable
internal fun VisualsMiniSubtitle(viewModel: PlayerViewModel) {
    val studioViewModel: StudioViewModel = geodeViewModel()
    val takes by studioViewModel.takeState.collectAsStateWithLifecycle()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    Text(
        when {
            takes.recording -> "● Recording  ${formatTakeTime(takes.recordedMs)}"
            takes.replaying != null -> "▶ ${takes.replaying}"
            else -> sceneDisplayLabel(viz.sceneId)
        },
        style = MaterialTheme.typography.labelMedium,
        color = if (takes.recording) MaterialTheme.colorScheme.error else accentTextColor(),
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
    )
}

/** The Visuals mini player's own actions: record a take, Studio › Render, and the live view. */
@Composable
internal fun RowScope.VisualsMiniActions(
    onOpenStudioRender: () -> Unit,
    onOpenLive: () -> Unit,
) {
    val studioViewModel: StudioViewModel = geodeViewModel()
    val takes by studioViewModel.takeState.collectAsStateWithLifecycle()
    IconButton(onClick = { if (takes.recording) studioViewModel.stopRecording() else studioViewModel.startRecording() }) {
        Icon(
            if (takes.recording) Icons.Filled.StopCircle else Icons.Filled.FiberManualRecord,
            if (takes.recording) "Stop recording this take" else "Record a take",
            tint = if (takes.recording) MaterialTheme.colorScheme.error else LocalContentColor.current,
        )
    }
    // Export, loop render and the takes list live in Studio; this only takes you there, even
    // when the Studio tab is hidden from the bar for someone who came to listen.
    IconButton(onClick = onOpenStudioRender) {
        StoneIconArt(StoneIcon.STUDIO, stringResource(R.string.visuals_open_render))
    }
    IconButton(onClick = onOpenLive) {
        Icon(Icons.Filled.Fullscreen, stringResource(R.string.visuals_view_live))
    }
}

/**
 * A hold toggles the panel; a double tap is the next look; a swipe past [STAGE_SWIPE_DP] steps
 * the style when it is mostly sideways (left is next, like turning a page) and hides or shows the
 * bars when it is mostly up or down. The callbacks go through [rememberUpdatedState] because
 * `pointerInput(Unit)` keeps its first lambda for as long as the stage is on screen.
 */
@Composable
private fun Modifier.stageGestures(
    onHold: () -> Unit,
    onSwipe: (Int) -> Unit,
    onDoubleTap: () -> Unit,
    onBars: (show: Boolean) -> Unit,
    barsShown: Boolean,
): Modifier {
    val hold by rememberUpdatedState(onHold)
    val swipe by rememberUpdatedState(onSwipe)
    val doubleTap by rememberUpdatedState(onDoubleTap)
    val bars by rememberUpdatedState(onBars)
    val next = stringResource(R.string.a11y_next_style)
    val previous = stringResource(R.string.a11y_previous_style)
    val nextLookLabel = stringResource(R.string.a11y_next_look)
    val toggle = stringResource(R.string.a11y_toggle_visuals_panel)
    val barsLabel = stringResource(if (barsShown) R.string.a11y_hide_bars else R.string.a11y_show_bars)
    return this
        .pointerInput(Unit) { detectTapGestures(onLongPress = { hold() }, onDoubleTap = { doubleTap() }) }
        .pointerInput(Unit) {
            var drag = Offset.Zero
            detectDragGestures(
                onDragStart = { drag = Offset.Zero },
                onDragEnd = {
                    val far = STAGE_SWIPE_DP.dp.toPx()
                    when {
                        abs(drag.x) >= abs(drag.y) -> if (abs(drag.x) > far) swipe(if (drag.x < 0) 1 else -1)
                        abs(drag.y) > far -> bars(drag.y < 0)
                    }
                },
            ) { _, delta -> drag += delta }
        }.semantics {
            customActions =
                listOf(
                    CustomAccessibilityAction(nextLookLabel) {
                        doubleTap()
                        true
                    },
                    CustomAccessibilityAction(next) {
                        swipe(1)
                        true
                    },
                    CustomAccessibilityAction(previous) {
                        swipe(-1)
                        true
                    },
                    CustomAccessibilityAction(barsLabel) {
                        bars(!barsShown)
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

@Composable
internal fun MilkDropTab(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    onOpenTextures: () -> Unit,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val pickerContext = androidx.compose.ui.platform.LocalContext.current
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
            CrystalButton(filled = false, onClick = { milkFolderPicker.launchPicker(pickerContext, null) }) { Text("Import folder…") }
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

// What the panel under the stage keeps before the stage grows: the resize handle and tabs,
// a page's search and filter rows, and room for a row of cards with the preview bar.
private const val PANEL_MIN_DP = 300f

private const val STAGE_SWIPE_DP = 72f

private const val PREVIEW_STEP_DP = 80f

// The full touch target: the grip drawn inside it is only 4dp tall.
private val PREVIEW_HANDLE_HEIGHT = 48.dp

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
