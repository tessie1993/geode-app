package dev.geode.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.interaction.DragInteraction
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import dev.geode.R
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collectLatest

@Composable
fun WaveformSeekBar(
    waveform: FloatArray?,
    positionMs: Long,
    durationMs: Long,
    loopStartMs: Long?,
    loopEndMs: Long?,
    onSeek: (Float) -> Unit,
    modifier: Modifier = Modifier,
) {
    var dragFraction by remember { mutableFloatStateOf(-1f) }
    val played =
        if (dragFraction >= 0f) {
            dragFraction
        } else if (durationMs > 0) {
            (positionMs / durationMs.toFloat()).coerceIn(0f, 1f)
        } else {
            0f
        }
    val primary = MaterialTheme.colorScheme.primary
    val idle = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.45f)
    val loopTint = MaterialTheme.colorScheme.secondary.copy(alpha = 0.28f)
    val playhead = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.85f)
    val positionLabel = formatClock(if (dragFraction >= 0f) (dragFraction * durationMs).toLong() else positionMs)
    val durationLabel = formatClock(durationMs)
    val seekDescription = stringResource(R.string.seek_description, positionLabel, durationLabel)
    Canvas(
        modifier
            .semantics {
                contentDescription = seekDescription
                progressBarRangeInfo = ProgressBarRangeInfo(played, 0f..1f)
                setProgress { target ->
                    onSeek(target.coerceIn(0f, 1f))
                    true
                }
            }
            .pointerInput(durationMs) {
                detectTapGestures { offset ->
                    onSeek((offset.x / size.width.toFloat()).coerceIn(0f, 1f))
                }
            }.pointerInput(durationMs) {
                detectHorizontalDragGestures(
                    onDragStart = { offset ->
                        dragFraction = (offset.x / size.width.toFloat()).coerceIn(0f, 1f)
                    },
                    onDragEnd = {
                        if (dragFraction >= 0f) onSeek(dragFraction)
                        dragFraction = -1f
                    },
                    onDragCancel = { dragFraction = -1f },
                ) { change, dragAmount ->
                    change.consume()
                    dragFraction = (dragFraction + dragAmount / size.width.toFloat()).coerceIn(0f, 1f)
                }
            },
    ) {
        if (loopStartMs != null && durationMs > 0) {
            val from = (loopStartMs / durationMs.toFloat()).coerceIn(0f, 1f) * size.width
            val to = ((loopEndMs ?: durationMs) / durationMs.toFloat()).coerceIn(0f, 1f) * size.width
            drawRect(loopTint, topLeft = Offset(from, 0f), size = Size((to - from).coerceAtLeast(2f), size.height))
        }
        if (waveform == null || waveform.isEmpty()) {
            val h = 3.dp.toPx()
            val y = size.height / 2f - h / 2f
            drawRoundRect(idle, Offset(0f, y), Size(size.width, h), CornerRadius(h / 2f))
            drawRoundRect(primary, Offset(0f, y), Size(size.width * played, h), CornerRadius(h / 2f))
        } else {
            val n = waveform.size
            val slot = size.width / n
            val barWidth = (slot * 0.62f).coerceAtLeast(1f)
            val playedX = size.width * played
            for (i in 0 until n) {
                val h = (size.height * (0.08f + 0.92f * waveform[i])).coerceAtLeast(2f)
                val x = i * slot + (slot - barWidth) / 2f
                drawRoundRect(
                    color = if (x + barWidth / 2f <= playedX) primary else idle,
                    topLeft = Offset(x, (size.height - h) / 2f),
                    size = Size(barWidth, h),
                    cornerRadius = CornerRadius(barWidth / 2f),
                )
            }
        }
        val x = (size.width * played).coerceIn(1f, maxOf(1f, size.width - 1f))
        drawRoundRect(
            playhead,
            topLeft = Offset(x - 1.dp.toPx(), 0f),
            size = Size(2.dp.toPx(), size.height),
            cornerRadius = CornerRadius(1.dp.toPx()),
        )
    }
}

@Composable
fun LyricsPanel(
    lyrics: Lyrics?,
    positionMs: Long,
    onSeek: (Long) -> Unit,
    modifier: Modifier = Modifier,
) {
    if (lyrics == null) {
        Column(
            modifier.padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            CrystalOverline(stringResource(R.string.lyrics_none))
            Text(
                stringResource(R.string.lyrics_none_explainer),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        return
    }
    val current = lyrics.indexAt(positionMs)
    val listState = rememberLazyListState()
    val follows = rememberFollowsPlayback(listState)
    LaunchedEffect(current, follows.value) {
        if (follows.value && current >= 0) {
            listState.animateScrollToItem(current.coerceAtLeast(0), scrollOffset = -SCROLL_LEAD_PX)
        }
    }
    LazyColumn(
        modifier,
        state = listState,
        contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 24.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        itemsIndexed(lyrics.lines) { index, line ->
            val active = index == current
            Text(
                line.text,
                Modifier
                    .fillMaxWidth()
                    .then(
                        if (lyrics.synced) {
                            Modifier.clickable {
                                follows.value = true
                                onSeek(line.timeMs)
                            }
                        } else {
                            Modifier
                        },
                    ),
                style =
                    if (active) {
                        MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold)
                    } else {
                        MaterialTheme.typography.bodyMedium
                    },
                color =
                    when {
                        active -> accentTextColor()
                        !lyrics.synced -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.85f)
                        index < current -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.35f)
                        else -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.65f)
                    },
            )
        }
        item {
            Text(
                stringResource(
                    if (lyrics.synced) R.string.lyrics_source_timed else R.string.lyrics_source_untimed,
                    lyrics.source,
                ),
                Modifier.padding(top = 16.dp),
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

private const val SCROLL_LEAD_PX = 160

private const val FOLLOW_RESUME_DELAY_MS = 5_000L

@Composable
private fun rememberFollowsPlayback(listState: LazyListState): MutableState<Boolean> {
    val follows = remember { mutableStateOf(true) }
    LaunchedEffect(listState) {
        listState.interactionSource.interactions.collectLatest { interaction ->
            if (interaction is DragInteraction.Start) {
                follows.value = false
            } else {
                delay(FOLLOW_RESUME_DELAY_MS)
                follows.value = true
            }
        }
    }
    return follows
}

@Composable
fun QueuePanel(
    queue: QueueUiState,
    favourites: Set<String>,
    onPlayIndex: (Int) -> Unit,
    onMoveUp: (Int) -> Unit,
    onRemove: (Int) -> Unit,
    modifier: Modifier = Modifier,
    viewModel: LibraryViewModel = geodeViewModel(),
) {
    if (queue.tracks.isEmpty()) {
        Column(modifier.padding(24.dp)) {
            CrystalOverline(stringResource(R.string.queue))
            Text(
                stringResource(R.string.queue_empty_explainer),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        return
    }
    val library by viewModel.library.collectAsStateWithLifecycle()
    var saving by rememberSaveable { mutableStateOf(false) }
    val listState = rememberLazyListState()
    val follows = rememberFollowsPlayback(listState)
    LaunchedEffect(queue.index, follows.value) {
        if (follows.value) {
            listState.animateScrollToItem(queue.index.coerceIn(0, queue.tracks.lastIndex))
        }
    }
    val keys = remember(queue.tracks) { queueRowKeys(queue.tracks) }
    Column(modifier) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CrystalOverline(stringResource(R.string.queue), Modifier.weight(1f))
            CrystalButton(
                compact = true,
                filled = false,
                onClick = { saving = true },
            ) { Text(stringResource(R.string.queue_save_as_playlist)) }
        }
        LazyColumn(
            Modifier.weight(1f).fillMaxWidth(),
            state = listState,
            contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 12.dp, vertical = 8.dp),
        ) {
            itemsIndexed(queue.tracks, key = { i, _ -> keys[i] }) { index, track ->
                val playing = index == queue.index
                Row(
                    Modifier
                        .fillMaxWidth()
                        .clickable {
                            follows.value = true
                            onPlayIndex(index)
                        }.padding(vertical = 6.dp, horizontal = 4.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    TrackArtwork(track.uri, Modifier.size(40.dp), corner = 8.dp)
                    Column(Modifier.weight(1f).padding(horizontal = 10.dp)) {
                        Text(
                            track.title,
                            style = MaterialTheme.typography.bodyMedium,
                            color = if (playing) accentTextColor() else MaterialTheme.colorScheme.onSurface,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                        Text(
                            listOfNotNull(
                                track.artist.takeIf { it.isNotBlank() },
                                "★".takeIf { track.uri in favourites },
                            ).joinToString("  ")
                                .ifBlank { stringResource(R.string.subtitle_unknown_artist) },
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                    if (index > 0) {
                        IconButton(onClick = { onMoveUp(index) }) {
                            Icon(Icons.Filled.KeyboardArrowUp, stringResource(R.string.action_move_up), Modifier.size(18.dp))
                        }
                    }
                    IconButton(onClick = { onRemove(index) }) {
                        StoneIconArt(StoneIcon.CLOSE, stringResource(R.string.action_remove_from_queue), Modifier.size(18.dp))
                    }
                }
            }
        }
    }
    if (saving) {
        PlaylistNameDialog(
            title = stringResource(R.string.queue_save_dialog_title),
            confirmLabel = stringResource(R.string.action_save),
            taken = library.playlists.map { it.name }.toSet(),
            onName = { name ->
                viewModel.createMusicPlaylist(name)
                queue.tracks.forEach { viewModel.addTrackToPlaylist(name, it.uri) }
            },
            onDismiss = { saving = false },
        )
    }
}

internal fun queueRowKeys(tracks: List<QueueTrack>): List<String> {
    val seen = HashMap<String, Int>()
    return tracks.map { t ->
        val n = seen[t.uri] ?: 0
        seen[t.uri] = n + 1
        if (n == 0) t.uri else "${t.uri}#$n"
    }
}

internal fun playlistNameAccepted(
    name: String,
    existing: Collection<String>,
): Boolean = name.isNotBlank() && name.trim() !in existing

@Composable
internal fun PlaylistNameDialog(
    title: String,
    confirmLabel: String,
    taken: Set<String>,
    onName: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var name by rememberSaveable { mutableStateOf("") }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = { OutlinedTextField(value = name, onValueChange = { name = it }, singleLine = true) },
        confirmButton = {
            CrystalButton(
                enabled = playlistNameAccepted(name, taken),
                onClick = {
                    onName(name.trim())
                    onDismiss()
                },
            ) { Text(confirmLabel) }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) } },
    )
}

@Composable
fun PlayerPanelSurface(
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    Box(
        modifier
            .crystalPanel(
                0.55f,
                MaterialTheme.colorScheme.surface,
                MaterialTheme.colorScheme.primary,
                corner = 20.dp,
                glowStrength = 0.8f,
            ),
    ) {
        Box(Modifier.fillMaxSize()) { content() }
    }
}
