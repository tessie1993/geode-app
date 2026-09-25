package dev.geode.ui

import android.content.Context
import android.content.ContextWrapper
import android.widget.Toast
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.spring
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AutoAwesome
import androidx.compose.material.icons.filled.Headphones
import androidx.compose.material.icons.filled.LibraryMusic
import androidx.compose.material.icons.filled.MovieCreation
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.SkipNext
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.SaveableStateHolder
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.MainActivity
import dev.geode.R
import dev.geode.data.BootAnimationStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.nav.DeepLink
import dev.geode.nav.Destination
import dev.geode.nav.Gate
import dev.geode.nav.Navigator
import dev.geode.nav.Overlay
import dev.geode.nav.Presentation
import dev.geode.nav.Routes
import dev.geode.nav.Section
import dev.geode.nav.connect.MotionPolicy
import dev.geode.nav.connect.NavConnectors
import dev.geode.render.VisualizerView
import dev.geode.ui.opaline.OpalineButton
import dev.geode.ui.opaline.OpalineIconButton
import dev.geode.ui.opaline.OpalinePage
import dev.geode.ui.opaline.OpalineRow
import dev.geode.ui.opaline.OpalineSceneHost
import dev.geode.ui.opaline.OpalineTheme
import dev.geode.ui.opaline.opalinePart
import dev.geode.ui.opaline.opalineReady
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.withContext

@Composable
fun OpalineApp(
    navigator: Navigator,
    connectors: NavConnectors,
    motion: MutableStateFlow<MotionPolicy>,
) {
    val player: PlayerViewModel = geodeViewModel()
    val settings: SettingsViewModel = geodeViewModel()
    val gui by settings.guiPrefs.collectAsStateWithLifecycle()
    val loaded by settings.userDataLoaded.collectAsStateWithLifecycle()
    val nav by navigator.state.collectAsStateWithLifecycle()
    val playback by player.uiState.collectAsStateWithLifecycle()
    val context = LocalContext.current
    val renderer = remember { VisualizerView(context) }
    val lifecycle = LocalLifecycleOwner.current
    val externalDisplay = rememberExternalDisplay()
    val secondScreen = if (gui.secondScreen) externalDisplay else null
    val pipShowing = VisualizerPipCoordinator.visualizerShowing
    val routeStateHolder = rememberSaveableStateHolder()

    LaunchedEffect(pipShowing, playback.isPlaying, gui.autoEnterPip) {
        context.findMainActivity()?.refreshPipParams()
    }
    var resumed by remember { mutableStateOf(lifecycle.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) }
    var started by remember { mutableStateOf(lifecycle.lifecycle.currentState.isAtLeast(Lifecycle.State.STARTED)) }
    val inPictureInPicture = VisualizerPipCoordinator.inPictureInPicture
    LaunchedEffect(renderer, resumed, started, inPictureInPicture) {
        if (resumed || (started && inPictureInPicture)) renderer.onResume() else renderer.onPause()
    }
    DisposableEffect(lifecycle, renderer) {
        val observer =
            LifecycleEventObserver { _, event ->
                when (event) {
                    Lifecycle.Event.ON_START -> started = true
                    Lifecycle.Event.ON_RESUME -> resumed = true
                    Lifecycle.Event.ON_PAUSE -> resumed = false
                    Lifecycle.Event.ON_STOP -> started = false
                    else -> Unit
                }
            }
        lifecycle.lifecycle.addObserver(observer)
        onDispose {
            lifecycle.lifecycle.removeObserver(observer)
            renderer.onPause()
        }
    }
    LaunchedEffect(gui.reducedMotion, gui.liquidMotion) {
        motion.value = MotionPolicy(gui.reducedMotion, gui.liquidMotion.coerceIn(0f, 1f))
    }
    VisualizerEngineBindings(player, renderer)
    if (secondScreen != null) SecondScreenCanvas(secondScreen, renderer)
    OpalineTheme(gui) {
        OpalineSceneHost(
            modifier = Modifier.fillMaxSize(),
            reducedMotion = gui.reducedMotion,
            section = nav.section.name.lowercase(),
            active = resumed && nav.overlay != Overlay.Visualizer,
        ) {
            Box(Modifier.fillMaxSize()) {
                // The engine view has a single parent. Remove the shell while immersive mode
                // owns it, while retaining each section's saveable page state above the shell.
                if (nav.overlay != Overlay.Visualizer && nav.gate == null) {
                    OpalineShell(navigator, connectors, player, renderer, gui.reducedMotion, routeStateHolder)
                }
                when (nav.overlay.takeIf { nav.gate == null }) {
                    Overlay.Search ->
                        routeStateHolder.SaveableStateProvider("overlay:search") {
                            OpalineSearch(navigator, player)
                        }
                    Overlay.Visualizer -> OpalineImmersive(player, renderer, navigator, secondScreen?.name)
                    Overlay.Export ->
                        OpalineOverlaySheet({ navigator.close(Overlay.Export) }) {
                            OpalineExportOverlay(player, renderer, navigator)
                        }
                    null -> Unit
                }
                if (loaded) OpalineGates(navigator, settings, gui)
                OpalineCrashReport()
            }
        }
    }
    OpalineLinkHandler(navigator, loaded)
}

@Composable
private fun OpalineOverlaySheet(
    onDismiss: () -> Unit,
    content: @Composable () -> Unit,
) {
    BoxWithConstraints(Modifier.fillMaxSize().statusBarsPadding()) {
        Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.48f)).clickable(onClick = onDismiss))
        Box(
            Modifier
                .align(Alignment.BottomCenter)
                .fillMaxWidth()
                .heightIn(max = maxHeight * 0.9f)
                .navigationBarsPadding()
                .imePadding(),
        ) {
            content()
        }
    }
}

@Composable
private fun OpalineShell(
    navigator: Navigator,
    connectors: NavConnectors,
    player: PlayerViewModel,
    renderer: VisualizerView,
    reducedMotion: Boolean,
    stateHolder: SaveableStateHolder,
) {
    val nav by navigator.state.collectAsStateWithLifecycle()
    val gesture by navigator.backGesture.collectAsStateWithLifecycle()
    var direction by remember { mutableStateOf(1) }
    LaunchedEffect(navigator) {
        navigator.moves.collect { move ->
            val transition = connectors.transition(move)
            direction =
                when (transition.direction) {
                    dev.geode.nav.connect.Transition.Direction.BACKWARD,
                    dev.geode.nav.connect.Transition.Direction.RIGHT,
                    dev.geode.nav.connect.Transition.Direction.DOWN,
                    -> -1
                    else -> 1
                }
        }
    }
    val page = nav.stack.lastOrNull { it.presentation != Presentation.SHEET } ?: nav.section.root
    val frame = OpalineRouteFrame(nav.section, page)
    BoxWithConstraints(Modifier.fillMaxSize().statusBarsPadding()) {
        val wide = maxWidth >= 840.dp
        val sheetMaxHeight = maxHeight * 0.9f
        Row(Modifier.fillMaxSize()) {
            if (wide) OpalineDock(navigator, vertical = true)
            Column(Modifier.weight(1f)) {
                Box(
                    Modifier.weight(1f).fillMaxWidth().graphicsLayer {
                        if (!reducedMotion) {
                            val progress = gesture?.progress ?: 0f
                            translationX =
                                progress * size.width * if (gesture?.edge == dev.geode.nav.BackGesture.Edge.RIGHT) -0.14f else 0.14f
                            scaleX = 1f - 0.035f * progress
                            scaleY = scaleX
                        }
                    },
                ) {
                    AnimatedContent(
                        targetState = frame,
                        transitionSpec = {
                            if (reducedMotion) {
                                EnterTransition.None togetherWith ExitTransition.None
                            } else {
                                (fadeIn() + slideInHorizontally(spring(stiffness = 420f)) { it / 10 * direction }) togetherWith
                                    (fadeOut() + slideOutHorizontally { -it / 12 * direction })
                            }
                        },
                        label = "opalineNavigation",
                    ) { route ->
                        stateHolder.SaveableStateProvider(route.saveableKey) {
                            OpalineRoute(route.destination, navigator, player, renderer)
                        }
                    }
                }
                if (nav.current != Destination.Player.NowPlaying) OpalineMiniPlayer(player, navigator)
                if (!wide) OpalineDock(navigator, vertical = false)
            }
        }
        if (nav.current.presentation == Presentation.SHEET && nav.overlay == null && nav.gate == null) {
            Box(Modifier.fillMaxSize()) {
                Box(
                    Modifier
                        .fillMaxSize()
                        .background(Color.Black.copy(alpha = 0.48f * (1f - (gesture?.progress ?: 0f))))
                        .clickable { navigator.back() },
                )
                Box(
                    Modifier
                        .align(Alignment.BottomCenter)
                        .fillMaxWidth()
                        .heightIn(max = sheetMaxHeight)
                        .navigationBarsPadding()
                        .imePadding()
                        .graphicsLayer {
                            translationY = (gesture?.progress ?: 0f) * size.height * 0.8f
                        },
                ) {
                    stateHolder.SaveableStateProvider(OpalineRouteFrame(nav.section, nav.current).saveableKey) {
                        OpalineRoute(nav.current, navigator, player, renderer)
                    }
                }
            }
        }
    }
}

private data class OpalineRouteFrame(
    val section: Section,
    val destination: Destination,
) {
    // Shared track routes can be opened by more than one section. Their saved UI state belongs
    // to the invoking section, just like that section's back stack.
    val saveableKey: String get() = "${section.name}:${Routes.encode(destination)}"
}

@Composable
private fun OpalineRoute(
    destination: Destination,
    navigator: Navigator,
    player: PlayerViewModel,
    renderer: VisualizerView,
) {
    when (destination) {
        is Destination.Player -> OpalinePlayerRoute(destination, navigator, player)
        is Destination.Library -> OpalineLibraryRoute(destination, navigator, player)
        is Destination.Shared -> OpalineTrackRoute(destination, navigator)
        else -> OpalineCreativeRoute(destination, navigator, player, renderer)
    }
}

@Composable
internal fun sectionLabel(section: Section): String =
    stringResource(
        when (section) {
            Section.PLAYER -> R.string.opaline_listen
            Section.LIBRARY -> R.string.nav_library
            Section.VISUALS -> R.string.nav_visuals
            Section.STUDIO -> R.string.nav_studio
            Section.SETTINGS -> R.string.nav_settings
        },
    )

@Composable
private fun OpalineDock(
    navigator: Navigator,
    vertical: Boolean,
) {
    val nav by navigator.state.collectAsStateWithLifecycle()
    val modifier =
        if (vertical) {
            Modifier
                .width(104.dp)
                .fillMaxHeight()
                .navigationBarsPadding()
                .padding(10.dp)
        } else {
            Modifier.fillMaxWidth().navigationBarsPadding().padding(horizontal = 12.dp, vertical = 8.dp)
        }
    val items: @Composable () -> Unit = {
        Section.entries.forEach { section -> OpalineDockItem(section, section == nav.section, navigator) }
    }
    if (vertical) {
        Column(
            modifier.opalinePart("D03").selectableGroup().verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            items()
        }
    } else {
        Row(modifier.opalinePart("D03").selectableGroup().padding(8.dp), horizontalArrangement = Arrangement.SpaceEvenly) { items() }
    }
}

@Composable
private fun OpalineDockItem(
    section: Section,
    selected: Boolean,
    navigator: Navigator,
) {
    val view = LocalView.current
    val icon =
        when (section) {
            Section.PLAYER -> Icons.Default.Headphones
            Section.LIBRARY -> Icons.Default.LibraryMusic
            Section.VISUALS -> Icons.Default.AutoAwesome
            Section.STUDIO -> Icons.Default.MovieCreation
            Section.SETTINGS -> Icons.Default.Settings
        }
    Column(
        Modifier
            .width(58.dp)
            .opalinePart("A03", selected = selected)
            .selectable(selected = selected, role = Role.Tab, onClick = {
                view.performHapticFeedback(android.view.HapticFeedbackConstants.KEYBOARD_TAP)
                navigator.show(section)
            })
            .padding(vertical = 10.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(5.dp),
    ) {
        Icon(
            icon,
            null,
            Modifier.size(23.dp),
            tint = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Text(sectionLabel(section), style = MaterialTheme.typography.labelSmall, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

@Composable
private fun OpalineMiniPlayer(
    player: PlayerViewModel,
    navigator: Navigator,
) {
    val state by player.uiState.collectAsStateWithLifecycle()
    if (!state.hasMedia) return
    OpalineRow(
        title = state.title ?: stringResource(R.string.title_untitled),
        subtitle = state.artist.orEmpty(),
        onClick = { navigator.show(Section.PLAYER) },
        modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
        leading = { TrackArtwork(player.currentTrackUri(), Modifier.size(40.dp)) },
        trailing = {
            Row {
                OpalineIconButton(
                    if (state.isPlaying) Icons.Default.Pause else Icons.Default.PlayArrow,
                    stringResource(R.string.action_play_pause),
                    player::togglePlayPause,
                )
                OpalineIconButton(Icons.Default.SkipNext, stringResource(R.string.action_next), player::next)
            }
        },
    )
}

@Composable
private fun OpalineLinkHandler(
    navigator: Navigator,
    loaded: Boolean,
) {
    val link by navigator.pendingLink.collectAsStateWithLifecycle()
    val visuals: VisualsViewModel = geodeViewModel()
    val player: PlayerViewModel = geodeViewModel()
    val resources = LocalResources.current
    val context = LocalContext.current
    LaunchedEffect(link, loaded) {
        if (!loaded) return@LaunchedEffect
        when (val pending = link) {
            is DeepLink.Preset -> {
                val result = visuals.importSharedPreset(pending.link)
                val message =
                    if (result is PresetLinkImport.Imported) {
                        resources.getString(R.string.preset_link_imported, result.name)
                    } else {
                        resources.getString(R.string.preset_link_unreadable)
                    }
                Toast.makeText(context, message, Toast.LENGTH_LONG).show()
                navigator.consumeLink(pending)
            }
            is DeepLink.Template ->
                visuals.importSharedTemplate(pending.link) { result ->
                    val message =
                        when (result) {
                            is TemplateLinkImport.Imported -> resources.getString(R.string.template_link_imported, result.name)
                            is TemplateLinkImport.Replaced -> resources.getString(R.string.template_link_imported, result.name)
                            is TemplateLinkImport.Unreadable -> resources.getString(R.string.template_link_unreadable)
                        }
                    Toast.makeText(context, message, Toast.LENGTH_LONG).show()
                    navigator.consumeLink(pending)
                }
            is DeepLink.PlayFromSearch -> {
                player.playFromSearch(pending.query)
                navigator.consumeLink(pending)
            }
            null -> Unit
        }
    }
}

@Composable
private fun OpalineGates(
    navigator: Navigator,
    settings: SettingsViewModel,
    gui: GuiPrefs,
) {
    val context = LocalContext.current
    val nav by navigator.state.collectAsStateWithLifecycle()
    val boot = remember { BootAnimationStore(GeodePrefsFiles(context).general).load() }
    var bootDone by rememberSaveable { mutableStateOf(false) }
    var tutorialDismissed by rememberSaveable { mutableStateOf(false) }
    LaunchedEffect(boot, gui.safetyAcknowledged, gui.setupDone, gui.tutorialSeen, gui.tutorialOnFirstRun, bootDone) {
        val needed =
            when {
                boot && !bootDone -> Gate.BOOT
                !gui.safetyAcknowledged -> Gate.SAFETY
                !gui.setupDone -> Gate.SETUP
                !gui.tutorialSeen && gui.tutorialOnFirstRun && !tutorialDismissed -> Gate.TUTORIAL
                else -> null
            }
        if (needed != null) navigator.raiseGate(needed)
    }
    LaunchedEffect(nav.gate) {
        if (nav.gate == Gate.BOOT) {
            delay(if (gui.reducedMotion) 200 else 1100)
            bootDone = true
            navigator.clearGate(Gate.BOOT)
        }
        if (nav.gate == Gate.TUTORIAL) tutorialDismissed = true
        if (nav.gate == null && tutorialDismissed && !gui.tutorialSeen) settings.setGuiPrefs(gui.copy(tutorialSeen = true))
    }
    val gate = nav.gate ?: return
    Box(
        Modifier
            .fillMaxSize()
            .background(
                MaterialTheme.colorScheme.background.copy(alpha = if (opalineReady()) 0.3f else 0.97f),
            ).statusBarsPadding()
            .navigationBarsPadding(),
    ) {
        OpalinePage(
            title =
                stringResource(
                    when (gate) {
                        Gate.BOOT -> R.string.app_name
                        Gate.SAFETY -> R.string.opaline_safety_title
                        Gate.SETUP -> R.string.opaline_setup_title
                        Gate.TUTORIAL -> R.string.opaline_tour_title
                    },
                ),
            subtitle = stringResource(R.string.opaline_tagline),
        ) {
            Column(Modifier.weight(1f).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(24.dp)) {
                Spacer(Modifier.height(24.dp))
                Box(Modifier.fillMaxWidth().height(190.dp).opalinePart("C20"))
                Text(
                    stringResource(
                        when (gate) {
                            Gate.BOOT -> R.string.opaline_welcome
                            Gate.SAFETY -> R.string.opaline_safety_body
                            Gate.SETUP -> R.string.opaline_setup_body
                            Gate.TUTORIAL -> R.string.opaline_tour_body
                        },
                    ),
                    style = MaterialTheme.typography.bodyLarge,
                )
                when (gate) {
                    Gate.BOOT -> Unit
                    Gate.SAFETY -> {
                        OpalineButton(stringResource(R.string.opaline_reduce_motion), {
                            settings.setGuiPrefs(gui.copy(reducedMotion = true, safetyAcknowledged = true))
                            navigator.clearGate(gate)
                        })
                        OpalineButton(stringResource(R.string.opaline_continue), {
                            settings.setGuiPrefs(gui.copy(safetyAcknowledged = true))
                            navigator.clearGate(gate)
                        })
                    }
                    Gate.SETUP -> {
                        OpalineImportActions()
                        OpalineButton(stringResource(R.string.opaline_enter), {
                            settings.setGuiPrefs(gui.copy(setupDone = true))
                            navigator.clearGate(gate)
                        })
                    }
                    Gate.TUTORIAL ->
                        OpalineButton(stringResource(R.string.opaline_start_listening), {
                            settings.setGuiPrefs(gui.copy(tutorialSeen = true))
                            navigator.clearGate(gate)
                        })
                }
            }
        }
    }
}

@Composable
private fun OpalineCrashReport() {
    val context = LocalContext.current
    var report by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(Unit) {
        report =
            withContext(Dispatchers.IO) {
                val file = java.io.File(context.filesDir, "crash-latest.txt")
                if (file.isFile) file.inputStream().use { String(it.readBytes().take(65536).toByteArray()) } else null
            }
    }
    val text = report ?: return
    AlertDialog(
        onDismissRequest = { report = null },
        title = { Text(stringResource(R.string.crash_dialog_title)) },
        text = { Text(text.take(600)) },
        confirmButton = {
            OpalineButton(stringResource(R.string.action_dismiss), {
                java.io.File(context.filesDir, "crash-latest.txt").delete()
                report = null
            })
        },
        dismissButton = {
            OpalineButton(stringResource(R.string.action_copy), {
                context
                    .getSystemService(
                        android.content.ClipboardManager::class.java,
                    ).setPrimaryClip(android.content.ClipData.newPlainText("Geode", text))
            })
        },
    )
}

internal tailrec fun Context.findMainActivity(): MainActivity? =
    when (this) {
        is MainActivity -> this
        is ContextWrapper -> baseContext.findMainActivity()
        else -> null
    }
