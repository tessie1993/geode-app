package dev.geode.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bedtime
import androidx.compose.material.icons.filled.Cast
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconToggleButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.repeatOnLifecycle
import androidx.media3.common.Player
import dev.geode.R
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import kotlinx.coroutines.delay

@Composable
fun PlayerScreen(
    viewModel: PlayerViewModel,
    onOpenSearch: () -> Unit,
    onExpand: () -> Unit,
    onOpenLibrary: () -> Unit,
) {
    val state by viewModel.uiState.collectAsStateWithLifecycle()
    // Not delegated: only the seek bar and the clock read .value, so a tick recomposes just those.
    val position = viewModel.positionMs.collectAsStateWithLifecycle()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val mic by viewModel.micState.collectAsStateWithLifecycle()
    val external by viewModel.externalAudio.collectAsStateWithLifecycle()
    val waveform by viewModel.waveform.collectAsStateWithLifecycle()
    val abLoop by viewModel.abLoop.collectAsStateWithLifecycle()
    val autoMode by viewModel.autoMode.collectAsStateWithLifecycle()
    val queue by viewModel.queue.collectAsStateWithLifecycle()
    val favourites by viewModel.favourites.collectAsStateWithLifecycle()
    val tick by viewModel.historyTick.collectAsStateWithLifecycle()
    val sleepRemainingMs by viewModel.sleepTimerRemainingMs.collectAsStateWithLifecycle()
    val canShuffle = remember(tick) { viewModel.recentlyPlayed().isNotEmpty() }
    var showQueue by rememberSaveable { mutableStateOf(true) }
    val upNext = remember(queue) { queue.tracks.drop(queue.index + 1).take(3) }

    val header: @Composable () -> Unit = {
        Row(
            Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, top = 16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                CrystalOverline(stringResource(R.string.app_name))
                GlowTitle(stringResource(R.string.nav_player))
            }
            IconButton(onClick = onOpenSearch) { StoneIconArt(StoneIcon.SEARCH, stringResource(R.string.action_search)) }
        }
    }
    val hero: @Composable (Dp) -> Unit = { artMax ->
        PlayerHero(
            viewModel = viewModel,
            state = state,
            styleLabel = sceneDisplayLabel(viz.sceneId),
            micActive = mic.active,
            external = external,
            favourites = favourites,
            canResume = canShuffle,
            onExpand = onExpand,
            onOpenLibrary = onOpenLibrary,
            artMax = artMax,
            modifier = Modifier.padding(horizontal = 16.dp),
        )
    }

    val controls: LazyListScope.() -> Unit = {
        item {
            TransportCard(
                viewModel = viewModel,
                state = state,
                position = { position.value },
                waveform = waveform,
                abLoop = abLoop,
                autoMode = autoMode,
                queueSize = queue.tracks.size,
                queueOpen = showQueue,
                onToggleQueue = { showQueue = !showQueue },
                modifier = Modifier.padding(horizontal = 16.dp),
            )
        }

        item {
            LiveSpectrum(
                viewModel,
                live = state.isPlaying || mic.active || external.active,
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp).height(44.dp),
            )
        }

        if (showQueue && upNext.isNotEmpty()) {
            item {
                QueuePreview(
                    upNext = upNext,
                    onExpand = onExpand,
                    modifier = Modifier.padding(horizontal = 16.dp),
                )
            }
        }

        item {
            QuickActions(
                viewModel = viewModel,
                micActive = mic.active,
                external = external,
                sleepRunning = sleepRemainingMs != null,
                canShuffle = canShuffle,
            )
        }
    }

    BoxWithConstraints(Modifier.fillMaxSize()) {
        // Side by side once the pane is wide enough for two readable columns, or on a short
        // landscape window, where one column would push the transport below the fold. Measured on
        // this pane rather than the window, so the Player half of the two-pane Library stays one
        // column.
        val sideBySide = maxWidth >= SIDE_BY_SIDE_MIN_WIDTH || (maxWidth > maxHeight && maxHeight < SHORT_HEIGHT)
        // Read here: inside the Row below, BoxWithConstraints' scope is not an implicit receiver.
        val paneHeight = maxHeight
        if (sideBySide) {
            Row(Modifier.fillMaxSize()) {
                Column(
                    Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .verticalScroll(rememberScrollState())
                        .padding(bottom = 24.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp),
                ) {
                    header()
                    // The art takes what height is left under the header and the title rows.
                    hero((paneHeight - HERO_CHROME).coerceIn(MIN_ART, MAX_ART))
                }
                LazyColumn(
                    Modifier.weight(1f).fillMaxHeight(),
                    contentPadding = PaddingValues(top = 16.dp, bottom = 24.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp),
                ) {
                    controls()
                }
            }
        } else {
            LazyColumn(
                Modifier.fillMaxSize(),
                contentPadding = PaddingValues(bottom = 24.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                item { header() }
                item { hero(MAX_ART) }
                controls()
            }
        }
    }
}

/** Below this the pane gets one column; at and above it, art and controls sit side by side. */
private val SIDE_BY_SIDE_MIN_WIDTH = 720.dp

/** A landscape pane shorter than this is a phone on its side. */
private val SHORT_HEIGHT = 480.dp

/** On a phone the art fills the width; on anything wider it stops here. */
private val MAX_ART = 420.dp
private val MIN_ART = 160.dp

/** Header, title rows, chips and buttons around the art in the side-by-side hero. */
private val HERO_CHROME = 300.dp

@Composable
private fun PlayerHero(
    viewModel: PlayerViewModel,
    state: PlayerUiState,
    styleLabel: String,
    micActive: Boolean,
    external: ExternalAudioState,
    favourites: Set<String>,
    canResume: Boolean,
    onExpand: () -> Unit,
    onOpenLibrary: () -> Unit,
    artMax: Dp,
    modifier: Modifier = Modifier,
) {
    val uri = remember(state.title, state.artist) { viewModel.currentTrackUri() }
    val foreign = external.active
    val foreignTrack = external.nowPlaying?.takeIf { it.title.isNotBlank() }
    val hasSource = foreign || micActive || state.hasMedia
    val isFavourite = uri != null && uri in favourites
    Column(
        modifier
            .fillMaxWidth()
            .crystalPanel(
                0.42f,
                MaterialTheme.colorScheme.surfaceVariant,
                MaterialTheme.colorScheme.primary,
                corner = 24.dp,
                glowStrength = if (state.isPlaying || foreign || micActive) 1.2f else 0.7f,
            ).clickable(enabled = hasSource, onClick = onExpand)
            .padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        // Square and as wide as the card on a phone, but never taller than [artMax]: on a tablet a
        // full-width square pushed the title and transport two screens down.
        TrackArtwork(
            if (foreign || micActive) null else uri,
            Modifier
                .align(Alignment.CenterHorizontally)
                .widthIn(max = artMax)
                .fillMaxWidth()
                .aspectRatio(1f),
            corner = 18.dp,
        )
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                CrystalOverline(
                    when {
                        foreign -> external.nowPlaying?.appLabel ?: stringResource(R.string.source_other_apps)
                        micActive -> stringResource(R.string.source_live_input)
                        state.isPlaying -> stringResource(R.string.state_now_playing)
                        state.hasMedia -> stringResource(R.string.state_paused)
                        else -> stringResource(R.string.state_nothing_playing)
                    },
                )
                Text(
                    when {
                        foreign -> foreignTrack?.title ?: stringResource(R.string.title_whatever_is_playing)
                        micActive -> stringResource(R.string.title_the_room)
                        state.hasMedia -> state.title ?: stringResource(R.string.title_untitled)
                        else -> stringResource(R.string.title_pick_something)
                    },
                    style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.SemiBold),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    when {
                        external.refusedByApp ->
                            stringResource(
                                R.string.subtitle_capture_refused,
                                external.refusingApp
                                    ?: stringResource(R.string.subtitle_capture_refused_unknown_app),
                            )
                        foreign ->
                            foreignTrack?.artist?.ifBlank { null }
                                ?: stringResource(R.string.subtitle_captured_from_another_app)
                        micActive -> stringResource(R.string.subtitle_microphone_hears)
                        state.hasMedia ->
                            state.artist?.takeIf { it.isNotBlank() }
                                ?: stringResource(R.string.subtitle_unknown_artist)
                        else -> stringResource(R.string.subtitle_nothing_playing)
                    },
                    style = MaterialTheme.typography.bodySmall,
                    color =
                        if (external.refusedByApp) {
                            MaterialTheme.colorScheme.error
                        } else {
                            MaterialTheme.colorScheme.onSurfaceVariant
                        },
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            if (state.hasMedia && !foreign) {
                IconButton(onClick = { viewModel.toggleFavourite() }) {
                    StoneIconArt(
                        StoneIcon.FAVORITE,
                        stringResource(
                            if (isFavourite) R.string.action_favourite_remove else R.string.action_favourite_add,
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
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            SceneChip(styleLabel)
            Box(Modifier.weight(1f))
            if (foreign) {
                CrystalButton(filled = false, compact = true, onClick = viewModel::stopExternalAudio) {
                    Text(stringResource(R.string.action_stop_capture))
                }
            }
        }
        if (!hasSource) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                CrystalButton(enabled = canResume, onClick = viewModel::resumeLastPlayed) {
                    Text(stringResource(R.string.action_resume_last_played))
                }
                CrystalButton(filled = false, onClick = onOpenLibrary) { Text(stringResource(R.string.action_open_library)) }
            }
        }
    }
}

@Composable
private fun SceneChip(label: String) {
    Box(
        Modifier
            .crystalPanel(
                0.25f,
                MaterialTheme.colorScheme.surfaceVariant,
                MaterialTheme.colorScheme.primary,
                corner = 12.dp,
                glowStrength = 0.4f,
            ).padding(horizontal = 10.dp, vertical = 4.dp),
    ) {
        Text(
            label,
            style = MaterialTheme.typography.labelMedium,
            color = accentTextColor(),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun TransportCard(
    viewModel: PlayerViewModel,
    state: PlayerUiState,
    position: () -> Long,
    waveform: FloatArray?,
    abLoop: AbLoop?,
    autoMode: Int,
    queueSize: Int,
    queueOpen: Boolean,
    onToggleQueue: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier
            .fillMaxWidth()
            .crystalPanel(
                0.35f,
                MaterialTheme.colorScheme.surfaceVariant,
                MaterialTheme.colorScheme.primary,
                corner = 24.dp,
                glowStrength = 0.8f,
            ).padding(horizontal = 14.dp, vertical = 10.dp),
    ) {
        Row(
            Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            PositionClock(position)
            WaveformSeekBar(
                waveform = waveform,
                positionMs = position,
                durationMs = state.durationMs,
                loopStartMs = abLoop?.startMs,
                loopEndMs = abLoop?.endMs,
                onSeek = viewModel::seekTo,
                modifier = Modifier.weight(1f).height(40.dp),
            )
            Text(formatClock(state.durationMs), style = MaterialTheme.typography.labelSmall)
        }
        Row(
            Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceEvenly,
        ) {
            ShuffleButton(state.shuffle, viewModel::toggleShuffle)
            IconButton(onClick = viewModel::previous, enabled = state.hasMedia) {
                StoneIconArt(StoneIcon.PREVIOUS, stringResource(R.string.action_previous))
            }
            CrystalPlayButton(
                icon = if (state.isPlaying) StoneIcon.PAUSE else StoneIcon.PLAY,
                contentDescription = stringResource(if (state.isPlaying) R.string.action_pause else R.string.action_play),
                onClick = viewModel::togglePlayPause,
                enabled = state.hasMedia,
            )
            IconButton(onClick = viewModel::next, enabled = state.hasMedia) {
                StoneIconArt(StoneIcon.NEXT, stringResource(R.string.action_next))
            }
            RepeatButton(state.repeatMode, viewModel::cycleRepeatMode)
        }
        Row(
            Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            TextButton(onClick = viewModel::cycleAbLoop, enabled = state.hasMedia) {
                Text(
                    when {
                        abLoop == null -> stringResource(R.string.ab_loop_idle)
                        abLoop.endMs == null -> stringResource(R.string.ab_loop_set_b)
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
            TextButton(onClick = viewModel::cycleAutoMode) {
                Text(
                    when (autoMode) {
                        1 -> stringResource(R.string.auto_random)
                        2 -> stringResource(R.string.auto_smart)
                        3 -> stringResource(R.string.auto_sections)
                        else -> stringResource(R.string.auto_off)
                    },
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            TextButton(onClick = onToggleQueue) {
                Text(
                    if (queueSize > 1) {
                        stringResource(R.string.queue_with_count, queueSize)
                    } else {
                        stringResource(R.string.queue)
                    },
                    style = MaterialTheme.typography.labelMedium,
                    color =
                        if (queueOpen) {
                            MaterialTheme.colorScheme.primary
                        } else {
                            MaterialTheme.colorScheme.onSurfaceVariant
                        },
                )
            }
        }
    }
}

@Composable
private fun QueuePreview(
    upNext: List<QueueTrack>,
    onExpand: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier
            .fillMaxWidth()
            .crystalPanel(
                0.3f,
                MaterialTheme.colorScheme.surfaceVariant,
                MaterialTheme.colorScheme.primary,
                corner = 20.dp,
                glowStrength = 0.5f,
            ).padding(start = 14.dp, end = 14.dp, top = 12.dp, bottom = 4.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        val untitled = stringResource(R.string.title_untitled)
        CrystalOverline(stringResource(R.string.queue_up_next))
        upNext.forEach { t ->
            Text(
                t.title.ifBlank { untitled },
                Modifier.fillMaxWidth().clickable(onClick = onExpand),
                style = MaterialTheme.typography.bodyMedium,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
            TextButton(onClick = onExpand) {
                Text(stringResource(R.string.action_open_queue), style = MaterialTheme.typography.labelMedium, color = accentTextColor())
            }
        }
    }
}

@Composable
private fun LiveSpectrum(
    viewModel: PlayerViewModel,
    live: Boolean,
    modifier: Modifier = Modifier,
) {
    // Kept as State and read only in the draw block: the 20 Hz bars redraw the Canvas without
    // recomposing anything, and the loop stops while the Activity is in the background.
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    val bars =
        produceState(initialValue = FloatArray(BARS), live, lifecycle) {
            lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
                driveSpectrum(live, { viewModel.features.value.bands }) { value = it }
            }
        }
    val primary = MaterialTheme.colorScheme.primary
    val secondary = MaterialTheme.colorScheme.secondary
    Spacer(
        modifier.drawWithCache {
            val gap = size.width / (BARS * 6f)
            val barWidth = (size.width - gap * (BARS - 1)) / BARS
            val brush =
                Brush.verticalGradient(
                    listOf(secondary.copy(alpha = 0.95f), primary.copy(alpha = 0.75f)),
                )
            onDrawBehind { drawBars(bars.value, gap, barWidth, brush) }
        },
    )
}

private fun DrawScope.drawBars(
    bars: FloatArray,
    gap: Float,
    barWidth: Float,
    brush: Brush,
) {
    for (i in 0 until BARS) {
        val v = bars.getOrElse(i) { 0f }.coerceIn(0f, 1f)
        val h = (size.height * (0.06f + 0.94f * v)).coerceAtLeast(2f)
        drawRoundRect(
            brush = brush,
            topLeft =
                androidx.compose.ui.geometry
                    .Offset(i * (barWidth + gap), size.height - h),
            size =
                androidx.compose.ui.geometry
                    .Size(barWidth, h),
            cornerRadius =
                androidx.compose.ui.geometry
                    .CornerRadius(barWidth / 2f),
        )
    }
}

internal const val BARS = 24

internal const val SPECTRUM_TICK_MS = 50L

internal suspend fun driveSpectrum(
    live: Boolean,
    bands: () -> FloatArray,
    emit: (FloatArray) -> Unit,
) {
    if (!live) {
        emit(FloatArray(BARS))
        return
    }
    val smoothed = FloatArray(BARS)
    while (true) {
        val current = bands()
        for (i in 0 until BARS) {
            val target =
                if (current.isEmpty()) {
                    0f
                } else {
                    val from = i * current.size / BARS
                    val to = ((i + 1) * current.size / BARS).coerceAtLeast(from + 1)
                    var acc = 0f
                    for (b in from until minOf(to, current.size)) acc += current[b]
                    acc / (minOf(to, current.size) - from)
                }
            smoothed[i] =
                if (target > smoothed[i]) target else smoothed[i] + (target - smoothed[i]) * 0.35f
        }
        emit(smoothed.copyOf())
        delay(SPECTRUM_TICK_MS)
    }
}

@Composable
private fun QuickActions(
    viewModel: PlayerViewModel,
    micActive: Boolean,
    external: ExternalAudioState,
    sleepRunning: Boolean,
    canShuffle: Boolean,
) {
    val context = androidx.compose.ui.platform.LocalContext.current
    // Once Android stops showing the dialog, request() opens the app's Settings page instead, so
    // the button never silently does nothing.
    val micPermission =
        rememberPermissionRequest(android.Manifest.permission.RECORD_AUDIO) { viewModel.setMicEnabled(true) }
    val projectionLauncher =
        androidx.activity.compose.rememberLauncherForActivityResult(
            androidx.activity.result.contract.ActivityResultContracts
                .StartActivityForResult(),
        ) { result ->
            val data = result.data
            if (result.resultCode == android.app.Activity.RESULT_OK && data != null) {
                dev.geode.audio.PlaybackCaptureService
                    .start(context, result.resultCode, data)
            } else {
                viewModel.noteExternalAudioConsentDenied()
            }
        }
    val capturePermissions =
        androidx.activity.compose.rememberLauncherForActivityResult(
            androidx.activity.result.contract.ActivityResultContracts
                .RequestMultiplePermissions(),
        ) { granted ->
            if (granted[android.Manifest.permission.RECORD_AUDIO] != false) {
                viewModel.noteExternalAudioConsentPending()
                projectionLauncher.launch(
                    context
                        .getSystemService(android.media.projection.MediaProjectionManager::class.java)
                        .createScreenCaptureIntent(),
                )
            } else {
                viewModel.noteExternalAudioConsentDenied()
            }
        }
    LazyRow(
        horizontalArrangement = Arrangement.spacedBy(10.dp),
        contentPadding =
            androidx.compose.foundation.layout
                .PaddingValues(horizontal = 16.dp),
    ) {
        item {
            QuickAction(
                StoneIcon.MICROPHONE,
                stringResource(if (micActive) R.string.quick_room_on else R.string.source_live_input),
                active = micActive,
            ) {
                if (micActive) {
                    viewModel.setMicEnabled(false)
                } else if (viewModel.hasMicPermission()) {
                    viewModel.setMicEnabled(true)
                } else {
                    micPermission.request()
                }
            }
        }
        if (external.supported) {
            item {
                QuickAction(
                    Icons.Filled.Cast,
                    stringResource(if (external.active) R.string.quick_capturing else R.string.source_other_apps),
                    active = external.active,
                ) {
                    if (external.active) {
                        viewModel.stopExternalAudio()
                    } else {
                        capturePermissions.launch(
                            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
                                arrayOf(
                                    android.Manifest.permission.RECORD_AUDIO,
                                    android.Manifest.permission.POST_NOTIFICATIONS,
                                )
                            } else {
                                arrayOf(android.Manifest.permission.RECORD_AUDIO)
                            },
                        )
                    }
                }
            }
        }
        item {
            QuickAction(
                Icons.Filled.Bedtime,
                stringResource(if (sleepRunning) R.string.quick_sleep_on else R.string.quick_sleep_30m),
                active = sleepRunning,
            ) {
                if (sleepRunning) viewModel.cancelSleepTimer() else viewModel.startSleepTimer(30)
            }
        }
        item {
            QuickAction(StoneIcon.SHUFFLE, stringResource(R.string.quick_shuffle_all), enabled = canShuffle) {
                viewModel.shuffleAllHistory()
            }
        }
    }
}

@Composable
private fun QuickAction(
    icon: StoneIcon,
    label: String,
    enabled: Boolean = true,
    active: Boolean = false,
    onClick: () -> Unit,
) = QuickActionShell(label, enabled, active, onClick) { StoneIconArt(icon, null, Modifier.size(22.dp), tint = it) }

@Composable
private fun QuickAction(
    icon: ImageVector,
    label: String,
    enabled: Boolean = true,
    active: Boolean = false,
    onClick: () -> Unit,
) = QuickActionShell(label, enabled, active, onClick) { Icon(icon, null, Modifier.size(22.dp), tint = it) }

@Composable
private fun QuickActionShell(
    label: String,
    enabled: Boolean,
    active: Boolean,
    onClick: () -> Unit,
    icon: @Composable (Color) -> Unit,
) {
    val tint =
        when {
            !enabled -> MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
            active -> MaterialTheme.colorScheme.primary
            else -> MaterialTheme.colorScheme.onSurface
        }
    val onLabel = stringResource(R.string.a11y_state_on)
    Column(
        Modifier
            // A minimum, not a fixed width, so "Shuffle all" at 200% font wraps instead of "Shu…".
            .widthIn(min = 84.dp, max = 120.dp)
            .crystalPanel(
                if (active) 0.5f else 0.28f,
                MaterialTheme.colorScheme.surfaceVariant,
                MaterialTheme.colorScheme.primary,
                corner = 18.dp,
                glowStrength = if (active) 1.1f else 0.45f,
                prismatic = active,
            ).semantics { if (active) stateDescription = onLabel }
            .clickable(enabled = enabled, role = Role.Button, onClick = onClick)
            .padding(vertical = 12.dp, horizontal = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        icon(tint)
        Text(
            label,
            style = MaterialTheme.typography.labelSmall,
            color = tint,
            textAlign = TextAlign.Center,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

internal fun formatClock(ms: Long): String {
    val total = (ms / 1000).coerceAtLeast(0)
    val hours = total / 3600
    val minutes = (total % 3600) / 60
    val seconds = total % 60
    return if (hours > 0) "%d:%02d:%02d".format(hours, minutes, seconds) else "%d:%02d".format(minutes, seconds)
}

/** A toggle, not a plain button, so TalkBack says whether shuffle is on rather than leaving it to the tint. */
@Composable
internal fun ShuffleButton(
    shuffle: Boolean,
    onToggle: () -> Unit,
) {
    IconToggleButton(checked = shuffle, onCheckedChange = { onToggle() }) {
        StoneIconArt(
            StoneIcon.SHUFFLE,
            stringResource(R.string.action_shuffle),
            tint = if (shuffle) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

/** Repeat-one gets its own icon, and the label names the mode, so all and one are told apart. */
@Composable
internal fun RepeatButton(
    repeatMode: Int,
    onClick: () -> Unit,
) {
    IconButton(onClick = onClick) {
        StoneIconArt(
            if (repeatMode == Player.REPEAT_MODE_ONE) StoneIcon.REPEAT_ONE else StoneIcon.REPEAT,
            stringResource(
                when (repeatMode) {
                    Player.REPEAT_MODE_ALL -> R.string.action_repeat_all
                    Player.REPEAT_MODE_ONE -> R.string.action_repeat_one
                    else -> R.string.action_repeat_off
                },
            ),
            tint =
                if (repeatMode != Player.REPEAT_MODE_OFF) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
        )
    }
}
