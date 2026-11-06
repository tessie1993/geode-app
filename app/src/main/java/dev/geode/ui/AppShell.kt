package dev.geode.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationRail
import androidx.compose.material3.NavigationRailItem
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.VerticalDivider
import androidx.compose.material3.adaptive.currentWindowAdaptiveInfo
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.layout.boundsInWindow
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntRect
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.roundToIntRect
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.navigationevent.NavigationEventInfo
import androidx.navigationevent.NavigationEventTransitionState
import androidx.navigationevent.compose.NavigationBackHandler
import androidx.navigationevent.compose.rememberNavigationEventState
import androidx.window.core.layout.WindowSizeClass
import dev.geode.R
import dev.geode.analysis.SearchMatcher
import dev.geode.data.BootAnimationStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.model.TutorialChapter
import dev.geode.model.TutorialProgress
import dev.geode.pro.PaywallTrigger
import dev.geode.render.VisualizerView
import dev.geode.render.scene.SceneIds
import dev.geode.ui.pro.PaywallHost
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.theme.GeodeGlass
import dev.geode.ui.theme.GeodeMotionScheme
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.VisualAccent
import dev.geode.ui.theme.geodeMotionScheme
import dev.geode.ui.theme.rememberSystemReducedMotion
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.withContext

private const val CRASH_REPORT_MAX_BYTES = 64 * 1024

/** How far a tab fades at the end of a back gesture's travel: still legible, clearly leaving. */
private const val BACK_TO_PLAYER_FADE = 0.5f

/**
 * Back from any tab but Player goes to Player, the app's home (#557 point 6), and the gesture
 * previews it: the returned provider reads how far the back gesture has travelled (0..1), which the
 * shell turns into the tab's fade. While the finger moves the value follows it exactly; on completion
 * [GeodeAppState.resetToPlayer] commits, and on completion or cancel the value eases back to 0 on the
 * theme's effects spring ([GeodeMotionScheme.fastEffectsSpec]), so Player fades in from where the
 * gesture left it and a cancel settles rather than jumps.
 *
 * Why a provider and an effect, not a value: the gesture changes the transition state every frame,
 * and this is a non-restartable composable (it returns a value), so a state read here would
 * invalidate the caller's scope and recompose the whole shell per frame. Instead a [snapshotFlow] in
 * a [LaunchedEffect] watches it and drives an [Animatable], and the caller reads the Animatable only
 * inside `graphicsLayer {}`, in the draw phase (jetpack-compose-performance skill, "Phase Deferral";
 * compose-performance skill, deferred reads). `collectLatest`, so a new gesture cuts a settle short
 * instead of queueing behind it. A fade and no slide or scale, so the reduced-motion scheme keeps it
 * as it is (spatial specs would snap there).
 *
 * The API: NavigationEvent's [NavigationBackHandler] with its [rememberNavigationEventState], whose
 * [NavigationEventTransitionState.InProgress] carries the progress. The navigation-event skill (and
 * its "Handle back gestures and predictive back animations" guide) names it for Compose on SDK 36+
 * in place of activity's `BackHandler`/`PredictiveBackHandler`: no Flow to collect and no
 * CancellationException to tell a cancel apart; the progress is snapshot state, watched above. It
 * shares the activity's one dispatcher with every `BackHandler` and Nav3's `NavDisplay`, and the
 * last registered enabled handler wins, which is why the caller composes this before every screen:
 * a screen's own back (Visuals' pages, Studio's tabs, a sheet) comes first. It is disabled on
 * Player, so Back there is the system's back-to-home, with the system's own preview.
 */
@Composable
private fun rememberBackToPlayer(
    appState: GeodeAppState,
    motion: GeodeMotionScheme,
): () -> Float {
    val backState = rememberNavigationEventState(currentInfo = NavigationEventInfo.None)
    NavigationBackHandler(
        state = backState,
        isBackEnabled = !appState.onPlayer,
        onBackCompleted = appState::resetToPlayer,
    )
    val fade = remember { Animatable(0f) }
    LaunchedEffect(backState, motion) {
        snapshotFlow { (backState.transitionState as? NavigationEventTransitionState.InProgress)?.latestEvent?.progress }
            .collectLatest { travel ->
                if (travel != null) fade.snapTo(travel) else fade.animateTo(0f, motion.fastEffectsSpec())
            }
    }
    return remember(fade) { { fade.value } }
}

/**
 * A destination paired with the resolved label and icon the navigation surfaces draw for it.
 *
 * Built from [GeodeDestination.entries], so the bar and the rail cannot drift out of step with
 * the screens they switch between, and selection compares destinations rather than positions.
 */
private data class NavEntry(
    val destination: GeodeDestination,
    val item: DockItem,
)

@Composable
fun AppRoot() {
    val viewModel: PlayerViewModel = geodeViewModel()
    val settingsViewModel: SettingsViewModel = geodeViewModel()
    val paywallViewModel: PaywallViewModel = geodeViewModel()
    val context = LocalContext.current
    val visualizerView = remember { VisualizerView(context) }
    // GLSurfaceView renders continuously and only ever stops when its host says so. Without this
    // the render thread keeps drawing whenever the app is visible but not resumed — behind a
    // permission dialog, in split-screen, partially obscured — burning battery on frames nobody
    // sees. GLSurfaceView's contract is that the owner forwards these two.
    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner, visualizerView) {
        val observer =
            LifecycleEventObserver { _, event ->
                when (event) {
                    Lifecycle.Event.ON_RESUME -> visualizerView.onResume()
                    Lifecycle.Event.ON_PAUSE -> visualizerView.onPause()
                    else -> Unit
                }
            }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            visualizerView.onPause()
        }
    }
    val gui by settingsViewModel.guiPrefs.collectAsStateWithLifecycle()
    val externalDisplay = rememberExternalDisplay()
    val appState = rememberGeodeAppState(externalDisplay)
    // Read on IO: at cold start this is the first touch of the general prefs file, and a
    // SharedPreferences read blocks until the whole file has been parsed. Null until it lands,
    // and nothing below that depends on it (the notice, setup, the intro) draws until then.
    val bootAnimEnabled by produceState<Boolean?>(null) {
        value = withContext(Dispatchers.IO) { BootAnimationStore(GeodePrefsFiles(context).general).load() }
    }
    val state by viewModel.uiState.collectAsStateWithLifecycle()
    // Not delegated: only the mini player's progress bar reads .value, in its own draw lambda.
    val position = viewModel.positionMs.collectAsStateWithLifecycle()
    val onSecondScreen = gui.secondScreen && externalDisplay != null
    if (onSecondScreen) {
        SecondScreenCanvas(externalDisplay, visualizerView)
    }
    // The Visuals screen's bounds, in root pixels. The one VisualizerView is hosted once below and
    // laid out there, so leaving Visuals hides it rather than detaching it (VisualizerCanvasHost).
    val visualsBounds = remember { mutableStateOf(IntRect.Zero) }

    var crashText by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(Unit) {
        crashText =
            withContext(Dispatchers.IO) {
                val file = java.io.File(context.filesDir, "crash-latest.txt")
                if (file.exists()) {
                    file.inputStream().use { stream ->
                        val buf = ByteArray(CRASH_REPORT_MAX_BYTES)
                        var read = 0
                        while (read < buf.size) {
                            val n = stream.read(buf, read, buf.size - read)
                            if (n < 0) break
                            read += n
                        }
                        String(buf, 0, read, Charsets.UTF_8)
                    }
                } else {
                    null
                }
            }
    }
    VisualizerEngineBindings(viewModel, visualizerView)
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    // The accent follows the visual: the middle of its palette's hues; MilkDrop paints its own.
    val accentTarget =
        if (viz.sceneId == SceneIds.MILKDROP) VisualAccent.Fallback else paletteAccent(viz.params.paletteBase, viz.params.paletteRange)
    // Outside the theme, so the motion scheme is built here from the same switches the theme reads.
    // Android's "Remove animations" alone (data/SystemMotion, #336); the paywall below reads it too.
    val reducedMotion = rememberSystemReducedMotion()
    val motion = geodeMotionScheme(reducedMotion)
    // Player is home: Back from any other tab goes there, previewed by the gesture (#557). Composed
    // before the theme and every screen, so their own back handlers, registered later, win first.
    val backToPlayer = rememberBackToPlayer(appState, motion)
    val accent by animateColorAsState(accentTarget, motion.slowEffectsSpec(), label = "accent")
    // The mini player (and on Visuals the tab bar and stage icons) wakes on a new screen or track.
    // A PiP window shows the visual alone (the sheet and the chrome hidden) until it closes.
    val inPip = VisualizerPipCoordinator.inPictureInPicture
    val chrome = rememberShellChrome(gui.miniPlayerFadeSeconds, inPip, appState.dest, state.title)
    var dockHeight by remember { mutableStateOf(0.dp) }
    val visualsReserve by animateDpAsState(dockHeight, motion.defaultSpatialSpec(), label = "dock")
    CrystalMaterialTheme(gui = gui, accentSource = accent) {
        val miniPlayer: @Composable (Modifier) -> Unit = { modifier ->
            FloatingMiniPlayer(
                title = state.title,
                artist = state.artist,
                artworkUri = viewModel.currentTrackUri(),
                isPlaying = state.isPlaying,
                progress = { if (state.durationMs > 0) position.value / state.durationMs.toFloat() else 0f },
                opacity = GeodeGlass.MATTE_FILL,
                onPin = chrome::pin,
                onOpenPlayer = { appState.navigateTo(GeodeDestination.PLAYER) },
                onHide = chrome::hide,
                onPlayPause = viewModel::togglePlayPause,
                onNext = viewModel::next,
                modifier = modifier,
            )
        }
        val dockedPlayer: @Composable (Modifier) -> Unit = { modifier ->
            DockedMiniPlayer(
                title = state.title,
                isPlaying = state.isPlaying,
                progress = { if (state.durationMs > 0) position.value / state.durationMs.toFloat() else 0f },
                onPin = chrome::pin,
                onOpenPlayer = { appState.navigateTo(GeodeDestination.PLAYER) },
                onHide = chrome::hide,
                onPrevious = viewModel::previous,
                onPlayPause = viewModel::togglePlayPause,
                onNext = viewModel::next,
                modifier = modifier,
            )
        }
        val tutorial = rememberTutorialState()
        val navEntries =
            GeodeDestination.entries
                .map { NavEntry(it, DockItem(stringResource(it.labelRes), it.icon)) }
        // Keyed by destination, not by position: rotating swaps the bar layout for the rail
        // layout, which would otherwise drop every screen's saved tab and scroll state.
        val destinationState = rememberSaveableStateHolder()
        val destinationContent: @Composable (twoPane: Boolean) -> Unit = { twoPane ->
            // The tab fades toward Player as a back gesture travels (rememberBackToPlayer); the banner
            // stays lit, since it belongs to playback, not to the tab.
            if (!inPip) PlaybackNoticeBanner(viewModel)
            Box(Modifier.graphicsLayer { alpha = 1f - BACK_TO_PLAYER_FADE * backToPlayer() }) {
                destinationState.SaveableStateProvider(appState.dest.name) {
                    when (appState.dest) {
                        GeodeDestination.PLAYER ->
                            PlayerScreen(
                                viewModel,
                                onOpenSearch = appState::openSearch,
                                // The old live view is gone (owner): this opens Visuals, whose full screen is the sheet hidden.
                                onExpand = { appState.navigateTo(GeodeDestination.VISUALS) },
                                onOpenLibrary = { appState.navigateTo(GeodeDestination.LIBRARY) },
                            )
                        GeodeDestination.LIBRARY ->
                            if (twoPane) {
                                Row(Modifier.fillMaxSize()) {
                                    Box(Modifier.weight(1f)) {
                                        LibraryScreen(
                                            onOpenSearch = appState::openSearch,
                                            onOpenFolders = appState::openSettingsFolders,
                                            onOpenVisuals = { appState.navigateTo(GeodeDestination.VISUALS) },
                                        )
                                    }
                                    VerticalDivider()
                                    Box(Modifier.weight(1f)) {
                                        PlayerScreen(
                                            viewModel,
                                            onOpenSearch = appState::openSearch,
                                            onExpand = { appState.navigateTo(GeodeDestination.VISUALS) },
                                            onOpenLibrary = {},
                                        )
                                    }
                                }
                            } else {
                                LibraryScreen(
                                    onOpenSearch = appState::openSearch,
                                    onOpenFolders = appState::openSettingsFolders,
                                    onOpenVisuals = { appState.navigateTo(GeodeDestination.VISUALS) },
                                )
                            }
                        GeodeDestination.VISUALS ->
                            // The visual fills Visuals, so these bounds are the PiP window's source rect and aspect,
                            // and where the shell lays the visual out.
                            Box(
                                Modifier.fillMaxSize().onGloballyPositioned {
                                    VisualizerPipCoordinator.canvasBoundsPx = it.windowRect()
                                    visualsBounds.value = it.boundsInRoot().roundToIntRect()
                                },
                            ) {
                                VisualsHub(
                                    viewModel,
                                    visualizerView,
                                    chrome = chrome,
                                    bottomReserve = visualsReserve,
                                    // The glass slider (Visuals quick settings) tunes only the sheet over the visual, so the change shows.
                                    sheetOpacity = gui.barOpacity,
                                    onOpenStudioRender = appState::openStudioRender,
                                    pip = inPip,
                                )
                            }
                        GeodeDestination.STUDIO ->
                            StudioHub(
                                viewModel,
                                visualizerView,
                                appState.studioSection,
                                onSection = { appState.studioSection = it },
                            )
                        GeodeDestination.SETTINGS ->
                            SettingsScreen(
                                viewModel,
                                requestedGroup = appState.settingsGroupRequest,
                                onRequestedGroupShown = { appState.settingsGroupRequest = null },
                                onOpenStudioRender = appState::openStudioRender,
                                onPlayTutorial = { tutorial.start(it) },
                                onResetTutorials = {
                                    tutorial.autoOfferUsed = true
                                    settingsViewModel.setGuiPrefs(gui.copy(tutorialSeen = false, tutorialProgress = TutorialProgress()))
                                },
                            )
                    }
                }
            }
        }
        Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background)) {
            val sizeClass = currentWindowAdaptiveInfo().windowSizeClass
            // Two panes only where both get a usable width and the Player pane can show more than
            // its artwork: from 840dp, and not on a short landscape phone. A foldable or a phone on
            // its side (600-839dp) gets the rail with one pane.
            val twoPane =
                sizeClass.isWidthAtLeastBreakpoint(WindowSizeClass.WIDTH_DP_EXPANDED_LOWER_BOUND) &&
                    sizeClass.isHeightAtLeastBreakpoint(WindowSizeClass.HEIGHT_DP_MEDIUM_LOWER_BOUND)
            // Onboarding and the safety notice draw in this window, over the shell. Hiding the shell
            // from accessibility keeps TalkBack from swiping into the app beneath. (The tour is a
            // Dialog, a window of its own, so it needs no such help.)
            val overlayUp = (bootAnimEnabled == false || appState.bootDone) && (!gui.safetyAcknowledged || !gui.setupDone)
            Box(
                Modifier
                    .fillMaxSize()
                    .then(if (overlayUp) Modifier.clearAndSetSemantics { } else Modifier),
            ) {
                // Drawn first, so every screen draws over it; Visuals draws nothing behind its stage,
                // so the visual shows there. The one VisualizerView has one parent: the second screen
                // holds it while it is on, and this host steps aside.
                if (!onSecondScreen) {
                    VisualizerCanvasHost(visualizerView, shown = appState.dest == GeodeDestination.VISUALS, bounds = visualsBounds)
                }
                if (!sizeClass.isWidthAtLeastBreakpoint(WindowSizeClass.WIDTH_DP_MEDIUM_LOWER_BOUND)) {
                    AppShellCompact(
                        navEntries = navEntries,
                        appState = appState,
                        chrome = chrome,
                        hasMedia = state.hasMedia,
                        pip = inPip,
                        miniPlayer = miniPlayer,
                        dockedPlayer = dockedPlayer,
                        onDock = { dockHeight = it },
                        content = { destinationContent(false) },
                    )
                } else {
                    // The Player pane beside Library already has the transport.
                    val playerPaneShown = twoPane && appState.dest == GeodeDestination.LIBRARY
                    AppShellExpanded(
                        navEntries = navEntries,
                        appState = appState,
                        chrome = chrome,
                        showMiniPlayer = state.hasMedia && !appState.onPlayer && !playerPaneShown,
                        pip = inPip,
                        centerRail = sizeClass.isHeightAtLeastBreakpoint(WindowSizeClass.HEIGHT_DP_MEDIUM_LOWER_BOUND),
                        miniPlayer = miniPlayer,
                        onDock = { dockHeight = it },
                        content = { destinationContent(twoPane) },
                    )
                }
                if (appState.searching) {
                    SearchScreen(viewModel, onClose = appState::closeSearch)
                }
            }
            val clipLabel = stringResource(R.string.crash_clip_label)
            crashText?.let { text ->
                androidx.compose.material3.AlertDialog(
                    onDismissRequest = {},
                    title = { Text(stringResource(R.string.crash_dialog_title)) },
                    text = {
                        Text(
                            stringResource(R.string.crash_dialog_body, text.take(600)),
                            style = MaterialTheme.typography.bodySmall,
                        )
                    },
                    confirmButton = {
                        TextButton(onClick = {
                            val cm = context.getSystemService(android.content.ClipboardManager::class.java)
                            cm.setPrimaryClip(android.content.ClipData.newPlainText(clipLabel, text))
                        }) { Text(stringResource(R.string.action_copy)) }
                    },
                    dismissButton = {
                        TextButton(onClick = {
                            java.io.File(context.filesDir, "crash-latest.txt").delete()
                            crashText = null
                        }) { Text(stringResource(R.string.action_dismiss)) }
                    },
                )
            }
            // Read by MainActivity to decide whether leaving the app should be treated as "enter
            // PiP" at all: only Visuals shows the visual, and every other screen has nothing worth
            // shrinking into a window. Not while a connected display holds the visual either: this
            // phone then shows Visuals without it, and a PiP window of that would carry nothing.
            val visualShowing = appState.dest == GeodeDestination.VISUALS && !onSecondScreen
            LaunchedEffect(visualShowing) {
                VisualizerPipCoordinator.visualizerShowing = visualShowing
            }
            // Keeps the platform's own auto-enter flag (API 31+) and the PiP window's play/pause
            // remote action in step with what is actually true.
            LaunchedEffect(gui.autoEnterPip, state.isPlaying, visualShowing) {
                context.findMainActivity()?.refreshPipParams()
            }
            if ((bootAnimEnabled == false || appState.bootDone) && !gui.safetyAcknowledged) {
                SafetyConsent(
                    onAcknowledge = { settingsViewModel.setGuiPrefs(gui.copy(safetyAcknowledged = true)) },
                )
            } else if ((bootAnimEnabled == false || appState.bootDone) && !gui.setupDone) {
                // Setup runs only after the notice is acknowledged, and only until it is done.
                FirstRunGate(
                    onDone = {
                        settingsViewModel.setGuiPrefs(gui.copy(setupDone = true))
                        appState.navigateTo(gui.intent.landingDestination)
                        // No Pro offer here: it waits until the guided tour ends (TutorialHost).
                    },
                )
            }
            // Offered once setup is out of the way, so the tour talks about a real app with a real
            // library, and held back while the boot intro, the notice, setup or the paywall is up.
            val paywall by paywallViewModel.uiState.collectAsStateWithLifecycle()
            TutorialHost(
                state = tutorial,
                gui = gui,
                blocked = (bootAnimEnabled != false && !appState.bootDone) || !gui.safetyAcknowledged || !gui.setupDone || paywall.visible,
                onGuiChange = settingsViewModel::setGuiPrefs,
                onShowMe = { target -> showTutorialTarget(target, appState) { paywallViewModel.open(PaywallTrigger.Settings) } },
            )
            // Drawn while the pref is still loading too (null): BootIntro opens on the splash's
            // last frame, so it covers that gap instead of letting the app flash through. If the
            // pref then lands false, it ends there, just as the splash would have.
            if (bootAnimEnabled != false && !appState.bootDone) {
                BootIntro(onDone = { appState.bootDone = true })
            }
            RecordSaveHost()
            PaywallHost(reducedMotion = reducedMotion)
            SharedLinkConfirm()
        }
    }
}

/**
 * The phone shell. Every screen fills the window; the mini player and the tab bar float over the
 * bottom of it on matte glass ([FloatingMiniPlayer], [FloatingTabBar]), inset from the edges.
 *
 * The mini player fades on every screen ([ShellChromeState]); asleep, a small handle pill keeps
 * its place. On Visuals the visual takes the whole window: the tab bar fades with the mini player
 * there and no handle stays, since a tap at the bottom of the visual brings both back. The other
 * screens keep the tab bar and are padded above it, so a list never ends under the glass.
 * [onDock] reports the tab bar's place in the dock, which the Visuals options stand on; on Visuals
 * the mini player is docked to those options instead ([LocalDockedMiniPlayer]).
 */
@Composable
private fun AppShellCompact(
    navEntries: List<NavEntry>,
    appState: GeodeAppState,
    chrome: ShellChromeState,
    hasMedia: Boolean,
    pip: Boolean,
    miniPlayer: @Composable (Modifier) -> Unit,
    dockedPlayer: @Composable (Modifier) -> Unit,
    onDock: (Dp) -> Unit,
    content: @Composable () -> Unit,
) {
    val density = LocalDensity.current
    // From the motion scheme, so "Remove animations" makes the dock appear without moving.
    val dockFade = LocalGeodeMotion.current.defaultEffectsSpec<Float>()
    val dockSize = LocalGeodeMotion.current.defaultSpatialSpec<IntSize>()
    val onVisuals = appState.dest == GeodeDestination.VISUALS
    val showsPlayer = hasMedia && !appState.onPlayer
    var barHeight by remember { mutableStateOf(0.dp) }
    Box(Modifier.fillMaxSize()) {
        // The handle's zone only while there is a handle to keep clear (the mini player hides on
        // Player itself), so Player does not lose a touch target's height to nothing.
        val navInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
        val bottom = navInset + barHeight + DOCK_GAP + if (showsPlayer) HANDLE_ZONE else 0.dp
        // What the Visuals options stand on: the tab bar's place, whether or not the bar is showing,
        // so the sheet never slides as the dock fades (owner: less twitchy). The mini player is not
        // in it there: Visuals docks it to the options (LocalDockedMiniPlayer).
        val dock = navInset + barHeight + DOCK_GAP
        LaunchedEffect(dock) { onDock(dock) }
        // Player scrolls under the glass and takes the reserve as content padding
        // (LocalDockReserve), so its transport is never cut hard above the bar; the other screens
        // are padded above it. The reserve is consumed as an inset, so imePadding below adds only
        // what the keyboard rises past it instead of counting the bottom twice.
        val padsItself = appState.onPlayer
        Box(
            Modifier
                .fillMaxSize()
                .then(
                    if (onVisuals) {
                        Modifier
                    } else {
                        Modifier
                            .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal))
                            .padding(bottom = if (padsItself) 0.dp else bottom)
                            .consumeWindowInsets(PaddingValues(bottom = bottom))
                            .imePadding()
                    },
                ),
        ) {
            CompositionLocalProvider(
                LocalDockReserve provides if (padsItself) bottom else 0.dp,
                LocalDockedMiniPlayer provides if (onVisuals && showsPlayer && !pip) dockedPlayer else null,
            ) { content() }
        }
        // A PiP window is the visual alone.
        if (pip) return@Box
        Column(
            Modifier
                .align(Alignment.BottomCenter)
                .fillMaxWidth()
                // Any touch in the dock restarts its sleep timer, so it never fades under a
                // finger on its way to a tab (it used to vanish before a second tap could land).
                .restartsSleepTimer(chrome)
                .navigationBarsPadding()
                .padding(horizontal = GeodeSpacing.EdgeInset)
                .padding(bottom = DOCK_GAP),
            verticalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
        ) {
            AnimatedVisibility(
                showsPlayer && chrome.awake && !onVisuals,
                enter = fadeIn(dockFade) + expandVertically(dockSize),
                exit = fadeOut(dockFade) + shrinkVertically(dockSize),
            ) {
                miniPlayer(Modifier)
            }
            if (showsPlayer && !chrome.awake && !onVisuals) MiniPlayerHandle(onWake = chrome::wake)
            // On Visuals the bar stays while the options are up (they stand on it, the player docked
            // to them), and otherwise fades in place on the same fade as the docked player's pill,
            // never resizing the dock.
            AnimatedVisibility(
                !onVisuals || chrome.awake || chrome.docked,
                enter = if (onVisuals) fadeIn(dockFade) else fadeIn(dockFade) + expandVertically(dockSize),
                exit = if (onVisuals) fadeOut(dockFade) else fadeOut(dockFade) + shrinkVertically(dockSize),
            ) {
                FloatingTabBar(
                    items = navEntries.map { it.item },
                    // A destination can be hidden while still being the current one — reaching
                    // Studio from a track menu, say. Fall back to the first tab rather than -1.
                    selected = navEntries.indexOfFirst { it.destination == appState.dest }.coerceAtLeast(0),
                    onSelect = { appState.navigateTo(navEntries[it].destination) },
                    opacity = GeodeGlass.MATTE_FILL,
                    modifier = Modifier.onSizeChanged { if (it.height > 0) barHeight = with(density) { it.height.toDp() } },
                )
            }
        }
    }
}

/**
 * The wide shell: the navigation rail at the side, the screen beside it, and the floating mini
 * player over the bottom of the screen, fading like the phone's ([AppShellCompact]).
 */
@Composable
private fun AppShellExpanded(
    navEntries: List<NavEntry>,
    appState: GeodeAppState,
    chrome: ShellChromeState,
    showMiniPlayer: Boolean,
    pip: Boolean,
    centerRail: Boolean,
    miniPlayer: @Composable (Modifier) -> Unit,
    onDock: (Dp) -> Unit,
    content: @Composable () -> Unit,
) {
    val density = LocalDensity.current
    // From the motion scheme, so "Remove animations" makes the dock appear without moving.
    val dockFade = LocalGeodeMotion.current.defaultEffectsSpec<Float>()
    // safeDrawing rather than the status and navigation bars alone: in landscape the display
    // cutout sits on a side edge, where it would cover the rail or the content's far edge. It
    // includes the keyboard too, so a focused field stays above it.
    Row(Modifier.fillMaxSize().safeDrawingPadding()) {
        // A PiP window is the visual alone, with no rail beside it.
        if (!pip) NavRail(navEntries, appState, centerRail)
        Box(Modifier.weight(1f).fillMaxHeight()) {
            content()
            Column(
                Modifier
                    .align(Alignment.BottomCenter)
                    .widthIn(max = MINI_PLAYER_MAX_WIDTH)
                    .fillMaxWidth()
                    .onSizeChanged { onDock(with(density) { it.height.toDp() }) }
                    .padding(GeodeSpacing.EdgeInset),
            ) {
                AnimatedVisibility(showMiniPlayer && chrome.awake, enter = fadeIn(dockFade), exit = fadeOut(dockFade)) {
                    miniPlayer(Modifier)
                }
                if (showMiniPlayer && !chrome.awake && appState.dest != GeodeDestination.VISUALS) MiniPlayerHandle(onWake = chrome::wake)
            }
        }
    }
}

/** The standard M3 navigation rail, on the plain canvas: no glass, no glow. */
@Composable
private fun NavRail(
    navEntries: List<NavEntry>,
    appState: GeodeAppState,
    centerRail: Boolean,
) {
    NavigationRail(containerColor = Color.Transparent) {
        if (centerRail) Spacer(Modifier.weight(1f))
        navEntries.forEach { (destination, item) ->
            val selected = appState.dest == destination
            NavigationRailItem(
                selected = selected,
                onClick = { appState.navigateTo(destination) },
                // The label below names the item; a description on the icon reads it twice.
                icon = { StoneIconArt(item.icon, null, selected = selected) },
                label = { Text(item.label, style = MaterialTheme.typography.labelSmall) },
            )
        }
        if (centerRail) Spacer(Modifier.weight(1f))
    }
}

/** The accent a palette gives: the middle of its hue span, bright enough to read on black. */
private fun paletteAccent(
    base: Float,
    range: Float,
): Color {
    val hue = ((base + range / 2f) % 1f + 1f) % 1f
    return Color.hsl(hue * DEGREES, ACCENT_SATURATION, ACCENT_LIGHTNESS)
}

private const val DEGREES = 360f
private const val ACCENT_SATURATION = 0.85f
private const val ACCENT_LIGHTNESS = 0.6f

/** The gap between the floating dock and the screen's bottom edge, and between the screen and the dock. */
private val DOCK_GAP = 8.dp

/**
 * The bottom of a screen the floating dock covers, for a screen that scrolls under the glass and
 * pads its own content (Player); 0 for a screen the shell already pads above the dock.
 */
val LocalDockReserve = compositionLocalOf { 0.dp }

/**
 * The mini player docked to the Visuals options, provided by the phone shell while something is
 * playing; null elsewhere (the wide shell keeps its floating player), and Visuals then shows its
 * options alone.
 */
val LocalDockedMiniPlayer = compositionLocalOf<(@Composable (Modifier) -> Unit)?> { null }

/** The handle pill's full touch target, kept clear above the tab bar on every screen. */
private val HANDLE_ZONE = 48.dp

/** How wide the floating mini player grows on a tablet before it stops. */
private val MINI_PLAYER_MAX_WIDTH = 640.dp

/**
 * Window-space bounds, not local: PictureInPictureParams.setSourceRectHint wants the same
 * coordinate space the platform draws the Activity's window in.
 */
private fun LayoutCoordinates.windowRect(): android.graphics.Rect {
    val bounds = boundsInWindow()
    return android.graphics.Rect(bounds.left.toInt(), bounds.top.toInt(), bounds.right.toInt(), bounds.bottom.toInt())
}

@Composable
fun SettingsScreen(
    viewModel: PlayerViewModel,
    requestedGroup: SettingsGroupKey? = null,
    onRequestedGroupShown: () -> Unit = {},
    onOpenStudioRender: () -> Unit,
    onPlayTutorial: (List<TutorialChapter>) -> Unit,
    onResetTutorials: () -> Unit,
) {
    Column(Modifier.fillMaxSize()) {
        Column(Modifier.padding(start = 16.dp, end = 16.dp, top = 16.dp)) {
            Text(
                stringResource(R.string.app_name),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            // Every top-level screen opens with its heading, the one TalkBack users jump between.
            Text(
                stringResource(R.string.nav_settings),
                Modifier.semantics { heading() },
                style = MaterialTheme.typography.headlineSmall,
                color = MaterialTheme.colorScheme.onBackground,
            )
        }
        AppSettingsTab(
            viewModel,
            requestedGroup = requestedGroup,
            onRequestedGroupShown = onRequestedGroupShown,
            onOpenStudioRender = onOpenStudioRender,
            onPlayTutorial = onPlayTutorial,
            onResetTutorials = onResetTutorials,
        )
    }
}

/** Show me: the real screen behind a tour card. The tour waits behind its Resume pill meanwhile. */
private fun showTutorialTarget(
    target: TutorialTarget,
    appState: GeodeAppState,
    openPaywall: () -> Unit,
) {
    when (target) {
        TutorialTarget.PLAYER -> appState.navigateTo(GeodeDestination.PLAYER)
        TutorialTarget.LIBRARY -> appState.navigateTo(GeodeDestination.LIBRARY)
        // The live view is Visuals now: full screen is its sheet hidden.
        TutorialTarget.VISUALS, TutorialTarget.LIVE_VIEW -> appState.navigateTo(GeodeDestination.VISUALS)
        TutorialTarget.STUDIO_RENDER -> appState.openStudioRender()
        TutorialTarget.SETTINGS -> appState.navigateTo(GeodeDestination.SETTINGS)
        TutorialTarget.PAYWALL -> openPaywall()
    }
}

private data class SearchTrackRow(
    val uri: String,
    val title: String,
    val subtitle: String,
    val fields: List<String>,
    val fromDevice: Boolean,
)

@Composable
fun SearchScreen(
    viewModel: PlayerViewModel,
    onClose: () -> Unit,
) {
    val libraryViewModel: LibraryViewModel = geodeViewModel()
    var query by rememberSaveable { mutableStateOf("") }
    var debounced by rememberSaveable { mutableStateOf("") }
    val library by libraryViewModel.library.collectAsStateWithLifecycle()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val deviceTracks by libraryViewModel.deviceTracks.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { libraryViewModel.refreshDeviceTracks() }
    LaunchedEffect(query) {
        if (query.isNotBlank()) delay(250)
        debounced = query
    }
    val dismiss = rememberPredictiveDismiss(onDismiss = onClose)

    val terms = remember(debounced) { SearchMatcher.terms(debounced) }
    val trackResults =
        remember(terms, deviceTracks, library.tracks) {
            val candidates =
                deviceTracks.map { t ->
                    SearchTrackRow(
                        uri = t.uri,
                        title = t.title,
                        subtitle = listOf(t.artist, t.album).filter { it.isNotBlank() }.joinToString(" · "),
                        fields = listOf(t.title, t.artist, t.album, t.folder),
                        fromDevice = true,
                    )
                } +
                    library.tracks.map { t ->
                        SearchTrackRow(
                            uri = t.uri,
                            title = t.title,
                            subtitle = listOf(t.artist, t.album).filter { it.isNotBlank() }.joinToString(" · "),
                            fields = listOf(t.title, t.artist, t.album, t.genre),
                            fromDevice = false,
                        )
                    }
            SearchMatcher.filterTracks(
                terms = terms,
                items = candidates,
                uriOf = { it.uri },
                fieldsOf = { it.fields },
                preferred = { it.fromDevice },
            )
        }
    val playlistResults =
        remember(terms, library.playlists) {
            library.playlists.filter { SearchMatcher.matches(terms, listOf(it.name)) }
        }
    val presetResults =
        remember(terms, viz.presets) {
            viz.presets.filter { SearchMatcher.matches(terms, listOf(it.name)) }
        }

    Box(
        Modifier
            .fillMaxSize()
            .dismissTransform(dismiss)
            .background(MaterialTheme.colorScheme.background)
            // The overlay owns the window: a tap in its margins must not reach the screen or the
            // tab bar under it.
            .pointerInput(Unit) { detectTapGestures { } },
    ) {
        Column(
            Modifier
                .fillMaxSize()
                // The results list runs to the bottom, under the keyboard that typing opens.
                .safeDrawingPadding()
                .padding(16.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(
                    value = query,
                    onValueChange = { query = it },
                    modifier = Modifier.weight(1f),
                    placeholder = { Text(stringResource(R.string.search_placeholder)) },
                    singleLine = true,
                )
                IconButton(onClick = onClose) { StoneIconArt(StoneIcon.CLOSE, stringResource(R.string.search_close)) }
            }
            LazyColumn(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                if (terms.isEmpty()) {
                    item {
                        Text(
                            stringResource(R.string.search_hint),
                            Modifier.padding(vertical = 16.dp),
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                } else {
                    if (trackResults.isNotEmpty()) {
                        item {
                            SearchHeading(
                                stringResource(R.string.search_heading_tracks, trackResults.size),
                                Modifier.padding(top = 8.dp),
                            )
                        }
                        items(trackResults, key = { "t:${it.uri}" }) { t ->
                            Row(
                                Modifier
                                    .fillMaxWidth()
                                    .clickable {
                                        viewModel.playFrom(
                                            trackResults.map { r -> QueueTrack(r.uri, r.title, r.subtitle) },
                                            t.uri,
                                        )
                                        onClose()
                                    },
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Column(Modifier.weight(1f).padding(vertical = 8.dp)) {
                                    Text(t.title, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                    if (t.subtitle.isNotBlank()) {
                                        Text(
                                            t.subtitle,
                                            style = MaterialTheme.typography.labelSmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                                            maxLines = 1,
                                            overflow = TextOverflow.Ellipsis,
                                        )
                                    }
                                }
                                IconButton(onClick = { viewModel.enqueue(t.uri) }) {
                                    StoneIconArt(StoneIcon.QUEUE, stringResource(R.string.action_add_to_queue))
                                }
                            }
                        }
                    }
                    if (playlistResults.isNotEmpty()) {
                        item {
                            SearchHeading(
                                stringResource(R.string.search_heading_playlists, playlistResults.size),
                                Modifier.padding(top = 8.dp),
                            )
                        }
                        items(playlistResults) { pl ->
                            Column(
                                Modifier
                                    .fillMaxWidth()
                                    .clickable {
                                        libraryViewModel.playPlaylist(pl.name)
                                        onClose()
                                    }.padding(vertical = 8.dp),
                            ) {
                                Text(pl.name, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                Text(
                                    pluralStringResource(
                                        R.plurals.track_count,
                                        pl.trackUris.size,
                                        pl.trackUris.size,
                                    ),
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                )
                            }
                        }
                    }
                    if (presetResults.isNotEmpty()) {
                        item {
                            SearchHeading(
                                stringResource(R.string.search_heading_presets, presetResults.size),
                                Modifier.padding(top = 8.dp),
                            )
                        }
                        items(presetResults) { p ->
                            Text(
                                p.name,
                                Modifier
                                    .fillMaxWidth()
                                    .clickable {
                                        viewModel.applyPreset(p)
                                        onClose()
                                    }.padding(vertical = 8.dp),
                            )
                        }
                    }
                    if (trackResults.isEmpty() && playlistResults.isEmpty() && presetResults.isEmpty()) {
                        item {
                            Text(
                                stringResource(R.string.search_no_results, debounced),
                                Modifier.padding(vertical = 16.dp),
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }
            }
        }
    }
}

/** A result group's heading: an M3 title in the accent, marked as a heading for TalkBack. */
@Composable
private fun SearchHeading(
    text: String,
    modifier: Modifier = Modifier,
) {
    Text(
        text,
        modifier.semantics { heading() },
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.primary,
    )
}

@Composable
private fun PlaybackNoticeBanner(viewModel: PlayerViewModel) {
    val notice by viewModel.playbackNotice.collectAsStateWithLifecycle()
    val message = notice ?: return
    val dismissDescription = stringResource(R.string.notice_dismiss_description)

    LaunchedEffect(message) {
        kotlinx.coroutines.delay(NOTICE_VISIBLE_MS)
        viewModel.clearPlaybackNotice()
    }

    Box(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp)) {
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(14.dp))
                    .background(MaterialTheme.colorScheme.errorContainer)
                    .clickable { viewModel.clearPlaybackNotice() }
                    .semantics { liveRegion = LiveRegionMode.Polite }
                    .padding(horizontal = 14.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = message,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onErrorContainer,
                modifier = Modifier.weight(1f),
            )
            Spacer(Modifier.width(8.dp))
            Text(
                text = stringResource(R.string.notice_dismiss),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onErrorContainer,
                modifier =
                    Modifier
                        .clickable { viewModel.clearPlaybackNotice() }
                        .semantics { contentDescription = dismissDescription }
                        .padding(8.dp),
            )
        }
    }
}

private const val NOTICE_VISIBLE_MS = 8_000L
