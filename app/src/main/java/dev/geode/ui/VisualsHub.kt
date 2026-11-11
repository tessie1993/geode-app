package dev.geode.ui

import androidx.compose.animation.ContentTransform
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.material3.adaptive.currentWindowAdaptiveInfo
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
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
import dev.geode.ui.theme.GeodeMotionScheme
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.LocalGlassBackdrop
import dev.geode.ui.theme.StoneHapticCue
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
 *   the bottom or the top wakes the shell's chrome (the mini player, the tab bar and the [StageIcons],
 *   which fade together) and, with the options hidden and the chrome already awake, brings the options
 *   back (the Fullscreen icon reads "Show options" then), a double tap hides or brings back the
 *   options, a sideways swipe steps the style, a swipe up wakes the chrome, a swipe down pulls the Macros drawer ([MacrosDrawer]), a hold records a take, a swirl
 *   randomizes and two fingers play with the visual.
 * - The options ([VisualsOptions], VisualsSheet.kt): matte glass over the visual. In portrait a
 *   bottom sheet of three heights; in landscape (the window wider than tall, from the medium width
 *   class) a side panel on the right, full height (V2 rule 5). Never a sheet stacked under the
 *   visual. Its pages (Styles first, then Customize, Presets and MilkDrop) are a Navigation 3 back
 *   stack, and one [VisualsNavState] drives both layouts.
 * - The drop-in: the Macros drawer, over everything. Settings is a page of the options (the ⚙).
 *
 * Back undoes the most recent first ([VisualsNavState.back]): hidden options come back, then the
 * route in reverse. No per-entry ViewModel decorator: every ViewModel here stays in the Activity's
 * store, which `PlayerSessionProvider` depends on (ADR 0001 § Consequences).
 *
 * [bottomReserve] is the tab bar's place at the bottom, kept whether or not the bar shows, so the
 * options never slide as the chrome fades. On a phone the mini player is docked under the options
 * ([LocalDockedMiniPlayer]) and their glass runs on down behind the tab bar: sheet, player and
 * tabs are one slab, the player and tabs alone when the options are hidden.
 */
@Composable
internal fun VisualsHub(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    chrome: ShellChromeState,
    bottomReserve: Dp,
    sheetOpacity: Float,
    onOpenStudioRender: () -> Unit,
    pip: Boolean = false,
) {
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val gate by viewModel.gate.collectAsStateWithLifecycle()
    // Every navigation decision lives in VisualsNavState (VisualsNavigation.kt); this only draws.
    val nav = rememberVisualsNavState()
    // A PiP window shows the visual alone (owner: the old live view goes): the sheet hides on the way
    // in and comes back on the way out if it was up; the stage icons sleep with the shell's chrome
    // ([rememberShellChrome] keeps it asleep while [pip]).
    var sheetBeforePip by rememberSaveable { mutableStateOf(false) }
    LaunchedEffect(pip) {
        if (pip) {
            sheetBeforePip = nav.sheetShown
            nav.hideSheet()
        } else if (sheetBeforePip) {
            sheetBeforePip = false
            nav.showSheet()
        }
    }
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
    // Settings › Behaviour › Keep the screen on in Visuals: held while this screen is composed (#337).
    KeepScreenOnWhileShown(behaviour.keepScreenOnVisuals)
    val studioViewModel: StudioViewModel = geodeViewModel()
    val takes by studioViewModel.takeState.collectAsStateWithLifecycle()
    var macros by rememberSaveable { mutableStateOf(false) }
    // While a drop-in is up the options fade out and take no touch (UI V2 rule 6: the visual shows
    // through, not the sheet); the nav state keeps the sheet's height, so it returns as it was.
    val dropInOpen = macros
    val fadeSpec = LocalGeodeMotion.current.defaultEffectsSpec<Float>()
    val optionsFade = animateFloatAsState(if (dropInOpen) 0f else 1f, fadeSpec, label = "dropIn")
    val actions =
        rememberStageActions(
            viewModel = viewModel,
            visualizerView = visualizerView,
            stepStyle = stepStyle,
            // The first tap wakes the chrome (and with it the stage icons); a tap while it is awake
            // and the options are hidden brings them back, so a single tap is a way out of full screen.
            onShowMiniPlayer = { if (tapRestoresOptions(chrome.awake, nav.sheetShown)) nav.showSheet() else chrome.wake() },
            onQuickSettings = { nav.openTab(VisualsPage.SETTINGS) },
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
        // The mini player docked under the options (phone, portrait): it takes its own height off
        // the sheet's room, so PEEK is the tabs over the player and TALL still clears the status bar.
        val docked = LocalDockedMiniPlayer.current.takeIf { !side }
        val dockedHeight = if (docked != null) DOCKED_PLAYER_HEIGHT else 0.dp
        // With the player docked the options' glass is the tab bar's too (the shell draws the bar
        // without glass then): it runs down behind the bar, so nothing shows between them.
        val dockGap = if (docked != null) 0.dp else GeodeSpacing.EdgeInset
        val barUnder = if (docked != null) with(density) { chrome.tabBarHeightPx.toDp() } else 0.dp
        val roomPx =
            with(density) { (maxHeight - statusTop - bottomReserve - dockGap - dockedHeight).toPx() }.coerceAtLeast(0f)
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
        // Not keyed on the anchors: they move on every frame of the dock's size animation, and a
        // settle restarted per frame chased the dock on a spring. A settle aims at the anchors of
        // its start and lands on the latest; between settles the sheet follows the dock exactly.
        val latestAnchors by rememberUpdatedState(anchors)
        LaunchedEffect(nav.sheet, settles, side) {
            heightPx.animateTo(if (side) 0f else latestAnchors.getValue(nav.sheet), settleSpec)
            heightPx.snapTo(if (side) 0f else latestAnchors.getValue(nav.sheet))
        }
        LaunchedEffect(anchors) {
            if (!side && !heightPx.isRunning) heightPx.snapTo(anchors.getValue(nav.sheet))
        }
        LaunchedEffect(nav.sheetShown, settles, panelWidth, side) {
            panelPx.animateTo(if (side && nav.sheetShown) panelWidth else 0f, settleSpec)
        }
        val reservePx = with(density) { (bottomReserve + dockGap + dockedHeight).toPx() }
        // The docked player stays while the options are up and holds the tab bar with it
        // (ShellChromeState.docked); with the options hidden it is a pill that sleeps with the
        // chrome, on the same fade as the tab bar under it.
        val playerOn = docked != null && !pip && (nav.sheetShown || chrome.awake)
        val holdsDock = docked != null && !pip && nav.sheetShown
        DisposableEffect(holdsDock, dropInOpen) {
            chrome.docked = holdsDock
            chrome.optionsHidden = dropInOpen
            onDispose {
                chrome.docked = false
                chrome.optionsHidden = false
            }
        }
        val playerAlpha = animateFloatAsState(if (playerOn) 1f else 0f, fadeSpec, label = "dockedPlayer")
        val playerFaded by remember { derivedStateOf { playerAlpha.value == 0f } }
        val playerComposed = docked != null && (playerOn || !playerFaded)
        val insetPx = with(density) { GeodeSpacing.EdgeInset.toPx() }
        VisualsStage(
            visualizerView = visualizerView,
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
                sheetShown = nav.sheetShown,
                sheetTopPx = { if (side) 0f else heightPx.value + reservePx },
                endPx = { if (panelPx.value > 0f) panelPx.value + insetPx else 0f },
                // Full screen is the options hidden (owner: the old live view goes), the double tap's twin.
                onFullScreen = { actions.run(GestureAction.TOGGLE_SHEET, 0) },
                onOpenStudioRender = onOpenStudioRender,
                onSettings = { nav.openTab(VisualsPage.SETTINGS) },
                // ‹ › are the sideways swipe's twin: they step what it steps, styles when it steps nothing.
                onStep = { step ->
                    val sideways = behaviour.actionFor(CanvasGesture.SWIPE_SIDEWAYS)
                    actions.run(if (sideways in STEP_ACTIONS) sideways else GestureAction.STEP_STYLE, step)
                },
            )
        }
        // The options, their glass's margins and corners and the gap above the dock are the
        // sheet's: a tap there must not reach the stage's top or bottom tap or its double tap. Only
        // while shown, so a hidden sheet's empty strip never blocks the stage's bottom tap.
        val swallowTaps = if (nav.sheetShown || playerComposed) Modifier.pointerInput(Unit) { detectTapGestures { } } else Modifier
        CompositionLocalProvider(LocalGlassBackdrop provides backdrop) {
            VisualsOptions(
                nav = nav,
                side = side,
                heightPx = heightPx,
                anchors = anchors,
                panelPx = panelPx,
                panelRange = range,
                opacity = sheetOpacity,
                visualizerView = visualizerView,
                // Native blur under the glass while the options or the player's pill are up; off in PiP.
                blurActive = (nav.sheetShown || playerOn) && !pip,
                bottomBar =
                    if (docked != null && playerComposed) {
                        {
                            docked(Modifier.restartsSleepTimer(chrome))
                            // The tab bar's place: the shell draws the bar over this glass.
                            Spacer(Modifier.height(barUnder))
                        }
                    } else {
                        null
                    },
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
                            .then(swallowTaps)
                            .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.End))
                            .padding(GeodeSpacing.EdgeInset)
                            .hiddenUnderDropIn(dropInOpen) { optionsFade.value }
                    } else {
                        Modifier
                            .align(Alignment.BottomCenter)
                            .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Horizontal))
                            .widthIn(max = SHEET_MAX_WIDTH)
                            .fillMaxWidth()
                            .padding(bottom = (bottomReserve - barUnder).coerceAtLeast(0.dp))
                            .then(swallowTaps)
                            .padding(horizontal = GeodeSpacing.EdgeInset)
                            .padding(top = GeodeSpacing.EdgeInset / 2, bottom = dockGap / 2)
                            .graphicsLayer { if (docked != null) alpha = playerAlpha.value }
                            .hiddenUnderDropIn(dropInOpen) { optionsFade.value }
                    },
            ) {
                val pageMotion = LocalGeodeMotion.current
                NavDisplay(
                    backStack = nav.backStack,
                    onBack = { nav.pop() },
                    modifier = Modifier.fillMaxSize(),
                    // All three on the theme's motion (#557), never Nav3's own 700 ms tweens and 0.7 scale.
                    transitionSpec = { pageCrossFade(pageMotion) },
                    popTransitionSpec = { pageCrossFade(pageMotion) },
                    predictivePopTransitionSpec = { _ -> pageCrossFade(pageMotion) },
                    entryProvider = { page ->
                        NavEntry(page) {
                            when (page) {
                                VisualsPage.STYLES -> StylesTab(viewModel)
                                VisualsPage.CUSTOMIZE -> CustomizePanel(viewModel, visualizerView, nav = nav)
                                VisualsPage.PRESETS -> PresetsTab(viewModel, visualizerView)
                                VisualsPage.MILKDROP ->
                                    MilkDropTab(viewModel, visualizerView, onOpenTextures = { nav.push(VisualsPage.TEXTURES) })
                                VisualsPage.TEXTURES -> TexturesHubTab(viewModel, visualizerView)
                                VisualsPage.SETTINGS -> VisualsSettingsPage(viewModel)
                            }
                        }
                    },
                )
            }
        }
        // Composed after NavDisplay so it wins over the pages' own back: hidden options come back
        // first. The two overlays below are composed later still, so Back closes them before that.
        CloseOnBack(enabled = !nav.sheetShown, onBack = nav::showSheet)
        MacrosDrawer(
            visible = macros,
            sceneId = viz.sceneId,
            onDismiss = { macros = false },
            onEdit = {
                macros = false
                nav.openTab(VisualsPage.CUSTOMIZE)
                nav.openCustomizeCard(CustomizeCategory.QUICK_KNOBS.name)
            },
            modifier = Modifier.fillMaxSize(),
        )
    }
}

/**
 * How the sheet's pages replace one another, forward, back and through a predictive back gesture:
 * a cross-fade on the theme's effects spring ([GeodeMotionScheme.defaultEffectsSpec]).
 *
 * Why not Nav3's defaults: they are fixed `tween(700)` fades and, on a predictive pop, a scale down
 * to 0.7 on its own spring, so neither the app's reduced-motion choice nor Android's "Remove
 * animations" reached them (#557). Why a fade and no slide or zoom: the pages are tabs of one
 * sheet, not a drill-down, and a fade is the one transition the reduced scheme keeps (WCAG 2.3.3
 * allows fades; spatial specs become `snap`, which a seeked predictive transition would show as a
 * jump at the gesture's start). The predictive spec ignores the swipe edge for the same reason: a
 * fade has no side. NavDisplay seeks this same transform with the gesture's progress, so the page
 * underneath fades in as the finger travels and settles back on cancel.
 *
 * Followed: the navigation-3 skill's Animations recipe (`transitionSpec`, `popTransitionSpec`,
 * `predictivePopTransitionSpec` on `NavDisplay`), checked against the navigation3-ui 1.2.0 sources
 * (the predictive spec takes the `@NavigationEvent.SwipeEdge Int`; `NavDisplay`'s own
 * `sizeTransform`, null, replaces the transform's).
 */
private fun pageCrossFade(motion: GeodeMotionScheme): ContentTransform =
    fadeIn(motion.defaultEffectsSpec()) togetherWith fadeOut(motion.defaultEffectsSpec())

/**
 * Whether the "show the mini player" action (the default tap) brings the options back instead of
 * waking the chrome: only when the chrome is already awake, so the first tap out of full screen
 * shows the controls and the next one the options, and a tap never hides what a swipe woke.
 */
internal fun tapRestoresOptions(
    chromeAwake: Boolean,
    sheetShown: Boolean,
): Boolean = chromeAwake && !sheetShown
