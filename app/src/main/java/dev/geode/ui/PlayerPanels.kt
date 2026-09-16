package dev.geode.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.interaction.DragInteraction
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
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
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.ui.glass.GlassButton
import dev.geode.ui.glass.GlassIcons
import dev.geode.ui.glass.GlassListRow
import dev.geode.ui.glass.GlassPalette
import dev.geode.ui.glass.GlassShapes
import dev.geode.ui.glass.GlassSheet
import dev.geode.ui.glass.GlassTextField
import dev.geode.ui.glass.glassSurface
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collectLatest

/** A pastel glass tube: the same waveform drawing as before, restyled onto a [glassSurface] track
 * (ref video-v3/v4: the seek bar reads as a frosted tube with an iridescent fill). */
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
    val idle = GlassPalette.textSecondary.copy(alpha = 0.4f)
    val loopTint = GlassPalette.lavender.copy(alpha = 0.3f)
    val playhead = GlassPalette.textPrimary
    val positionLabel = formatClock(if (dragFraction >= 0f) (dragFraction * durationMs).toLong() else positionMs)
    val durationLabel = formatClock(durationMs)
    val seekDescription = stringResource(R.string.seek_description, positionLabel, durationLabel)
    Canvas(
        modifier
            .glassSurface(shape = GlassShapes.pill)
            .semantics {
                contentDescription = seekDescription
                progressBarRangeInfo = ProgressBarRangeInfo(played, 0f..1f)
                setProgress { target ->
                    onSeek(target.coerceIn(0f, 1f))
                    true
                }
            }.pointerInput(durationMs) {
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
            drawRoundRect(seekFillBrush(size.width * played), Offset(0f, y), Size(size.width * played, h), CornerRadius(h / 2f))
        } else {
            val n = waveform.size
            val slot = size.width / n
            val barWidth = (slot * 0.62f).coerceAtLeast(1f)
            val playedX = size.width * played
            for (i in 0 until n) {
                val h = (size.height * (0.08f + 0.92f * waveform[i])).coerceAtLeast(2f)
                val x = i * slot + (slot - barWidth) / 2f
                val filled = x + barWidth / 2f <= playedX
                drawRoundRect(
                    color = if (filled) GlassPalette.mint else idle,
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

private fun androidx.compose.ui.graphics.drawscope.DrawScope.seekFillBrush(width: Float) =
    androidx.compose.ui.graphics.Brush.horizontalGradient(
        listOf(GlassPalette.mint, GlassPalette.lavender, GlassPalette.peach),
        endX = width.coerceAtLeast(1f),
    )

/** The lyrics overlay as a [GlassSheet]: glass rows, the active line tinted mint. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LyricsPanel(
    lyrics: Lyrics?,
    positionMs: Long,
    onSeek: (Long) -> Unit,
    onDismissRequest: () -> Unit,
    modifier: Modifier = Modifier,
) {
    GlassSheet(onDismissRequest = onDismissRequest, modifier = modifier) {
        if (lyrics == null) {
            Column(
                Modifier.padding(24.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Text(
                    stringResource(R.string.lyrics_none),
                    style = MaterialTheme.typography.titleMedium,
                    color = GlassPalette.textPrimary,
                )
                Text(
                    stringResource(R.string.lyrics_none_explainer),
                    style = MaterialTheme.typography.bodySmall,
                    color = GlassPalette.textSecondary,
                )
            }
            return@GlassSheet
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
            Modifier.fillMaxWidth(),
            state = listState,
            contentPadding =
                androidx.compose.foundation.layout
                    .PaddingValues(horizontal = 20.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            itemsIndexed(lyrics.lines) { index, line ->
                val active = index == current
                GlassListRow(
                    title = line.text,
                    selected = active,
                    onClick =
                        if (lyrics.synced) {
                            {
                                follows.value = true
                                onSeek(line.timeMs)
                            }
                        } else {
                            null
                        },
                )
            }
            item {
                Text(
                    stringResource(
                        if (lyrics.synced) R.string.lyrics_source_timed else R.string.lyrics_source_untimed,
                        lyrics.source,
                    ),
                    Modifier.padding(top = 12.dp, bottom = 24.dp),
                    style = MaterialTheme.typography.labelSmall,
                    color = GlassPalette.textSecondary,
                )
            }
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

/** The queue overlay as a [GlassSheet]: glass rows with artwork, reorder/remove, save-as-playlist. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun QueuePanel(
    queue: QueueUiState,
    favourites: Set<String>,
    onPlayIndex: (Int) -> Unit,
    onMoveUp: (Int) -> Unit,
    onRemove: (Int) -> Unit,
    onDismissRequest: () -> Unit,
    modifier: Modifier = Modifier,
    viewModel: LibraryViewModel = geodeViewModel(),
) {
    GlassSheet(onDismissRequest = onDismissRequest, modifier = modifier) {
        if (queue.tracks.isEmpty()) {
            Column(Modifier.padding(24.dp)) {
                Text(stringResource(R.string.queue), style = MaterialTheme.typography.titleMedium, color = GlassPalette.textPrimary)
                Text(
                    stringResource(R.string.queue_empty_explainer),
                    style = MaterialTheme.typography.bodySmall,
                    color = GlassPalette.textSecondary,
                )
            }
            return@GlassSheet
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
        Column(Modifier.fillMaxSize()) {
            Row(
                Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 6.dp),
                verticalAlignment = androidx.compose.ui.Alignment.CenterVertically,
            ) {
                Text(
                    stringResource(R.string.queue),
                    Modifier.weight(1f),
                    style = MaterialTheme.typography.titleMedium,
                    color = GlassPalette.textPrimary,
                )
                GlassButton(
                    text = stringResource(R.string.queue_save_as_playlist),
                    onClick = { saving = true },
                )
            }
            LazyColumn(
                Modifier.fillMaxWidth(),
                state = listState,
                contentPadding =
                    androidx.compose.foundation.layout
                        .PaddingValues(horizontal = 16.dp, vertical = 8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                itemsIndexed(queue.tracks, key = { i, _ -> keys[i] }) { index, track ->
                    val playing = index == queue.index
                    val favouriteMark = "★".takeIf { track.uri in favourites }
                    GlassListRow(
                        title = track.title,
                        subtitle =
                            listOfNotNull(track.artist.takeIf { it.isNotBlank() }, favouriteMark)
                                .joinToString("  ")
                                .ifBlank { stringResource(R.string.subtitle_unknown_artist) },
                        leading = { TrackArtwork(track.uri, Modifier.fillMaxSize(), corner = 14.dp) },
                        trailing = {
                            Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                                if (index > 0) {
                                    IconRowButton(
                                        icon = androidx.compose.material.icons.Icons.Filled.KeyboardArrowUp,
                                        contentDescription = stringResource(R.string.action_move_up),
                                        onClick = { onMoveUp(index) },
                                    )
                                }
                                IconRowButton(
                                    icon = GlassIcons.Close,
                                    contentDescription = stringResource(R.string.action_remove_from_queue),
                                    onClick = { onRemove(index) },
                                )
                            }
                        },
                        selected = playing,
                        onClick = {
                            follows.value = true
                            onPlayIndex(index)
                        },
                    )
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
}

@Composable
private fun IconRowButton(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    contentDescription: String?,
    onClick: () -> Unit,
) {
    Icon(
        icon,
        contentDescription,
        Modifier
            .size(28.dp)
            .clickable(onClick = onClick)
            .padding(4.dp),
        tint = GlassPalette.textSecondary,
    )
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

/** The playlist-name prompt as a glass card [Dialog], the same frosted-tile look as `GlassDialog`
 * with a [GlassTextField] body — `GlassDialog` itself only takes a plain title/text string. */
@Composable
internal fun PlaylistNameDialog(
    title: String,
    confirmLabel: String,
    taken: Set<String>,
    onName: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var name by rememberSaveable { mutableStateOf("") }
    androidx.compose.ui.window.Dialog(onDismissRequest = onDismiss) {
        Column(
            Modifier
                .fillMaxWidth()
                .glassSurface(shape = GlassShapes.tile)
                .padding(24.dp),
        ) {
            Text(title, style = MaterialTheme.typography.titleLarge, color = GlassPalette.textPrimary)
            GlassTextField(
                value = name,
                onValueChange = { name = it },
                modifier = Modifier.fillMaxWidth().padding(top = 16.dp),
            )
            Row(
                Modifier.fillMaxWidth().padding(top = 20.dp),
                horizontalArrangement = Arrangement.End,
            ) {
                GlassButton(text = stringResource(R.string.action_cancel), onClick = onDismiss)
                androidx.compose.foundation.layout
                    .Spacer(Modifier.size(12.dp))
                GlassButton(
                    text = confirmLabel,
                    enabled = playlistNameAccepted(name, taken),
                    onClick = {
                        onName(name.trim())
                        onDismiss()
                    },
                )
            }
        }
    }
}
