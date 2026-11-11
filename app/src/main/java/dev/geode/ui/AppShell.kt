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
import androidx.compose.material3.minimumInteractiveComponentSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.layout.boundsInWindow
import androidx.compose.ui.layout.layout
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
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntRect
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.constrainHeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.offset
import androidx.compose.ui.unit.roundToIntRect
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
import dev.geode.model.QueueTrack
import dev.geode.model.TourChapter
import dev.geode.model.TourPlace
import dev.geode.model.TourSpot
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.render.VisualizerView
import dev.geode.render.scene.SceneIds
import dev.geode.ui.cast.CastHost
import dev.geode.ui.pro.PaywallHost
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.theme.GeodeMotionScheme
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.VisualAccent
import dev.geode.ui.theme.floatingGlass
import dev.geode.ui.theme.geodeMotionScheme
import dev.geode.ui.theme.rememberSystemReducedMotion
import dev.geode.ui.tour.LocalTourNudge
import dev.geode.ui.tour.TourHost
import dev.geode.ui.tour.TourNav
import dev.geode.ui.tour.TourViewModel
import dev.geode.ui.tour.tourTarget
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
    val tourViewModel: TourViewModel = geodeViewModel()
    val nudge = remember(tourViewModel) { tourViewModel::nudge }
    CompositionLocalProvider(LocalTourNudge provides nudge) { AppContent(tourViewModel) }
}

@Composable
private fun AppContent(tourViewModel: TourViewModel) {
    val viewModel: PlayerViewModel = geodeViewModel()
    val settingsViewModel: SettingsViewModel = geodeViewModel()
    val paywallViewModel: PaywallViewModel = geodeViewModel()
    val context = LocalContext.current
    val visualizerView = remember { VisualizerView(context) }
    val gui by settingsViewModel.guiPrefs.collectAsStateWithLifecycle()
    ConnectedDisplayWatch()
    val appState = rememberGeodeAppState()
    // Read on IO: at cold start this is the first touch of the general prefs file, and a
    // SharedPreferences read blocks until the whole file has been parsed. Null until it lands,
    // and nothing below that depends on it (the notice, setup, the intro) draws until then.
    val bootAnimEnabled by produceState<Boolean?>(null) {
        value = withContext(Dispatchers.IO) { BootAnimationStore(GeodePrefsFiles(context).general).load() }
    }
    val state by viewModel.uiState.collectAsStateWithLifecycle()
    // Not delegated: only the mini player's seek bar and progress line read .value, in their own scope.
    val position = viewModel.positionMs.collectAsStateWithLifecycle()
    // A connected display shows the visual alone while the switch is on and Cast's entitlement allows it.
    val proGate by paywallViewModel.gate.collectAsStateWithLifecycle()
    val secondScreen = ConnectedDisplays.target(wanted = gui.secondScreen, entitled = proGate.allows(ProFeature.CAST))
    val onSecondScreen = secondScreen != null
    if (secondScreen != null) {
        SecondScreenCanvas(secondScreen, visualizerView)
    }
    StageRunEffect(visualizerView, onSecondScreen, viewModel::onDirectorStyleMoment)
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
    val vizState = viewModel.vizState.collectAsStateWithLifecycle()
    // The accent follows the visual: the middle of its palette's hues; MilkDrop paints its own.
    // Derived, so the root recomposes when the accent changes, not on every Customize slider tick.
    val accentTarget by remember {
        derivedStateOf {
            val viz = vizState.value
            if (viz.sceneId == SceneIds.MILKDROP) VisualAccent.Fallback else paletteAccent(viz.params.paletteBase, viz.params.paletteRange)
        }
    }
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
    val tour by tourViewModel.uiState.collectAsStateWithLifecycle()
    // A pointer lights real controls, so the ones that fade stay lit while it is up.
    // Its sleep timer waits while the boot intro or a first-run page covers the shell.
    val shellCovered = (bootAnimEnabled != false && !appState.bootDone) || !gui.safetyAcknowledged || !gui.setupDone
    val chrome = rememberShellChrome(gui.miniPlayerFadeSeconds, inPip, tour.step != null, shellCovered, appState.dest, state.title)
    var dockHeight by remember { mutableStateOf(0.dp) }
    val visualsReserve by animateDpAsState(dockHeight, motion.defaultSpatialSpec(), label = "dock")
    CrystalMaterialTheme(gui = gui, accentSource = accent) {
        val miniPlayer: @Composable (Modifier) -> Unit = { modifier ->
            FloatingMiniPlayer(
                title = state.title,
                artist = state.artist,
                artworkUri = viewModel.currentTrackUri(),
                isPlaying = state.isPlaying,
                positionMs = { position.value },
                durationMs = state.durationMs,
                onSeek = viewModel::seekTo,
                onOpenLibrary = { appState.navigateTo(GeodeDestination.LIBRARY) },
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
                onOpenLibrary = { appState.navigateTo(GeodeDestination.LIBRARY) },
                onOpenPlayer = { appState.navigateTo(GeodeDestination.PLAYER) },
                onHide = chrome::hide,
                onPrevious = viewModel::previous,
                onPlayPause = viewModel::togglePlayPause,
                onNext = viewModel::next,
                modifier = modifier,
            )
        }
        // Remembered: a fresh list each recomposition would recompose the tab bar and rail with it.
        val navLabels = GeodeDestination.entries.map { stringResource(it.labelRes) }
        val navEntries =
            remember(navLabels) {
                GeodeDestination.entries.mapIndexed { i, destination ->
                    NavEntry(destination, DockItem(navLabels[i], destination.icon, destination.tourSpot))
                }
            }
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
                                    requestedPage = appState.visualsPageRequest,
                                    onRequestedPageShown = { appState.visualsPageRequest = null },
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
                                onPlayTutorial = tourViewModel::replay,
                                onResetTutorials = tourViewModel::reset,
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
            // The aspect ratio and the source rect hint follow the canvas: after first layout, a
            // rotation or a window resize (a snapshotFlow, so a bounds change never recomposes AppRoot).
            LaunchedEffect(Unit) {
                snapshotFlow { VisualizerPipCoordinator.canvasBoundsPx }
                    .collect { context.findMainActivity()?.refreshPipParams() }
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
                        appState.navigateTo(GeodeDestination.PLAYER)
                        // No Pro offer here: the tour makes it, after the first look at Visuals.
                    },
                )
            }
            // Held back while the boot intro, the notice, setup, the paywall or a PiP window is up.
            val paywall by paywallViewModel.uiState.collectAsStateWithLifecycle()
            TourHost(
                viewModel = tourViewModel,
                state = tour,
                place = appState.dest.tourPlace,
                playing = state.isPlaying,
                blocked =
                    (bootAnimEnabled != false && !appState.bootDone) ||
                        !gui.safetyAcknowledged ||
                        !gui.setupDone ||
                        paywall.visible ||
                        inPip,
                onNavigate = { showTourScreen(it, appState) },
                onOfferPro = { paywallViewModel.open(PaywallTrigger.Onboarding) },
            )
            // Drawn while the pref is still loading too (null): BootIntro opens on the splash's
            // last frame, so it covers that gap instead of letting the app flash through. If the
            // pref then lands false, it ends there, just as the splash would have.
            if (bootAnimEnabled != false && !appState.bootDone) {
                BootIntro(onDone = { appState.bootDone = true })
            }
            RecordSaveHost()
            PaywallHost(reducedMotion = reducedMotion)
            CastHost()
            SharedLinkConfirm()
            MilkFileOpened(appState)
        }
    }
}

/**
 * The phone shell. Every screen fills the window; the mini player and the tab bar float over the
 * bottom of it as one slab of matte glass ([DockSlab] around [FloatingMiniPlayer] and
 * [FloatingTabBar]), inset from the edges.
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
    val tabItems = remember(navEntries) { navEntries.map { it.item } }
    var dockDrawn by remember { mutableStateOf(0.dp) }
    Box(Modifier.fillMaxSize()) {
        val navInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
        // The dock as drawn (the tab bar, and the mini player or its handle above it), measured
        // every frame of their expand and shrink: the screen above moves up with the mini player
        // instead of being covered by it (owner: no overlap), and never jumps. Never less than the
        // bar's own place, so the first frame, before the dock is measured, is not padded short.
        // A provider, not a value: dockDrawn changes every frame of the mini player's expand and
        // shrink, so it is read in the layout phase (dockBottomPadding, DockInsets) and only Player,
        // which sizes its content by it, reads it in composition.
        val bottom: () -> Dp = { maxOf(dockDrawn, navInset + barHeight + DOCK_GAP) + DOCK_GAP }
        val latestBottom by rememberUpdatedState(bottom)
        val dockInsets = remember { DockInsets { latestBottom() } }
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
                            .dockBottomPadding { if (padsItself) 0.dp else latestBottom() }
                            .consumeWindowInsets(dockInsets)
                            .imePadding()
                    },
                ),
        ) {
            CompositionLocalProvider(
                LocalDockReserve provides if (padsItself) bottom() else 0.dp,
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
                .onSizeChanged { dockDrawn = with(density) { it.height.toDp() } }
                .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Horizontal))
                .navigationBarsPadding()
                .padding(horizontal = GeodeSpacing.EdgeInset)
                .padding(bottom = DOCK_GAP),
        ) {
            // Asleep, the player leaves a handle above the slab. It and the card trade places on one
            // spring, so the dock's height (and the screen's padding) moves once, smoothly.
            AnimatedVisibility(!onVisuals, enter = fadeIn(dockFade), exit = fadeOut(dockFade)) {
                AnimatedVisibility(
                    showsPlayer && !chrome.awake,
                    enter = fadeIn(dockFade) + expandVertically(dockSize),
                    exit = fadeOut(dockFade) + shrinkVertically(dockSize),
                ) {
                    Box(Modifier.padding(bottom = GeodeSpacing.S)) { MiniPlayerHandle(onWake = chrome::wake) }
                }
            }
            // With the player docked on Visuals the options' glass runs down behind the bar
            // (VisualsHub), so sheet, player and tabs are one slab there too.
            val onOptionsGlass = onVisuals && showsPlayer
            // The slab only ever fades in place: on Visuals it stays while the options are up (they
            // stand on it, the player docked to them) and otherwise fades with the docked player;
            // on the options' glass it also goes when a drop-in fades them out. Elsewhere it is
            // always there.
            val slabShown =
                when {
                    !onVisuals -> true
                    onOptionsGlass && chrome.optionsHidden -> false
                    else -> chrome.awake || chrome.docked
                }
            AnimatedVisibility(
                slabShown,
                enter = fadeIn(dockFade),
                exit = fadeOut(dockFade),
            ) {
                DockSlab(
                    joined = !onVisuals && showsPlayer && chrome.awake,
                    barHeight = barHeight,
                    glass = !onOptionsGlass,
                ) {
                    // Into and out of Visuals the floating player only fades, at its full size and in
                    // place, while the docked one fades in over the same spot: one player changing
                    // shape, not a card that shrinks into the tab bar (owner: it read as loose).
                    AnimatedVisibility(!onVisuals, enter = fadeIn(dockFade), exit = fadeOut(dockFade)) {
                        AnimatedVisibility(
                            showsPlayer && chrome.awake,
                            enter = fadeIn(dockFade) + expandVertically(dockSize),
                            exit = fadeOut(dockFade) + shrinkVertically(dockSize),
                        ) {
                            miniPlayer(Modifier)
                        }
                    }
                    FloatingTabBar(
                        items = tabItems,
                        // A destination can be hidden while still being the current one — reaching
                        // Studio from a track menu, say. Fall back to the first tab rather than -1.
                        selected = navEntries.indexOfFirst { it.destination == appState.dest }.coerceAtLeast(0),
                        onSelect = { appState.navigateTo(navEntries[it].destination) },
                        modifier =
                            Modifier.onSizeChanged {
                                if (it.height > 0) {
                                    barHeight = with(density) { it.height.toDp() }
                                    chrome.tabBarHeightPx = it.height
                                }
                            },
                    )
                }
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
    val dockSize = LocalGeodeMotion.current.defaultSpatialSpec<IntSize>()
    val onVisuals = appState.dest == GeodeDestination.VISUALS
    // safeDrawing rather than the status and navigation bars alone: in landscape the display
    // cutout sits on a side edge, where it would cover the rail or the content's far edge. It
    // includes the keyboard too, so a focused field stays above it. Visuals is the exception: its
    // canvas draws edge to edge behind the bars and the cutout, and only the rail, the stage
    // controls, the options and the dock take the insets themselves (#864).
    val railInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Start + WindowInsetsSides.Vertical)
    Row(Modifier.fillMaxSize().then(if (onVisuals) Modifier else Modifier.safeDrawingPadding())) {
        // A PiP window is the visual alone, with no rail beside it.
        if (!pip) {
            Box(if (onVisuals) Modifier.windowInsetsPadding(railInsets) else Modifier) {
                NavRail(navEntries, appState, centerRail)
            }
        }
        // The rail took the start edge's inset, so the controls beside it must not take it again.
        val besideRail =
            if (onVisuals && !pip) Modifier.consumeWindowInsets(WindowInsets.safeDrawing.only(WindowInsetsSides.Start)) else Modifier
        Box(Modifier.weight(1f).fillMaxHeight().then(besideRail)) {
            var dockDrawn by remember { mutableStateOf(0.dp) }
            // Visuals is the visual under its own options; every other screen ends above the player.
            Box(Modifier.fillMaxSize().dockBottomPadding { if (onVisuals) 0.dp else dockDrawn }) { content() }
            Column(
                Modifier
                    .align(Alignment.BottomCenter)
                    .widthIn(max = MINI_PLAYER_MAX_WIDTH)
                    .fillMaxWidth()
                    .onSizeChanged {
                        dockDrawn = with(density) { it.height.toDp() }
                        onDock(dockDrawn)
                    }.windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.End + WindowInsetsSides.Bottom))
                    .padding(GeodeSpacing.EdgeInset),
            ) {
                AnimatedVisibility(
                    showMiniPlayer && chrome.awake,
                    enter = fadeIn(dockFade) + expandVertically(dockSize),
                    exit = fadeOut(dockFade) + shrinkVertically(dockSize),
                ) {
                    // No tab bar under it here (the rail has the tabs), so it is a card of its own.
                    miniPlayer(Modifier.floatingGlass())
                }
                AnimatedVisibility(
                    showMiniPlayer && !chrome.awake && !onVisuals,
                    enter = fadeIn(dockFade) + expandVertically(dockSize),
                    exit = fadeOut(dockFade) + shrinkVertically(dockSize),
                ) {
                    MiniPlayerHandle(onWake = chrome::wake)
                }
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
                modifier = item.tour?.let { Modifier.tourTarget(it) } ?: Modifier,
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

/** Bottom padding whose size is read when the layout runs, so a value that changes every frame never recomposes. */
private fun Modifier.dockBottomPadding(bottom: () -> Dp): Modifier =
    layout { measurable, constraints ->
        val pad = bottom().roundToPx()
        val placeable = measurable.measure(constraints.offset(vertical = -pad))
        layout(placeable.width, constraints.constrainHeight(placeable.height + pad)) { placeable.place(0, 0) }
    }

/** The dock's bottom as window insets to consume, read lazily so its changes stay out of composition. */
private class DockInsets(
    private val bottom: () -> Dp,
) : WindowInsets {
    override fun getLeft(
        density: Density,
        layoutDirection: LayoutDirection,
    ) = 0

    override fun getTop(density: Density) = 0

    override fun getRight(
        density: Density,
        layoutDirection: LayoutDirection,
    ) = 0

    override fun getBottom(density: Density) = with(density) { bottom().roundToPx() }
}

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
    onPlayTutorial: (List<TourChapter>) -> Unit,
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

private fun showTourScreen(
    screen: TourNav,
    appState: GeodeAppState,
) {
    when (screen) {
        TourNav.LIBRARY -> appState.navigateTo(GeodeDestination.LIBRARY)
        TourNav.VISUALS -> appState.openVisualsPage(VisualsPage.STYLES)
        TourNav.CUSTOMIZE -> appState.openVisualsPage(VisualsPage.CUSTOMIZE)
        TourNav.STUDIO -> appState.openStudioRender()
    }
}

private val GeodeDestination.tourSpot: TourSpot?
    get() =
        when (this) {
            GeodeDestination.LIBRARY -> TourSpot.NAV_LIBRARY
            GeodeDestination.VISUALS -> TourSpot.NAV_VISUALS
            else -> null
        }

private val GeodeDestination.tourPlace: TourPlace
    get() =
        when (this) {
            GeodeDestination.LIBRARY -> TourPlace.LIBRARY
            GeodeDestination.VISUALS -> TourPlace.VISUALS
            else -> TourPlace.ELSEWHERE
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
    // Search opens ready to type: the field takes focus, which raises the keyboard.
    val searchFocus = remember { FocusRequester() }
    LaunchedEffect(Unit) { searchFocus.requestFocus() }

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
                    modifier =
                        Modifier
                            .weight(1f)
                            .focusRequester(searchFocus),
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
                        .minimumInteractiveComponentSize()
                        .clickable { viewModel.clearPlaybackNotice() }
                        .semantics { contentDescription = dismissDescription }
                        .padding(8.dp),
            )
        }
    }
}

private const val NOTICE_VISIBLE_MS = 8_000L
