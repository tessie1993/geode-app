package dev.geode.ui

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.adaptive.currentWindowAdaptiveInfo
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation3.runtime.NavEntry
import androidx.navigation3.ui.NavDisplay
import androidx.window.core.layout.WindowSizeClass
import dev.geode.data.BehaviourPrefs
import dev.geode.data.BehaviourStore
import dev.geode.data.CanvasGesture
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.GestureAction
import dev.geode.render.VisualizerView
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.LocalGlassBackdrop
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.glassBackdrop
import dev.geode.ui.theme.performStoneHaptic
import dev.geode.ui.theme.rememberGlassBackdrop
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Visuals: the visual fills the whole screen, and everything else floats over it (docs/ui-redesign;
 * the owner dropped the old stage window). This is the root: it reads the state and lays out the
 * three layers, each in its own file.
 *
 * - The stage ([VisualsStage], VisualsStage.kt): the visual and its gestures. By default a tap on
 *   the bottom wakes the shell's chrome (the mini player, the tab bar and the [StageIcons], which
 *   fade together), a tap on the top opens the quick settings ([VisualsQuickSettings]), a double
 *   tap hides or brings back the options, a sideways swipe steps the style, a swipe up wakes the
 *   chrome, a swipe down pulls the Macros drawer ([MacrosDrawer]), a hold records a take, a swirl
 *   randomizes and two fingers play with the visual.
 * - The options ([VisualsOptions], VisualsSheet.kt): matte glass over the visual. In portrait a
 *   bottom sheet of three heights; in landscape (the window wider than tall, from the medium width
 *   class) a side panel on the right, full height (V2 rule 5). Never a sheet stacked under the
 *   visual. Its pages (Styles first, then Customize, Presets and MilkDrop) are a Navigation 3 back
 *   stack, and one [VisualsNavState] drives both layouts.
 * - The drop-ins: the Macros drawer and the quick settings, over everything.
 *
 * Back undoes the most recent first ([VisualsNavState.back]): hidden options come back, then the
 * route in reverse. No per-entry ViewModel decorator: every ViewModel here stays in the Activity's
 * store, which `PlayerSessionProvider` depends on (ADR 0001 § Consequences).
 *
 * [bottomReserve] is the room the shell's floating mini player and tab bar take at the bottom
 * while awake; the sheet and the panel stand on top of it, so they glide down when those fade.
 */
@Composable
internal fun VisualsHub(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    chrome: ShellChromeState,
    bottomReserve: Dp,
    sheetOpacity: Float,
    // Unused since Full screen became the hidden sheet; it leaves with the old live view (owner).
    @Suppress("UNUSED_PARAMETER", "UnusedParameter")
    onOpenLive: () -> Unit,
    onOpenStudioRender: () -> Unit,
    canvasFree: Boolean = false,
) {
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val gate by viewModel.gate.collectAsStateWithLifecycle()
    // Every navigation decision lives in VisualsNavState (VisualsNavigation.kt); this only draws.
    val nav = rememberVisualsNavState()
    val stepStyle: (Int) -> Unit = { step ->
        nextStageStyle(stageStyleOrder(), viz.sceneId, step) { !gate.allowsStyle(it) }?.let(viewModel::selectScene)
    }
    val context = LocalContext.current
    val view = LocalView.current
    // Read once per visit, off the main thread; Settings › Behaviour is another screen, so a change
    // there is always read on the way back.
    val behaviour by produceState(BehaviourPrefs()) {
        value = withContext(Dispatchers.IO) { BehaviourStore(GeodePrefsFiles(context).general).load() }
    }
    val studioViewModel: StudioViewModel = geodeViewModel()
    val takes by studioViewModel.takeState.collectAsStateWithLifecycle()
    var quickSettings by rememberSaveable { mutableStateOf(false) }
    var macros by rememberSaveable { mutableStateOf(false) }
    val actions =
        rememberStageActions(
            viewModel = viewModel,
            visualizerView = visualizerView,
            stepStyle = stepStyle,
            onShowMiniPlayer = chrome::wake,
            onQuickSettings = { quickSettings = true },
            onToggleSheet = { if (nav.sheetShown) nav.hideSheet() else nav.showSheet() },
            onMacros = { macros = true },
            onRecord = { if (!takes.recording) studioViewModel.startRecording() },
        )
    val sizeClass = currentWindowAdaptiveInfo().windowSizeClass
    val backdrop = rememberGlassBackdrop()
    // The side panel's share of the width, kept across turns of the phone.
    var panelFraction by rememberSaveable { mutableFloatStateOf(PANEL_DEFAULT_FRACTION) }
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val side = maxWidth > maxHeight && sizeClass.isWidthAtLeastBreakpoint(WindowSizeClass.WIDTH_DP_MEDIUM_LOWER_BOUND)
        val density = LocalDensity.current
        val statusTop = WindowInsets.statusBars.asPaddingValues().calculateTopPadding()
        val roomPx = with(density) { (maxHeight - statusTop - bottomReserve - GeodeSpacing.EdgeInset).toPx() }.coerceAtLeast(0f)
        val anchors = remember(roomPx, density) { sheetAnchors(roomPx, with(density) { SHEET_PEEK.toPx() }) }
        val fullPx = constraints.maxHeight.toFloat()
        val fullWidthPx = constraints.maxWidth.toFloat()
        val range =
            remember(fullWidthPx, density) {
                with(density) { panelRange(fullWidthPx, PANEL_MIN.toPx(), PANEL_MAX.toPx()) }
            }
        val panelWidth = (panelFraction * fullWidthPx).coerceIn(range)
        val heightPx = remember { Animatable(if (side) 0f else anchors.getValue(nav.sheet)) }
        val panelPx = remember { Animatable(if (side && nav.sheetShown) panelWidth else 0f) }
        var settles by remember { mutableIntStateOf(0) }
        // The motion scheme's spring, so the system's "Remove animations" snaps the options instead.
        val settleSpec = LocalGeodeMotion.current.defaultSpatialSpec<Float>()
        LaunchedEffect(nav.sheet, settles, anchors, side) {
            heightPx.animateTo(if (side) 0f else anchors.getValue(nav.sheet), settleSpec)
        }
        LaunchedEffect(nav.sheetShown, settles, panelWidth, side) {
            panelPx.animateTo(if (side && nav.sheetShown) panelWidth else 0f, settleSpec)
        }
        val reservePx = with(density) { (bottomReserve + GeodeSpacing.EdgeInset).toPx() }
        val insetPx = with(density) { GeodeSpacing.EdgeInset.toPx() }
        VisualsStage(
            visualizerView = visualizerView,
            canvasFree = canvasFree,
            behaviour = behaviour,
            actions = actions,
            recording = takes.recording,
            // The halves of the visual left in view above the sheet; the whole height beside the panel.
            tapSplitPx = { if (side) fullPx / 2f else (fullPx - heightPx.value - reservePx) / 2f },
        )
        // What the glass blurs: the stage icons, never the GL surface (GlassBlur.kt says why).
        Box(Modifier.matchParentSize().glassBackdrop(backdrop)) {
            StageIcons(
                chrome = chrome,
                sheetTopPx = { if (side) 0f else heightPx.value + reservePx },
                endPx = { if (panelPx.value > 0f) panelPx.value + insetPx else 0f },
                // Full screen is the options hidden (owner: the old live view goes), the double tap's twin.
                onFullScreen = { actions.run(GestureAction.TOGGLE_SHEET, 0) },
                onOpenStudioRender = onOpenStudioRender,
                // ‹ › are the sideways swipe's twin: they step what it steps, styles when it steps nothing.
                onStep = { step ->
                    val sideways = behaviour.actionFor(CanvasGesture.SWIPE_SIDEWAYS)
                    actions.run(if (sideways in STEP_ACTIONS) sideways else GestureAction.STEP_STYLE, step)
                },
            )
        }
        CompositionLocalProvider(LocalGlassBackdrop provides backdrop) {
            VisualsOptions(
                nav = nav,
                side = side,
                heightPx = heightPx,
                anchors = anchors,
                panelPx = panelPx,
                panelRange = range,
                opacity = sheetOpacity,
                onSettle = { height ->
                    view.performStoneHaptic(StoneHapticCue.TAP)
                    nav.settleSheet(height)
                    settles++
                },
                onPanelSettle = { width ->
                    view.performStoneHaptic(StoneHapticCue.TAP)
                    if (width == null) {
                        nav.hideSheet()
                    } else {
                        panelFraction = width / fullWidthPx
                    }
                    settles++
                },
                modifier =
                    if (side) {
                        Modifier
                            .align(Alignment.TopEnd)
                            .fillMaxHeight()
                            .padding(bottom = bottomReserve)
                            .windowInsetsPadding(WindowInsets.statusBars)
                            .padding(GeodeSpacing.EdgeInset)
                    } else {
                        Modifier
                            .align(Alignment.BottomCenter)
                            .fillMaxWidth()
                            .padding(bottom = bottomReserve)
                            .padding(horizontal = GeodeSpacing.EdgeInset, vertical = GeodeSpacing.EdgeInset / 2)
                    },
            ) {
                NavDisplay(
                    backStack = nav.backStack,
                    onBack = { nav.pop() },
                    modifier = Modifier.fillMaxSize(),
                    entryProvider = { page ->
                        NavEntry(page) {
                            when (page) {
                                VisualsPage.STYLES -> StylesTab(viewModel)
                                VisualsPage.CUSTOMIZE -> CustomizePanel(viewModel, visualizerView, nav = nav)
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
        // Composed after NavDisplay so it wins over the pages' own back: hidden options come back
        // first. The two overlays below are composed later still, so Back closes them before that.
        BackHandler(enabled = !nav.sheetShown, onBack = nav::showSheet)
        MacrosDrawer(
            visible = macros,
            onDismiss = { macros = false },
            onEdit = {
                macros = false
                nav.openTab(VisualsPage.CUSTOMIZE)
            },
            modifier = Modifier.fillMaxSize(),
        )
        val drop = LocalGeodeMotion.current.defaultEffectsSpec<Float>()
        AnimatedVisibility(quickSettings, Modifier.fillMaxSize(), enter = fadeIn(drop), exit = fadeOut(drop)) {
            VisualsQuickSettings(viewModel, onDismiss = { quickSettings = false }, modifier = Modifier.fillMaxSize())
        }
    }
}

// Takes, Studio's list of recorded performances (StudioHub › Takes). It sits here until Studio's
// My videos rework takes it over.
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
