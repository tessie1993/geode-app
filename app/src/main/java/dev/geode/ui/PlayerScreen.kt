package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bedtime
import androidx.compose.material.icons.filled.Cast
import androidx.compose.material.icons.filled.History
import androidx.compose.material.icons.filled.Repeat
import androidx.compose.material.icons.filled.Shuffle
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.media3.common.Player
import dev.geode.R
import dev.geode.ui.glass.GlassBubbleButton
import dev.geode.ui.glass.GlassButton
import dev.geode.ui.glass.GlassIcons
import dev.geode.ui.glass.GlassListRow
import dev.geode.ui.glass.GlassPalette
import dev.geode.ui.glass.GlassShapes
import dev.geode.ui.glass.GlassTopBar
import dev.geode.ui.glass.GlassTransportBar
import dev.geode.ui.glass.floatOnWater
import dev.geode.ui.glass.glassSurface
import dev.geode.ui.glass.glassTouch
import kotlinx.coroutines.delay

@Composable
fun PlayerScreen(
    viewModel: PlayerViewModel,
    onOpenSearch: () -> Unit,
    onExpand: () -> Unit,
    onOpenLibrary: () -> Unit,
) {
    val state by viewModel.uiState.collectAsStateWithLifecycle()
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
    var showQueuePanel by rememberSaveable { mutableStateOf(false) }
    val upNext = remember(queue) { queue.tracks.drop(queue.index + 1).take(3) }

    LazyColumn(
        Modifier.fillMaxSize(),
        contentPadding =
            androidx.compose.foundation.layout
                .PaddingValues(bottom = 24.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        item {
            GlassTopBar(
                title = stringResource(R.string.app_name),
                onClose = onOpenLibrary,
                onMenu = onOpenSearch,
            )
        }

        item {
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
                modifier = Modifier.padding(horizontal = 16.dp),
            )
        }

        item {
            TransportSection(
                viewModel = viewModel,
                state = state,
                waveform = waveform,
                abLoop = abLoop,
                autoMode = autoMode,
                queueSize = queue.tracks.size,
                queueOpen = showQueue,
                onToggleQueue = { showQueue = !showQueue },
                onOpenQueuePanel = { showQueuePanel = true },
                onOpenLibrary = onOpenLibrary,
                modifier = Modifier.padding(horizontal = 16.dp),
            )
        }

        item {
            LiveSpectrum(
                viewModel,
                live = state.isPlaying || mic.active || external.active,
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
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
                state = state,
                micActive = mic.active,
                external = external,
                sleepRunning = sleepRemainingMs != null,
                canShuffle = canShuffle,
            )
        }
    }

    if (showQueuePanel) {
        QueuePanel(
            queue = queue,
            favourites = favourites,
            onPlayIndex = viewModel::playQueueIndex,
            onMoveUp = { viewModel.moveQueueItem(it, it - 1) },
            onRemove = viewModel::removeQueueItem,
            onDismissRequest = { showQueuePanel = false },
        )
    }
}

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
            .glassTouch(enabled = hasSource, onClick = onExpand),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Box(
            Modifier
                .size(HERO_BUBBLE_SIZE)
                .glassSurface(
                    shape = GlassShapes.bubble,
                    glow = if (state.isPlaying || foreign || micActive) 0.6f else 0.25f,
                ).floatOnWater(strength = 1.3f),
        ) {
            TrackArtwork(
                if (foreign || micActive) null else uri,
                Modifier.matchParentSize().padding(14.dp),
                corner = HERO_ARTWORK_CORNER,
            )
        }
        Text(
            when {
                foreign -> external.nowPlaying?.appLabel ?: stringResource(R.string.source_other_apps)
                micActive -> stringResource(R.string.source_live_input)
                state.isPlaying -> stringResource(R.string.state_now_playing)
                state.hasMedia -> stringResource(R.string.state_paused)
                else -> stringResource(R.string.state_nothing_playing)
            },
            style = MaterialTheme.typography.labelMedium,
            color = GlassPalette.textSecondary,
            maxLines = 1,
        )
        Row(
            Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                when {
                    foreign -> foreignTrack?.title ?: stringResource(R.string.title_whatever_is_playing)
                    micActive -> stringResource(R.string.title_the_room)
                    state.hasMedia -> state.title ?: stringResource(R.string.title_untitled)
                    else -> stringResource(R.string.title_pick_something)
                },
                Modifier.weight(1f),
                style = MaterialTheme.typography.headlineSmall,
                color = GlassPalette.textPrimary,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (state.hasMedia && !foreign) {
                GlassBubbleButton(
                    icon = GlassIcons.Heart,
                    contentDescription =
                        stringResource(if (isFavourite) R.string.action_favourite_remove else R.string.action_favourite_add),
                    onClick = { viewModel.toggleFavourite() },
                    size = 36.dp,
                    tint = if (isFavourite) GlassPalette.pink else null,
                )
            }
        }
        Text(
            when {
                external.refusedByApp ->
                    stringResource(
                        R.string.subtitle_capture_refused,
                        external.refusingApp ?: stringResource(R.string.subtitle_capture_refused_unknown_app),
                    )
                foreign -> foreignTrack?.artist?.ifBlank { null } ?: stringResource(R.string.subtitle_captured_from_another_app)
                micActive -> stringResource(R.string.subtitle_microphone_hears)
                state.hasMedia -> state.artist?.takeIf { it.isNotBlank() } ?: stringResource(R.string.subtitle_unknown_artist)
                else -> stringResource(R.string.subtitle_nothing_playing)
            },
            style = MaterialTheme.typography.bodyMedium,
            color = if (external.refusedByApp) GlassPalette.pink else GlassPalette.textSecondary,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            SceneChip(styleLabel)
            if (foreign) {
                GlassButton(
                    text = stringResource(R.string.action_stop_capture),
                    onClick = viewModel::stopExternalAudio,
                )
            }
        }
        if (!hasSource) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                GlassButton(
                    text = stringResource(R.string.action_resume_last_played),
                    enabled = canResume,
                    tint = GlassPalette.mint,
                    onClick = viewModel::resumeLastPlayed,
                )
                GlassButton(text = stringResource(R.string.action_open_library), onClick = onOpenLibrary)
            }
        }
    }
}

@Composable
private fun SceneChip(label: String) {
    Box(Modifier.glassSurface(shape = GlassShapes.pill, tint = GlassPalette.lavender).padding(horizontal = 12.dp, vertical = 6.dp)) {
        Text(label, style = MaterialTheme.typography.labelMedium, color = GlassPalette.textPrimary, maxLines = 1)
    }
}

@Composable
private fun TransportSection(
    viewModel: PlayerViewModel,
    state: PlayerUiState,
    waveform: FloatArray?,
    abLoop: AbLoop?,
    autoMode: Int,
    queueSize: Int,
    queueOpen: Boolean,
    onToggleQueue: () -> Unit,
    onOpenQueuePanel: () -> Unit,
    onOpenLibrary: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(
            Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(formatClock(state.positionMs), style = MaterialTheme.typography.labelSmall, color = GlassPalette.textSecondary)
            WaveformSeekBar(
                waveform = waveform,
                positionMs = state.positionMs,
                durationMs = state.durationMs,
                loopStartMs = abLoop?.startMs,
                loopEndMs = abLoop?.endMs,
                onSeek = viewModel::seekTo,
                modifier = Modifier.weight(1f).height(40.dp),
            )
            Text(formatClock(state.durationMs), style = MaterialTheme.typography.labelSmall, color = GlassPalette.textSecondary)
        }
        // GlassTransportBar has no per-button enabled slot (docs/design/liquid-glass/README.md);
        // the callbacks themselves guard on hasMedia so previous/play/next stay inert with an
        // empty queue, matching the disabled state the old transport row had.
        GlassTransportBar(
            playing = state.isPlaying,
            onPlayPause = { if (state.hasMedia) viewModel.togglePlayPause() },
            onPrevious = { if (state.hasMedia) viewModel.previous() },
            onNext = { if (state.hasMedia) viewModel.next() },
            onLibrary = onOpenLibrary,
            onProfile = onOpenQueuePanel,
        )
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            GlassButton(
                text =
                    when {
                        abLoop == null -> stringResource(R.string.ab_loop_idle)
                        abLoop.endMs == null -> stringResource(R.string.ab_loop_set_b)
                        else -> stringResource(R.string.ab_loop_looping)
                    },
                selected = abLoop != null,
                onClick = viewModel::cycleAbLoop,
            )
            GlassButton(
                text =
                    when (autoMode) {
                        1 -> stringResource(R.string.auto_random)
                        2 -> stringResource(R.string.auto_smart)
                        3 -> stringResource(R.string.auto_sections)
                        else -> stringResource(R.string.auto_off)
                    },
                onClick = viewModel::cycleAutoMode,
            )
            GlassButton(
                text =
                    if (queueSize > 1) {
                        stringResource(R.string.queue_with_count, queueSize)
                    } else {
                        stringResource(R.string.queue)
                    },
                selected = queueOpen,
                onClick = onToggleQueue,
            )
        }
    }
}

@Composable
private fun QueuePreview(
    upNext: List<QueueTrack>,
    onExpand: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val untitled = stringResource(R.string.title_untitled)
    Column(modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(stringResource(R.string.queue_up_next), style = MaterialTheme.typography.labelLarge, color = GlassPalette.textSecondary)
        upNext.forEach { track ->
            GlassListRow(
                title = track.title.ifBlank { untitled },
                subtitle = track.artist.takeIf { it.isNotBlank() },
                onClick = onExpand,
            )
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
            GlassButton(text = stringResource(R.string.action_open_queue), onClick = onExpand)
        }
    }
}

@Composable
private fun LiveSpectrum(
    viewModel: PlayerViewModel,
    live: Boolean,
    modifier: Modifier = Modifier,
) {
    val bars by produceState(initialValue = FloatArray(BARS), live) {
        driveSpectrum(live, { viewModel.features.value.bands }) { value = it }
    }
    Box(modifier.glassSurface(shape = GlassShapes.tile).height(60.dp).padding(10.dp)) {
        androidx.compose.foundation.Canvas(Modifier.fillMaxSize()) {
            val gap = size.width / (BARS * 6f)
            val barWidth = (size.width - gap * (BARS - 1)) / BARS
            val brush =
                Brush.verticalGradient(
                    listOf(GlassPalette.mint.copy(alpha = 0.9f), GlassPalette.lavender.copy(alpha = 0.6f)),
                )
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
    state: PlayerUiState,
    micActive: Boolean,
    external: ExternalAudioState,
    sleepRunning: Boolean,
    canShuffle: Boolean,
) {
    val context = androidx.compose.ui.platform.LocalContext.current
    val micPermission =
        androidx.activity.compose.rememberLauncherForActivityResult(
            androidx.activity.result.contract.ActivityResultContracts
                .RequestPermission(),
        ) { granted -> if (granted) viewModel.setMicEnabled(true) }
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
                GlassIcons.Mic,
                stringResource(if (micActive) R.string.quick_room_on else R.string.source_live_input),
                active = micActive,
            ) {
                if (micActive) {
                    viewModel.setMicEnabled(false)
                } else if (viewModel.hasMicPermission()) {
                    viewModel.setMicEnabled(true)
                } else {
                    micPermission.launch(android.Manifest.permission.RECORD_AUDIO)
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
            QuickAction(Icons.Filled.Shuffle, stringResource(R.string.action_shuffle), active = state.shuffle) {
                viewModel.toggleShuffle()
            }
        }
        item {
            QuickAction(
                Icons.Filled.Repeat,
                stringResource(R.string.action_repeat),
                active = state.repeatMode != Player.REPEAT_MODE_OFF,
            ) {
                viewModel.cycleRepeatMode()
            }
        }
        item {
            QuickAction(Icons.Filled.History, stringResource(R.string.quick_shuffle_all), enabled = canShuffle) {
                viewModel.shuffleAllHistory()
            }
        }
    }
}

@Composable
private fun QuickAction(
    icon: ImageVector,
    label: String,
    enabled: Boolean = true,
    active: Boolean = false,
    onClick: () -> Unit,
) {
    val tint =
        when {
            !enabled -> GlassPalette.textSecondary.copy(alpha = 0.4f)
            active -> GlassPalette.mint
            else -> GlassPalette.textPrimary
        }
    Column(
        Modifier.width(84.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Box(
            Modifier
                .size(56.dp)
                .glassSurface(shape = GlassShapes.bubble, tint = if (active) GlassPalette.mint else null, glow = if (active) 0.5f else 0f)
                .floatOnWater(strength = 0.6f)
                .glassTouch(enabled = enabled, onClick = onClick),
            contentAlignment = Alignment.Center,
        ) {
            Icon(icon, contentDescription = null, tint = tint, modifier = Modifier.size(22.dp))
        }
        Text(
            label,
            style = MaterialTheme.typography.labelSmall,
            color = tint,
            maxLines = 1,
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

private val HERO_BUBBLE_SIZE = 240.dp
private val HERO_ARTWORK_CORNER = 999.dp
