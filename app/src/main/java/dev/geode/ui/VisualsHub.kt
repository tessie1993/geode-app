package dev.geode.ui

import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.AnimationVector1D
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.Orientation
import androidx.compose.foundation.gestures.draggable
import androidx.compose.foundation.gestures.rememberDraggableState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Close
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
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigation3.runtime.NavEntry
import androidx.navigation3.ui.NavDisplay
import dev.geode.R
import dev.geode.data.BehaviourPrefs
import dev.geode.data.BehaviourStore
import dev.geode.data.CanvasGesture
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.GestureAction
import dev.geode.pro.FeatureGate
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.render.VisualizerView
import dev.geode.render.scene.SceneCapabilities
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.StyleFamilies
import dev.geode.render.scene.VisualStyleCatalog
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.theme.GeodeGlass
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.floatingGlass
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * Visuals: the visual fills the whole screen, and everything else floats over it (docs/ui-redesign;
 * the owner dropped the old stage window). The options are a sheet of matte glass at the bottom
 * whose height ([SheetHeight]) the user drags between its tabs alone, half and tall, or hides; its
 * pages (Styles first, then Customize, Presets and MilkDrop) are a Navigation 3 back stack.
 *
 * The visual takes gestures ([visualGestures], ui/VisualGestures.kt), each doing what Settings ›
 * Behaviour's gesture map gives it ([BehaviourPrefs.actionFor]). By default a tap on the bottom wakes
 * the shell's chrome (the mini player, the tab bar and the stage icons, which fade together), a tap on
 * the top opens the quick settings ([VisualsQuickSettings]), a double tap hides or brings back the
 * sheet, a sideways swipe steps the style, a swipe up wakes the chrome, a swipe down pulls the Macros
 * drawer ([MacrosDrawer]), a hold records a take, a swirl randomizes and two fingers play with the
 * visual. A swipe that starts in the system's gesture strips is left to Android (back, home, the
 * shade). Gestures have visible twins among the stage icons, and TalkBack gets them as custom actions.
 *
 * Back undoes the most recent first ([VisualsNavState.back]): a hidden sheet comes back, then the
 * route in reverse. No per-entry ViewModel decorator: every ViewModel here stays in the Activity's
 * store, which `PlayerSessionProvider` depends on (ADR 0001 § Consequences).
 *
 * [bottomReserve] is the room the shell's floating mini player and tab bar take at the bottom
 * while awake; the sheet stands on top of it, so it glides down when they fade.
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
    val ring = remember { HoldRingState() }
    BoxWithConstraints(Modifier.fillMaxSize()) {
        // The one VisualizerView has one parent: while the live view or a second screen holds it,
        // the shell's background shows instead.
        if (canvasFree) VisualizerCanvasHost(visualizerView, Modifier.fillMaxSize())
        val density = LocalDensity.current
        val statusTop = WindowInsets.statusBars.asPaddingValues().calculateTopPadding()
        val roomPx = with(density) { (maxHeight - statusTop - bottomReserve - GeodeSpacing.EdgeInset).toPx() }.coerceAtLeast(0f)
        val anchors = remember(roomPx, density) { sheetAnchors(roomPx, with(density) { SHEET_PEEK.toPx() }) }
        val heightPx = remember { Animatable(anchors.getValue(nav.sheet)) }
        var settles by remember { mutableIntStateOf(0) }
        LaunchedEffect(nav.sheet, settles, anchors) {
            heightPx.animateTo(
                anchors.getValue(nav.sheet),
                spring(dampingRatio = Spring.DampingRatioLowBouncy, stiffness = Spring.StiffnessMediumLow),
            )
        }
        val reservePx = with(density) { (bottomReserve + GeodeSpacing.EdgeInset).toPx() }
        val fullPx = constraints.maxHeight.toFloat()
        Box(
            Modifier
                .fillMaxSize()
                .visualGestures(
                    actionFor = behaviour::actionFor,
                    ring = ring,
                    // The top and bottom halves of the visual left in view above the sheet.
                    tapSplitPx = { (fullPx - heightPx.value - reservePx) / 2f },
                    holdArmed = !(takes.recording && behaviour.actionFor(CanvasGesture.LONG_PRESS) == GestureAction.RECORD),
                    onGesture = { gesture, step -> actions.run(behaviour.actionFor(gesture), step) },
                    onTouchPoints = visualizerView.visualizerRenderer::submitTouchPoints,
                    a11yActions = rememberVisualA11yActions(behaviour::actionFor, actions, takes.recording),
                ),
        )
        HoldRing(ring)
        StageIcons(
            chrome = chrome,
            sheetTopPx = { heightPx.value + reservePx },
            // Full screen is the sheet hidden (owner: the old live view goes), the double tap's twin.
            onFullScreen = { actions.run(GestureAction.TOGGLE_SHEET, 0) },
            onOpenStudioRender = onOpenStudioRender,
            // ‹ › are the sideways swipe's twin: they step what it steps, styles when it steps nothing.
            onStep = { step ->
                val sideways = behaviour.actionFor(CanvasGesture.SWIPE_SIDEWAYS)
                actions.run(if (sideways in STEP_ACTIONS) sideways else GestureAction.STEP_STYLE, step)
            },
        )
        VisualsSheet(
            nav = nav,
            heightPx = heightPx,
            anchors = anchors,
            opacity = sheetOpacity,
            onSettle = { height ->
                nav.settleSheet(height)
                settles++
            },
            modifier =
                Modifier
                    .align(Alignment.BottomCenter)
                    .padding(bottom = bottomReserve)
                    .padding(horizontal = GeodeSpacing.EdgeInset, vertical = GeodeSpacing.EdgeInset / 2),
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
        // Composed after NavDisplay so it wins over the pages' own back: a hidden sheet comes back
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
        AnimatedVisibility(quickSettings, Modifier.fillMaxSize(), enter = fadeIn(), exit = fadeOut()) {
            VisualsQuickSettings(viewModel, onDismiss = { quickSettings = false }, modifier = Modifier.fillMaxSize())
        }
    }
}

/**
 * What the visual's gestures can do here, for the gesture map to choose from ([VisualActions]).
 * Randomize everything is Customize's roll with no page given (`randomizeParams()`, the native
 * randomizer), which is Pro there; a free user meets the same paywall Customize's roll shows.
 */
@Composable
private fun rememberStageActions(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    stepStyle: (Int) -> Unit,
    onShowMiniPlayer: () -> Unit,
    onQuickSettings: () -> Unit,
    onToggleSheet: () -> Unit,
    onMacros: () -> Unit,
    onRecord: () -> Unit,
): VisualActions {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val paywall: PaywallViewModel = geodeViewModel()
    val touchPulse = rememberTouchPulse(visualizerView.visualizerRenderer::submitTouchPoints)
    val gate = { viewModel.gate.value }
    return VisualActions(
        showMiniPlayer = onShowMiniPlayer,
        quickSettings = onQuickSettings,
        toggleSheet = onToggleSheet,
        stepStyle = stepStyle,
        stepPreset = { step -> stepPreset(viewModel, gate(), step) },
        // With fewer than two looks in the playlist, the styles step instead (as nextLook does).
        stepPlaylist = { step -> if (!viewModel.stepVizPlaylist(step)) stepStyle(step) },
        macros = onMacros,
        record = onRecord,
        randomizeAll = {
            if (gate().allows(ProFeature.ADVANCED_CUSTOMIZATION)) {
                visualsViewModel.randomizeParams()
            } else {
                paywall.open(PaywallTrigger.Feature(ProFeature.ADVANCED_CUSTOMIZATION))
            }
        },
        touchPulse = touchPulse,
        playPause = viewModel::togglePlayPause,
    )
}

/** The actions a sideways swipe, or its ‹ › twins, can step through. */
private val STEP_ACTIONS = setOf(GestureAction.STEP_STYLE, GestureAction.STEP_PRESET, GestureAction.STEP_PLAYLIST)

/** The sheet's resting heights in px for [roomPx] of room: its tabs alone, half, nearly all. */
private fun sheetAnchors(
    roomPx: Float,
    peekPx: Float,
): Map<SheetHeight, Float> =
    mapOf(
        SheetHeight.HIDDEN to 0f,
        SheetHeight.PEEK to peekPx.coerceAtMost(roomPx),
        SheetHeight.HALF to (roomPx * HALF_FRACTION).coerceAtLeast(peekPx).coerceAtMost(roomPx),
        SheetHeight.TALL to roomPx,
    )

/**
 * Where a drag released at [px] comes to rest: the next height up or down on a fling, else the
 * nearest. [velocityUp] is px/s, positive upward. Below its tabs the sheet hides.
 */
private fun settle(
    px: Float,
    velocityUp: Float,
    anchors: Map<SheetHeight, Float>,
): SheetHeight {
    val ordered = SheetHeight.entries.sortedBy { anchors.getValue(it) }
    return when {
        velocityUp > FLING_PX_PER_S -> ordered.firstOrNull { anchors.getValue(it) > px + 1f } ?: ordered.last()
        velocityUp < -FLING_PX_PER_S -> ordered.lastOrNull { anchors.getValue(it) < px - 1f } ?: ordered.first()
        else -> ordered.minBy { abs(anchors.getValue(it) - px) }
    }
}

/**
 * The options sheet: matte glass floating over the visual, its height read from [heightPx] in the
 * layout pass so a drag or a settle never recomposes the page under it. The header (the grip, the
 * tabs and ×) is what drags; a tab tap on a sheet down to its tabs raises it to half
 * ([VisualsNavState.openTab]). TalkBack gets expand, collapse and hide, since a drag is out of reach.
 */
@Composable
private fun VisualsSheet(
    nav: VisualsNavState,
    heightPx: Animatable<Float, AnimationVector1D>,
    anchors: Map<SheetHeight, Float>,
    opacity: Float,
    onSettle: (SheetHeight) -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    val scope = rememberCoroutineScope()
    val maxPx = anchors.getValue(SheetHeight.TALL)
    val drag =
        rememberDraggableState { delta ->
            scope.launch { heightPx.snapTo((heightPx.value - delta).coerceIn(0f, maxPx)) }
        }
    val handle = stringResource(R.string.visuals_sheet_handle)
    val expand = stringResource(R.string.visuals_sheet_expand)
    val collapse = stringResource(R.string.visuals_sheet_collapse)
    val close = stringResource(R.string.visuals_sheet_close)
    Column(
        modifier
            .fillMaxWidth()
            .layout { measurable, constraints ->
                val h = heightPx.value.roundToInt().coerceIn(0, constraints.maxHeight)
                if (h == 0) {
                    layout(constraints.maxWidth, 0) {}
                } else {
                    val placeable = measurable.measure(constraints.copy(minHeight = h, maxHeight = h))
                    layout(placeable.width, h) { placeable.place(0, 0) }
                }
            }.floatingGlass(fillAlpha = opacity),
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .draggable(
                    drag,
                    Orientation.Vertical,
                    onDragStopped = { velocity -> onSettle(settle(heightPx.value, -velocity, anchors)) },
                ).semantics {
                    contentDescription = handle
                    customActions =
                        listOf(
                            CustomAccessibilityAction(expand) {
                                onSettle(if (nav.sheet == SheetHeight.HALF) SheetHeight.TALL else SheetHeight.HALF)
                                true
                            },
                            CustomAccessibilityAction(collapse) {
                                onSettle(if (nav.sheet == SheetHeight.TALL) SheetHeight.HALF else SheetHeight.PEEK)
                                true
                            },
                            CustomAccessibilityAction(close) {
                                onSettle(SheetHeight.HIDDEN)
                                true
                            },
                        )
                },
        ) {
            Box(
                Modifier
                    .padding(top = GeodeSpacing.S)
                    .size(width = GRIP_WIDTH, height = GRIP_HEIGHT)
                    .background(MaterialTheme.colorScheme.onSurfaceVariant, RoundedCornerShape(50))
                    .align(Alignment.CenterHorizontally),
            )
            Row(verticalAlignment = Alignment.CenterVertically) {
                CrystalTabs(
                    titles = VisualsTabs.map { it.title },
                    selected = nav.selectedTab,
                    onSelect = { nav.openTab(VisualsTabs[it]) },
                    modifier = Modifier.weight(1f),
                )
                IconButton(onClick = { onSettle(SheetHeight.HIDDEN) }) {
                    Icon(Icons.Filled.Close, contentDescription = close)
                }
            }
        }
        Box(Modifier.weight(1f).fillMaxWidth()) { content() }
    }
}

/**
 * The icons over the visual, the visible twins of its gestures: Studio and Full screen (the sheet
 * hidden or back) at the top right, ‹ › at the sides, Record at the bottom right just above the sheet. They fade with
 * the shell's chrome; Record stays while a take is being recorded, red, as its stop.
 */
@Composable
private fun BoxScope.StageIcons(
    chrome: ShellChromeState,
    sheetTopPx: () -> Float,
    onFullScreen: () -> Unit,
    onOpenStudioRender: () -> Unit,
    onStep: (Int) -> Unit,
) {
    val studioViewModel: StudioViewModel = geodeViewModel()
    val takes by studioViewModel.takeState.collectAsStateWithLifecycle()
    val glass = Modifier.floatingGlass(RoundedCornerShape(50), fillAlpha = GeodeGlass.MATTE_FILL)
    AnimatedVisibility(chrome.awake, Modifier.matchParentSize(), enter = fadeIn(), exit = fadeOut()) {
        Box(Modifier.fillMaxSize()) {
            Row(
                Modifier
                    .align(Alignment.TopEnd)
                    .statusBarsPadding()
                    .padding(GeodeSpacing.EdgeInset)
                    .then(glass),
            ) {
                // Export, loop render and the takes list live in Studio; this only takes you there.
                IconButton(onClick = onOpenStudioRender) {
                    StoneIconArt(StoneIcon.STUDIO, stringResource(R.string.visuals_open_render))
                }
                IconButton(onClick = onFullScreen) {
                    Icon(Icons.Filled.Fullscreen, stringResource(R.string.visuals_gesture_toggle_sheet))
                }
            }
            IconButton(onClick = { onStep(-1) }, modifier = Modifier.align(Alignment.CenterStart).padding(GeodeSpacing.S).then(glass)) {
                Icon(Icons.AutoMirrored.Filled.KeyboardArrowLeft, stringResource(R.string.visuals_previous_style))
            }
            IconButton(onClick = { onStep(1) }, modifier = Modifier.align(Alignment.CenterEnd).padding(GeodeSpacing.S).then(glass)) {
                Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, stringResource(R.string.visuals_next_style))
            }
        }
    }
    if (chrome.awake || takes.recording) {
        IconButton(
            onClick = { if (takes.recording) studioViewModel.stopRecording() else studioViewModel.startRecording() },
            modifier =
                Modifier
                    .align(Alignment.BottomEnd)
                    .offset { IntOffset(0, -sheetTopPx().roundToInt()) }
                    .padding(GeodeSpacing.EdgeInset)
                    .then(glass),
        ) {
            Icon(
                if (takes.recording) Icons.Filled.StopCircle else Icons.Filled.FiberManualRecord,
                if (takes.recording) "Stop recording this take" else "Record a take",
                tint = if (takes.recording) MaterialTheme.colorScheme.error else LocalContentColor.current,
            )
        }
    }
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

// Peek: the grip and the tabs row, the sheet's header alone.
private val SHEET_PEEK = 72.dp

private const val HALF_FRACTION = 0.5f

private const val FLING_PX_PER_S = 1200f


private val GRIP_WIDTH = 36.dp

private val GRIP_HEIGHT = 4.dp

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
