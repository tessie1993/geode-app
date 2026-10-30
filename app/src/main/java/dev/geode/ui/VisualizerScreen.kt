package dev.geode.ui

import android.os.Build
import android.view.accessibility.AccessibilityManager
import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.calculateCentroid
import androidx.compose.foundation.gestures.calculateCentroidSize
import androidx.compose.foundation.gestures.calculatePan
import androidx.compose.foundation.gestures.calculateRotation
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.PictureInPictureAlt
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.adaptive.currentWindowAdaptiveInfo
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.PointerEvent
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.layout.boundsInWindow
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.util.fastAny
import androidx.compose.ui.util.fastForEach
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.window.core.layout.WindowSizeClass
import dev.geode.R
import dev.geode.render.TouchField
import dev.geode.render.VisualizerRenderer
import dev.geode.render.VisualizerView
import dev.geode.render.scene.TouchTransform
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import kotlinx.coroutines.delay
import kotlin.math.PI
import kotlin.math.abs

@Composable
fun VisualizerScreen(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    externalDisplayName: String? = null,
    onCollapse: () -> Unit,
    onOpenVisuals: () -> Unit,
) {
    val settingsViewModel: SettingsViewModel = geodeViewModel()
    val state by viewModel.uiState.collectAsStateWithLifecycle()
    // Not delegated: only the seek bar, the clock and the lyrics read .value.
    val position = viewModel.positionMs.collectAsStateWithLifecycle()
    val autoMode by viewModel.autoMode.collectAsStateWithLifecycle()
    val gui by settingsViewModel.guiPrefs.collectAsStateWithLifecycle()
    val waveform by viewModel.waveform.collectAsStateWithLifecycle()
    val lyrics by viewModel.lyrics.collectAsStateWithLifecycle()
    val queue by viewModel.queue.collectAsStateWithLifecycle()
    val favourites by viewModel.favourites.collectAsStateWithLifecycle()
    val abLoop by viewModel.abLoop.collectAsStateWithLifecycle()
    val external by viewModel.externalAudio.collectAsStateWithLifecycle()
    val currentUri = remember(state.title, state.artist) { viewModel.currentTrackUri() }
    val isFavourite = currentUri != null && currentUri in favourites
    var panel by remember { mutableStateOf(PlayerPanel.TRANSPORT) }
    // The chrome sits on live visuals that can go white, so its glass never drops below the point
    // where its text still reads (about 7:1 over a white frame at this floor; 1.4:1 at 0.25). It
    // hides itself after a few seconds, so a denser panel costs little of the view.
    val chromeAlpha = maxOf(gui.barOpacity, CHROME_MIN_ALPHA)
    // A phone on its side: the transport folds into two rows so it doesn't cover half the visuals.
    val shortWindow =
        !currentWindowAdaptiveInfo().windowSizeClass.isHeightAtLeastBreakpoint(WindowSizeClass.HEIGHT_DP_MEDIUM_LOWER_BOUND)
    val toggleControlsLabel =
        stringResource(if (controlsVisible) R.string.a11y_hide_controls else R.string.a11y_show_controls)
    var controlsVisible by remember { mutableStateOf(true) }
    val context = LocalContext.current
    // Picture-in-picture shrinks this screen to a window with no room for chrome, and no way for
    // the person to tap a button in it anyway — every control, not just the transport bar, is
    // gone while the platform reports the mode, and the last state comes back on exit.
    val inPip = VisualizerPipCoordinator.inPictureInPicture
    val showChrome = controlsVisible && !inPip
    // Bumped on every finger down and up anywhere on the screen, chrome included; each bump
    // restarts the auto-hide countdown. Moves are not counted, so a long drag recomposes
    // nothing — the lift that ends it restarts the countdown instead.
    var touches by remember { mutableIntStateOf(0) }

    ImmersiveSystemBars()
    AutoHideChrome(
        // Not while paused (the chrome is how playback resumes), and not while the lyrics or
        // queue panel is open, which someone is reading rather than watching past.
        armed = controlsVisible && state.isPlaying && panel == PlayerPanel.TRANSPORT,
        touches = touches,
        onHide = { controlsVisible = false },
    )

    val dismiss = rememberPredictiveDismiss(onDismiss = onCollapse)

    Box(
        Modifier
            .fillMaxSize()
            .dismissTransform(dismiss)
            .background(Color.Black)
            .pointerInput(Unit) {
                // The Initial pass reaches this ancestor before any child, and nothing is
                // consumed, so taps on the transport buttons count without being stolen.
                awaitPointerEventScope {
                    while (true) {
                        val type = awaitPointerEvent(PointerEventPass.Initial).type
                        if (type == PointerEventType.Press || type == PointerEventType.Release) touches++
                    }
                }
            }.pointerInput(Unit) {
                detectTapGestures(onTap = { controlsVisible = !controlsVisible })
            }.semantics {
                // The tap above is invisible to TalkBack; with the chrome hidden, this action is
                // the only way a screen reader user can bring the controls back.
                onClick(label = toggleControlsLabel) {
                    controlsVisible = !controlsVisible
                    true
                }
            }.visualizerTouch(
                renderer = visualizerView.visualizerRenderer,
                smear = gui.touchSmear,
                smearStrength = gui.touchSmearStrength,
                transform = gui.touchTransform,
                onTransform = viewModel::nudgeTransform,
            ),
    ) {
        if (externalDisplayName == null) {
            VisualizerCanvasHost(
                visualizerView,
                Modifier
                    .fillMaxSize()
                    .onGloballyPositioned { coords ->
                        // Window-space, not local: PictureInPictureParams.setSourceRectHint wants
                        // the same coordinate space the platform draws the Activity's window in.
                        val bounds = coords.boundsInWindow()
                        VisualizerPipCoordinator.canvasBoundsPx =
                            android.graphics.Rect(
                                bounds.left.toInt(),
                                bounds.top.toInt(),
                                bounds.right.toInt(),
                                bounds.bottom.toInt(),
                            )
                    },
            )
        } else {
            CrystalBackground(Modifier.fillMaxSize(), reducedMotion = gui.reducedMotion)
            Column(
                Modifier.align(Alignment.Center).padding(32.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                CrystalOverline(stringResource(R.string.second_screen_showing_on))
                Text(
                    externalDisplayName,
                    style = MaterialTheme.typography.headlineSmall,
                    color = MaterialTheme.colorScheme.onBackground,
                )
                Text(
                    stringResource(R.string.second_screen_explainer),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(top = 8.dp),
                )
            }
        }

        if (showChrome) {
            VisualizerTopBar(
                chromeAlpha = chromeAlpha,
                externalDisplayName = externalDisplayName,
                state = state,
                external = external,
                isFavourite = isFavourite,
                onCollapse = onCollapse,
                onEnterPip = { context.findMainActivity()?.enterVisualizerPip() },
                onToggleFavourite = { viewModel.toggleFavourite() },
            )

            if (panel != PlayerPanel.TRANSPORT) {
                PlayerPanelSurface(
                    Modifier
                        .align(Alignment.Center)
                        .fillMaxWidth()
                        .padding(horizontal = 12.dp)
                        .padding(top = 88.dp, bottom = 210.dp)
                        .fillMaxSize(),
                ) {
                    when (panel) {
                        PlayerPanel.LYRICS ->
                            LyricsPanel(
                                lyrics = lyrics,
                                positionMs = { position.value },
                                onSeek = { viewModel.seekToMs(it) },
                                modifier = Modifier.fillMaxSize(),
                            )
                        else ->
                            QueuePanel(
                                queue = queue,
                                favourites = favourites,
                                onPlayIndex = viewModel::playQueueIndex,
                                onMoveUp = { viewModel.moveQueueItem(it, it - 1) },
                                onRemove = viewModel::removeQueueItem,
                                modifier = Modifier.fillMaxSize(),
                            )
                    }
                }
            }

            Box(
                Modifier
                    .align(Alignment.BottomCenter)
                    .fillMaxWidth()
                    .height(200.dp)
                    .glassScrim(),
            )

            Box(
                modifier =
                    Modifier
                        .align(Alignment.BottomCenter)
                        // A tablet's full width spreads five buttons a hand-span apart.
                        .widthIn(max = TRANSPORT_MAX_WIDTH)
                        .fillMaxWidth()
                        .windowInsetsPadding(
                            WindowInsets.safeDrawing.only(WindowInsetsSides.Bottom + WindowInsetsSides.Horizontal),
                        ).padding(horizontal = 12.dp)
                        .padding(bottom = if (shortWindow) 8.dp else 16.dp)
                        .crystalPanel(
                            chromeAlpha,
                            MaterialTheme.colorScheme.surface,
                            MaterialTheme.colorScheme.primary,
                            corner = 24.dp,
                            glowStrength = 0.8f,
                        ),
            ) {
                VisualizerTransport(
                    viewModel = viewModel,
                    state = state,
                    position = { position.value },
                    waveform = waveform,
                    abLoop = abLoop,
                    autoMode = autoMode,
                    queueSize = queue.tracks.size,
                    panel = panel,
                    onPanel = { panel = it },
                    onOpenVisuals = onOpenVisuals,
                    shortWindow = shortWindow,
                )
            }
        }
    }
}

/**
 * The live view's transport: seek, playback buttons, the Visuals and auto-visuals shortcuts, and
 * the panel chips. On a short window (a phone on its side) the buttons, shortcuts and chips share
 * one row, so the panel covers a strip of the visuals instead of more than half of them.
 */
@Composable
private fun VisualizerTransport(
    viewModel: PlayerViewModel,
    state: PlayerUiState,
    position: () -> Long,
    waveform: FloatArray?,
    abLoop: AbLoop?,
    autoMode: Int,
    queueSize: Int,
    panel: PlayerPanel,
    onPanel: (PlayerPanel) -> Unit,
    onOpenVisuals: () -> Unit,
    shortWindow: Boolean,
) {
    val buttons: @Composable RowScope.() -> Unit = {
        ShuffleButton(state.shuffle, viewModel::toggleShuffle)
        IconButton(onClick = viewModel::previous, enabled = state.hasMedia) {
            StoneIconArt(StoneIcon.PREVIOUS, stringResource(R.string.action_previous))
        }
        CrystalPlayButton(
            icon = if (state.isPlaying) StoneIcon.PAUSE else StoneIcon.PLAY,
            contentDescription =
                stringResource(if (state.isPlaying) R.string.action_pause else R.string.action_play),
            onClick = viewModel::togglePlayPause,
            enabled = state.hasMedia,
        )
        IconButton(onClick = viewModel::next, enabled = state.hasMedia) {
            StoneIconArt(StoneIcon.NEXT, stringResource(R.string.action_next))
        }
        RepeatButton(state.repeatMode, viewModel::cycleRepeatMode)
    }
    val extras: @Composable RowScope.() -> Unit = {
        TextButton(onClick = onOpenVisuals) {
            StoneIconArt(StoneIcon.SETTINGS, null)
            Text("  " + stringResource(R.string.nav_visuals))
        }
        TextButton(onClick = viewModel::cycleAutoMode) {
            StoneIconArt(StoneIcon.QUEUE, null)
            Text(
                "  " +
                    stringResource(
                        when (autoMode) {
                            1 -> R.string.auto_random
                            2 -> R.string.auto_smart
                            3 -> R.string.auto_sections
                            else -> R.string.auto_off
                        },
                    ),
            )
        }
    }
    val chips: @Composable RowScope.() -> Unit = {
        PlayerPanel.entries.forEach { p ->
            PanelChip(
                label = stringResource(p.label),
                selected = panel == p,
                badge = if (p == PlayerPanel.QUEUE && queueSize > 1) queueSize else 0,
                onClick = { onPanel(if (panel == p) PlayerPanel.TRANSPORT else p) },
            )
        }
    }
    val abLoopButton: @Composable () -> Unit = {
        TextButton(onClick = viewModel::cycleAbLoop, enabled = state.hasMedia) {
            Text(
                when {
                    abLoop == null -> stringResource(R.string.ab_loop_idle)
                    abLoop?.endMs == null -> stringResource(R.string.ab_loop_set_b)
                    else -> stringResource(R.string.ab_loop_looping)
                },
                style = MaterialTheme.typography.labelMedium,
                color =
                    if (abLoop != null) {
                        MaterialTheme.colorScheme.primary
                    } else {
                        MaterialTheme.colorScheme.onSurfaceVariant
                    },
            )
        }
    }
    Column(Modifier.padding(horizontal = 14.dp, vertical = if (shortWindow) 4.dp else 10.dp)) {
        Row(
            Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            PositionClock(position, ::formatTime)
            WaveformSeekBar(
                waveform = waveform,
                positionMs = position,
                durationMs = state.durationMs,
                loopStartMs = abLoop?.startMs,
                loopEndMs = abLoop?.endMs,
                onSeek = viewModel::seekTo,
                modifier = Modifier.weight(1f).height(40.dp),
            )
            Text(formatTime(state.durationMs), style = MaterialTheme.typography.labelSmall)
        }
        if (shortWindow) {
            Row(
                Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                buttons()
                Spacer(Modifier.width(12.dp))
                extras()
                chips()
                abLoopButton()
            }
        } else {
            Row(
                Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceEvenly,
            ) { buttons() }
            Row(
                Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween,
            ) { extras() }
            Row(
                Modifier.fillMaxWidth().padding(top = 2.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                chips()
                Box(Modifier.weight(1f))
                abLoopButton()
            }
        }
    }
}

/**
 * Hides the status and navigation bars for as long as the live view is composed, and gives them
 * back when it leaves.
 *
 * The live view is something to watch, and a status bar across the top of it is the one piece of
 * chrome the auto-hide below cannot reach. Swiping from an edge still reveals the bars, over the
 * visuals and only for a moment, so the clock and notifications stay a gesture away.
 */
@Composable
private fun ImmersiveSystemBars() {
    val window = LocalContext.current.findMainActivity()?.window ?: return
    val view = LocalView.current
    DisposableEffect(window, view) {
        val controller = WindowInsetsControllerCompat(window, view)
        val behaviourBefore = controller.systemBarsBehavior
        controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        controller.hide(WindowInsetsCompat.Type.systemBars())
        onDispose {
            controller.show(WindowInsetsCompat.Type.systemBars())
            controller.systemBarsBehavior = behaviourBefore
        }
    }
}

/**
 * Hides the chrome after [CHROME_TIMEOUT_MS] with no touch, while [armed]. Every change of
 * [touches] restarts the countdown.
 *
 * Two accessibility settings win over the countdown. The timeout is stretched to the user's
 * "time to take action" choice (API 29+), and with touch exploration on (TalkBack) the chrome
 * never hides by itself: a screen reader user finds buttons by feel, and controls that vanish
 * while being explored cannot be found at all.
 */
@Composable
private fun AutoHideChrome(
    armed: Boolean,
    touches: Int,
    onHide: () -> Unit,
) {
    val context = LocalContext.current
    val hide by rememberUpdatedState(onHide)
    LaunchedEffect(armed, touches) {
        if (!armed) return@LaunchedEffect
        val a11y = context.getSystemService(AccessibilityManager::class.java)
        val recommendedMs =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                a11y?.getRecommendedTimeoutMillis(CHROME_TIMEOUT_MS, AccessibilityManager.FLAG_CONTENT_CONTROLS)
            } else {
                null
            }
        delay((recommendedMs ?: CHROME_TIMEOUT_MS).toLong())
        if (a11y?.isTouchExplorationEnabled != true) hide()
    }
}

private const val CHROME_TIMEOUT_MS = 4_000

private const val CHROME_MIN_ALPHA = 0.75f

private val TRANSPORT_MAX_WIDTH = 720.dp

/**
 * The top chrome row: collapse, PiP entry, now-playing title/subtitle, and favourite toggle.
 *
 * Extracted from [VisualizerScreen] purely to stay under detekt's LongMethod ceiling; it has
 * no state of its own beyond what the screen already collects.
 */
@Composable
private fun VisualizerTopBar(
    chromeAlpha: Float,
    externalDisplayName: String?,
    state: PlayerUiState,
    external: ExternalAudioState,
    isFavourite: Boolean,
    onCollapse: () -> Unit,
    onEnterPip: () -> Unit,
    onToggleFavourite: () -> Unit,
) {
    // Safe drawing rather than status bars: the bars are hidden on this screen, so their inset is
    // zero, and the camera cutout is what would otherwise sit on the buttons.
    Row(
        Modifier
            .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal))
            .padding(12.dp)
            .crystalPanel(
                chromeAlpha,
                MaterialTheme.colorScheme.surface,
                MaterialTheme.colorScheme.primary,
                corner = 12.dp,
                glowStrength = 0.6f,
            ).padding(horizontal = 8.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        FilledTonalIconButton(onClick = onCollapse) {
            Icon(Icons.Filled.KeyboardArrowDown, stringResource(R.string.action_collapse))
        }
        // Not offered on the second-screen placeholder: the visuals are rendering on the
        // connected display, and this card has nothing worth shrinking into a PiP window.
        if (externalDisplayName == null) {
            IconButton(onClick = onEnterPip) {
                Icon(Icons.Filled.PictureInPictureAlt, stringResource(R.string.action_pip))
            }
        }
        Column(Modifier.weight(1f, fill = false)) {
            val foreign = external.active
            val foreignTrack = external.nowPlaying?.takeIf { it.title.isNotBlank() }
            val appName = stringResource(R.string.app_name)
            Text(
                when {
                    foreign -> foreignTrack?.title ?: stringResource(R.string.source_other_apps)
                    else -> state.title?.ifBlank { appName } ?: appName
                },
                style = MaterialTheme.typography.titleSmall,
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            val subtitle =
                when {
                    foreign ->
                        listOfNotNull(
                            foreignTrack?.artist?.takeIf { it.isNotBlank() },
                            external.nowPlaying?.appLabel,
                        ).joinToString(" · ")
                            .ifBlank { stringResource(R.string.subtitle_captured_from_another_app) }
                    else -> state.artist?.takeIf { it.isNotBlank() }.orEmpty()
                }
            if (subtitle.isNotEmpty()) {
                Text(
                    subtitle,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        if (state.hasMedia && !external.active) {
            IconButton(onClick = onToggleFavourite) {
                StoneIconArt(
                    StoneIcon.FAVORITE,
                    stringResource(
                        if (isFavourite) {
                            R.string.action_favourite_remove
                        } else {
                            R.string.action_favourite_add
                        },
                    ),
                    tint =
                        if (isFavourite) {
                            MaterialTheme.colorScheme.primary
                        } else {
                            MaterialTheme.colorScheme.onSurfaceVariant
                        },
                )
            }
        }
    }
}

enum class PlayerPanel(
    @param:StringRes val label: Int,
) {
    TRANSPORT(R.string.panel_now),
    LYRICS(R.string.panel_lyrics),
    QUEUE(R.string.queue),
}

@Composable
private fun PanelChip(
    label: String,
    selected: Boolean,
    badge: Int,
    onClick: () -> Unit,
) {
    Box(
        Modifier
            .crystalPanel(
                if (selected) 0.5f else 0.2f,
                MaterialTheme.colorScheme.surfaceVariant,
                MaterialTheme.colorScheme.primary,
                corner = 14.dp,
                glowStrength = if (selected) 1f else 0.35f,
                prismatic = selected,
            ).selectable(selected = selected, role = Role.Tab, onClick = onClick)
            .padding(horizontal = 12.dp, vertical = 6.dp),
    ) {
        Text(
            if (badge > 0) "$label · $badge" else label,
            style = MaterialTheme.typography.labelMedium,
            color = if (selected) accentTextColor() else MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

/**
 * The visualizer surface's one pointer detector.
 *
 * Extracted from [VisualizerScreen] rather than inlined into its Modifier chain because the
 * composable sits against detekt's LongMethod ceiling, and because a detector this long reads
 * as its own concern: it is the only place in the app that turns raw pointers into both a
 * transform gesture and the engine-wide touch field.
 */
private fun Modifier.visualizerTouch(
    renderer: VisualizerRenderer,
    smear: Boolean,
    smearStrength: Float,
    transform: Boolean,
    onTransform: (Float, Float) -> Unit,
): Modifier =
    pointerInput(smear, smearStrength, transform) {
        // Still exactly one detector on this surface, because pointers belong to
        // whoever consumes them first and two detectors would each see half a
        // gesture. The pointers are published whatever the settings say — every
        // scene family is now expected to mean something by "where is the finger",
        // and no preference turns that off — while zoom, twist and smear stay
        // gated on the settings that have always gated them.
        val touch =
            VisualizerTouch(
                renderer = renderer,
                smear = smear,
                smearStrength = smearStrength,
                transform = transform,
                onTransform = onTransform,
            )
        try {
            awaitEachGesture {
                touch.begin()
                val slop = viewConfiguration.touchSlop
                awaitFirstDown(requireUnconsumed = false)
                // Published before the first move: a finger that lands and holds
                // still is a one-finger anchor, and for that gesture this is the
                // whole of it. Waiting for movement would make the commonest
                // gesture the one that never arrives.
                touch.publish(currentEvent, size)
                do {
                    val event = awaitPointerEvent()
                    // Consumed means another detector already claimed these
                    // pointers; deriving a transform from them as well would be a
                    // second reading of one gesture.
                    val canceled = event.changes.fastAny { it.isConsumed }
                    if (!canceled) {
                        touch.publish(event, size)
                        touch.steer(event, slop, size)
                    }
                } while (!canceled && event.changes.fastAny { it.pressed })
                touch.release()
            }
        } finally {
            // The loop is cancelled outright when the screen leaves composition
            // mid-gesture (a back swipe with a finger still down) or when a touch
            // preference changes. Without this the last published pointers would
            // read as still down forever, because nothing else retires them.
            touch.release()
        }
    }

/**
 * Everything one finger on the visualizer can mean, in one place.
 *
 * WHY it is one place: `detectTransformGestures` hands its callback a centroid and
 * nothing else, and the scene touch model reads the finger COUNT — one anchors, two
 * define an axis, three or more open a vortex — so a centroid cannot serve it. Adding a
 * second detector to read the pointers is worse than useless: pointers belong to whoever
 * consumes them first, so the two would fight and each would see a truncated gesture.
 * The pinch/twist quantities are therefore derived here, off the same event the
 * per-pointer positions are read from.
 *
 * The slop accounting is deliberately Compose's own arithmetic, factor for factor. This
 * surface also carries the tap that toggles the chrome, and "moved far enough to be a
 * drag" has to mean exactly one thing on it, or a gesture that steers the visuals also
 * flashes the transport bar on its way past.
 */

private class VisualizerTouch(
    private val renderer: VisualizerRenderer,
    private val smear: Boolean,
    private val smearStrength: Float,
    private val transform: Boolean,
    private val onTransform: (zoomFactor: Float, rotationDegrees: Float) -> Unit,
) {
    /**
     * Reused for the life of the gesture loop, never allocated per event: pointers arrive
     * at 120-240 Hz on a modern panel, so a fresh array each time would be thousands of
     * short-lived objects a minute underneath the one gesture that has to stay smooth.
     */
    private val pointers = FloatArray(TouchField.MAX_POINTS * 2)

    /** With both settings off there is nothing to steer, and so nothing to consume. */
    private val steers = smear || transform

    private var zoom = 1f
    private var rotation = 0f
    private var pan = Offset.Zero
    private var pastSlop = false

    /** Start a gesture. The slop totals are per-gesture; the pointer buffer is not. */
    fun begin() {
        zoom = 1f
        rotation = 0f
        pan = Offset.Zero
        pastSlop = false
    }

    /**
     * Hand the scenes the pointers that are down in [event], in y-up NDC.
     *
     * Only pressed changes count. A change that reports its finger as lifted is that
     * pointer's obituary rather than a position, and publishing it would leave an anchor
     * sitting under a finger that is gone.
     *
     * [surface] is read per event rather than captured once because this modifier is not
     * restarted by a rotation or a fold, and it can start before layout has given the node
     * a size at all — a stale divisor here would put the anchor somewhere the user is not.
     */
    fun publish(
        event: PointerEvent,
        surface: IntSize,
    ) {
        val w = surface.width.toFloat().coerceAtLeast(1f)
        val h = surface.height.toFloat().coerceAtLeast(1f)
        val changes = event.changes
        var live = 0
        for (i in changes.indices) {
            val change = changes[i]
            if (change.pressed && live < TouchField.MAX_POINTS) {
                // Compose measures y downward from the top-left corner; the scenes measure
                // it upward from the centre, which is the frame their own geometry is in.
                pointers[live * 2] = change.position.x / w * 2f - 1f
                pointers[live * 2 + 1] = 1f - change.position.y / h * 2f
                live++
            }
        }
        renderer.submitTouchPoints(pointers, live)
    }

    /**
     * Every finger is off, or something else claimed the gesture.
     *
     * Publishing zero points is a state and not a no-op: it is the only thing that retires
     * a point and starts the release decay, so an abandoned or cancelled gesture has to
     * say it too or the visuals keep being steered by a touch that ended.
     */
    fun release() = renderer.submitTouchPoints(pointers, 0)

    /**
     * Feed [event] to the pinch/twist and smear paths, once the gesture has earned them.
     */
    fun steer(
        event: PointerEvent,
        slop: Float,
        surface: IntSize,
    ) {
        if (!steers) return
        val zoomChange = event.calculateZoom()
        val rotationChange = event.calculateRotation()
        val panChange = event.calculatePan()
        if (!pastSlop) {
            zoom *= zoomChange
            rotation += rotationChange
            pan += panChange
            // Slop is measured in the pixels the fingers actually travel: the same 2% pinch
            // moves further under a wide grip than a narrow one, so the spread scales it.
            // `useCurrent = false` takes that spread from BEFORE this event, so the sample
            // being tested does not also move the threshold it is tested against.
            val spread = event.calculateCentroidSize(useCurrent = false)
            val zoomMotion = abs(1f - zoom) * spread
            val twistMotion = abs(rotation * PI.toFloat() * spread / 180f)
            pastSlop = zoomMotion > slop || twistMotion > slop || pan.getDistance() > slop
        }
        if (!pastSlop) return
        if (rotationChange != 0f || zoomChange != 1f || panChange != Offset.Zero) {
            dispatch(event, zoomChange, rotationChange, panChange, surface)
        }
        // Consuming is how the tap detector upstream learns this was a drag and not a tap
        // on the chrome. Only past slop: a finger that lands and holds still must leave its
        // changes untouched, or the chrome toggle dies with it.
        event.changes.fastForEach { if (it.positionChanged()) it.consume() }
    }

    private fun dispatch(
        event: PointerEvent,
        zoomChange: Float,
        rotationChange: Float,
        panChange: Offset,
        surface: IntSize,
    ) {
        if (transform && TouchTransform.isTransform(zoomChange, rotationChange)) {
            // Pinch and twist drive the real Zoom and Rotation params rather than a
            // view-only transform, which is why a gesture survives into presets, takes and
            // exports instead of evaporating when the screen is left.
            onTransform(zoomChange, rotationChange)
        } else if (smear) {
            val w = surface.width.toFloat().coerceAtLeast(1f)
            val h = surface.height.toFloat().coerceAtLeast(1f)
            val centroid = event.calculateCentroid(useCurrent = false)
            renderer.queueTouchStroke(
                nx = centroid.x / w,
                ny = centroid.y / h,
                ndx = panChange.x / w,
                ndy = panChange.y / h,
                dt = FRAME_DT,
                strength = smearStrength,
            )
        }
    }
}

private const val FRAME_DT = 1f / 60f

private fun formatTime(ms: Long): String {
    val total = (ms / 1000).coerceAtLeast(0)
    return "%d:%02d".format(total / 60, total % 60)
}
