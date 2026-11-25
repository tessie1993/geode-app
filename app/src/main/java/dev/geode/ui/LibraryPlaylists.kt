package dev.geode.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGesturesAfterLongPress
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.DragHandle
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.collapse
import androidx.compose.ui.semantics.expand
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.zIndex
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.MusicPlaylist
import dev.geode.data.PlaylistImportResult
import dev.geode.model.LibraryTrack
import dev.geode.playback.PlaybackQueue
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import kotlinx.coroutines.launch
import kotlin.math.roundToInt

/** Every favourite, above the hand-made playlists: opens in place like one, and plays newest first. */
@Composable
private fun FavouritesRow(
    count: Int,
    open: Boolean,
    onToggle: () -> Unit,
    onPlay: () -> Unit,
) {
    Row(
        Modifier
            .fillMaxWidth()
            .clickable(onClick = onToggle)
            .semantics {
                if (open) {
                    collapse {
                        onToggle()
                        true
                    }
                } else {
                    expand {
                        onToggle()
                        true
                    }
                }
            }.padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        StoneIconArt(StoneIcon.FAVORITE, null, tint = MaterialTheme.colorScheme.primary, selected = true)
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(stringResource(R.string.auto_favourites))
            Text(pluralStringResource(R.plurals.track_count, count, count), style = MaterialTheme.typography.bodySmall)
        }
        IconButton(onClick = onPlay, enabled = count > 0) {
            StoneIconArt(StoneIcon.PLAY, stringResource(R.string.action_play))
        }
    }
}

@Composable
internal fun PlaylistsTab(
    viewModel: LibraryViewModel,
    playerViewModel: PlayerViewModel,
    wide: Boolean = false,
) {
    val library by viewModel.library.collectAsStateWithLifecycle()
    val favourites by viewModel.favourites.collectAsStateWithLifecycle()
    val deviceTracks by viewModel.deviceTracks.collectAsStateWithLifecycle()
    val overrides by viewModel.trackOverrides.collectAsStateWithLifecycle()
    val favouriteTracks = remember(favourites, deviceTracks) { LibraryBrowse.favouriteTracks(favourites, deviceTracks) }
    val favouriteQueue = remember(favouriteTracks) { favouriteTracks.map(PlaybackQueue::queueTrack) }
    var favouritesOpen by rememberSaveable { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    var expanded by remember { mutableStateOf<String?>(null) }
    var renaming by remember { mutableStateOf<String?>(null) }
    var renameText by remember { mutableStateOf("") }
    var creating by remember { mutableStateOf(false) }
    var deleting by remember { mutableStateOf<String?>(null) }
    var importResult by remember { mutableStateOf<PlaylistImportResult?>(null) }
    val importPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) {
                scope.launch { importResult = viewModel.importPlaylistFile(uri) }
            }
        }
    Column {
        Row(
            Modifier.padding(horizontal = 16.dp, vertical = 2.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            FilledTonalButton(
                onClick = { creating = true },
                contentPadding = PaddingValues(horizontal = GeodeSpacing.M, vertical = GeodeSpacing.XS),
            ) { Text(stringResource(R.string.playlist_new)) }
            FilledTonalButton(
                onClick = { importPicker.launch(arrayOf("*/*")) },
                contentPadding = PaddingValues(horizontal = GeodeSpacing.M, vertical = GeodeSpacing.XS),
            ) { Text(stringResource(R.string.playlist_import_action)) }
        }
        // A wide pane puts the smart playlists beside the hand-made ones instead of on top of
        // them, so neither list is pushed below the fold of a short landscape screen.
        Row(Modifier.fillMaxSize()) {
            // Three kinds of item share this list, so each declares its type and lazy layout reuses
            // a playlist row's slot only for another playlist row. Playlist keys carry a prefix so a
            // playlist named like the fixed items cannot collide with them.
            LazyColumn(Modifier.weight(1f).fillMaxHeight()) {
                if (!wide) item(key = "smart", contentType = "smart") { SmartPlaylistsSection(viewModel) }
                item(key = "favourites", contentType = "favourites") {
                    FavouritesRow(
                        count = favouriteTracks.size,
                        open = favouritesOpen,
                        onToggle = { favouritesOpen = !favouritesOpen },
                        onPlay = viewModel::playFavourites,
                    )
                }
                if (favouritesOpen) {
                    items(favouriteTracks, key = { "favourite:${it.uri}" }, contentType = { "track" }) { t ->
                        TrackRow(t, playerViewModel, viewModel, overrides[t.uri], favourite = true, queue = favouriteQueue)
                    }
                    if (favouriteTracks.isEmpty()) {
                        item(key = "favourites-empty", contentType = "empty") {
                            Text(
                                stringResource(R.string.library_favourites_empty),
                                Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }
                items(library.playlists, key = { "playlist:${it.name}" }, contentType = { "playlist" }) { pl ->
                    Column(Modifier.fillMaxWidth()) {
                        Row(
                            Modifier
                                .fillMaxWidth()
                                .clickable { expanded = if (expanded == pl.name) null else pl.name }
                                .padding(horizontal = 16.dp, vertical = 12.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Column(Modifier.weight(1f)) {
                                Text(pl.name)
                                Text(
                                    pluralStringResource(
                                        R.plurals.track_count,
                                        pl.trackUris.size,
                                        pl.trackUris.size,
                                    ),
                                    style = MaterialTheme.typography.bodySmall,
                                )
                            }
                            IconButton(onClick = {
                                renaming = pl.name
                                renameText = pl.name
                            }) { StoneIconArt(StoneIcon.EDIT, stringResource(R.string.action_rename)) }
                            IconButton(onClick = { viewModel.playPlaylist(pl.name) }) {
                                StoneIconArt(StoneIcon.PLAY, stringResource(R.string.action_play))
                            }
                            IconButton(onClick = { deleting = pl.name }) {
                                StoneIconArt(StoneIcon.CLOSE, stringResource(R.string.playlist_delete_title))
                            }
                        }
                        if (expanded == pl.name) {
                            PlaylistTracks(pl, library.tracks, viewModel)
                        }
                    }
                }
                if (library.playlists.isEmpty()) {
                    item(key = "empty", contentType = "empty") {
                        Text(
                            stringResource(R.string.playlist_none_yet),
                            Modifier.padding(16.dp),
                        )
                    }
                }
            }
            if (wide) {
                Column(Modifier.weight(1f).fillMaxHeight().verticalScroll(rememberScrollState())) {
                    SmartPlaylistsSection(viewModel)
                }
            }
        }
    }
    if (creating) {
        PlaylistNameDialog(
            title = stringResource(R.string.playlist_new),
            confirmLabel = stringResource(R.string.action_create),
            taken = library.playlists.map { it.name }.toSet(),
            onName = { name -> viewModel.createMusicPlaylist(name) },
            onDismiss = { creating = false },
        )
    }
    deleting?.let { doomed ->
        AlertDialog(
            onDismissRequest = { deleting = null },
            title = { Text(stringResource(R.string.playlist_delete_title)) },
            text = { Text(stringResource(R.string.playlist_delete_body, doomed)) },
            confirmButton = {
                Button(onClick = {
                    viewModel.deleteMusicPlaylist(doomed)
                    deleting = null
                }) { Text(stringResource(R.string.action_delete)) }
            },
            dismissButton = { TextButton(onClick = { deleting = null }) { Text(stringResource(R.string.action_cancel)) } },
        )
    }
    renaming?.let { old ->
        val proposed = renameText.trim()
        val otherNames =
            library.playlists
                .map { it.name }
                .filterNot { it == old }
                .toSet()
        val nameOk = playlistNameAccepted(proposed, otherNames)
        AlertDialog(
            onDismissRequest = { renaming = null },
            title = { Text(stringResource(R.string.playlist_rename_title)) },
            text = {
                Column {
                    OutlinedTextField(value = renameText, onValueChange = { renameText = it }, singleLine = true)
                    if (!nameOk) {
                        Text(
                            if (proposed.isEmpty()) {
                                stringResource(R.string.playlist_name_required)
                            } else {
                                stringResource(R.string.playlist_name_taken, proposed)
                            },
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.error,
                        )
                    }
                }
            },
            confirmButton = {
                Button(enabled = nameOk, onClick = {
                    viewModel.renameMusicPlaylist(old, proposed)
                    renaming = null
                }) { Text(stringResource(R.string.action_rename)) }
            },
            dismissButton = { TextButton(onClick = { renaming = null }) { Text(stringResource(R.string.action_cancel)) } },
        )
    }
    importResult?.let { result ->
        AlertDialog(
            onDismissRequest = { importResult = null },
            title = {
                Text(
                    stringResource(
                        if (result is PlaylistImportResult.Imported) {
                            R.string.playlist_import_done_title
                        } else {
                            R.string.playlist_import_failed_title
                        },
                    ),
                )
            },
            text = {
                Text(
                    when (result) {
                        is PlaylistImportResult.Imported ->
                            stringResource(
                                R.string.playlist_import_summary,
                                result.name,
                                result.addedCount,
                                result.unresolvedCount,
                                result.ambiguousCount,
                            )
                        // result.why is a diagnostic detail, not user-facing text; the dialog always
                        // shows one generic, localized failure message instead.
                        is PlaylistImportResult.Failed -> stringResource(R.string.playlist_import_failed_body)
                    },
                )
            },
            confirmButton = { TextButton(onClick = { importResult = null }) { Text(stringResource(R.string.action_ok)) } },
        )
    }
}

internal fun playlistDropIndex(
    from: Int,
    offsetPx: Float,
    rowHeightPx: Int,
    count: Int,
): Int {
    if (count <= 0) return from
    if (rowHeightPx <= 0) return from.coerceIn(0, count - 1)
    return (from + (offsetPx / rowHeightPx).roundToInt()).coerceIn(0, count - 1)
}

internal fun playlistRowShift(
    index: Int,
    from: Int,
    to: Int,
): Int =
    when {
        index == from -> 0
        from < to && index in (from + 1)..to -> -1
        from > to && index in to until from -> 1
        else -> 0
    }

@Composable
private fun PlaylistTracks(
    playlist: MusicPlaylist,
    tracks: List<LibraryTrack>,
    viewModel: LibraryViewModel,
) {
    val count = playlist.trackUris.size
    var dragFrom by remember(playlist.name) { mutableIntStateOf(-1) }
    var dragOffset by remember(playlist.name) { mutableFloatStateOf(0f) }
    var rowHeight by remember(playlist.name) { mutableIntStateOf(0) }
    // dragOffset changes on every pointer event; read through derivedStateOf, the rows recompose
    // only when the drop slot moves, while the lifted row follows the finger in graphicsLayer.
    // Keyed like the states it reads, so another playlist never sees this one's drag.
    val dropIndex by remember(playlist.name, count) {
        derivedStateOf { playlistDropIndex(dragFrom, dragOffset, rowHeight, count) }
    }
    // One pass over the library instead of a linear search per row.
    val byUri = remember(tracks) { tracks.associateBy { it.uri } }
    val liftTint = MaterialTheme.colorScheme.primary.copy(alpha = 0.14f)
    // The neighbours slide aside on the motion scheme's fast spatial spring, so with the
    // system's "Remove animations" on they snap into place instead of springing.
    val motion = LocalGeodeMotion.current
    playlist.trackUris.forEachIndexed { i, uri ->
        val title = byUri[uri]?.title ?: stringResource(R.string.playlist_untitled_track, i + 1)
        val dragging = i == dragFrom
        val shift by animateFloatAsState(
            if (dragFrom < 0) 0f else (playlistRowShift(i, dragFrom, dropIndex) * rowHeight).toFloat(),
            motion.fastSpatialSpec(),
            label = "playlistRowShift",
        )
        Row(
            Modifier
                .fillMaxWidth()
                .zIndex(if (dragging) 1f else 0f)
                .graphicsLayer { translationY = if (dragging) dragOffset else shift }
                .then(if (dragging) Modifier.background(liftTint) else Modifier)
                .onSizeChanged { rowHeight = it.height }
                .pointerInput(playlist.name, i, count) {
                    detectDragGesturesAfterLongPress(
                        onDragStart = {
                            dragFrom = i
                            dragOffset = 0f
                        },
                        onDragEnd = {
                            val to = playlistDropIndex(i, dragOffset, rowHeight, count)
                            if (to != i) viewModel.moveMusicPlaylistTrack(playlist.name, i, to)
                            dragFrom = -1
                            dragOffset = 0f
                        },
                        onDragCancel = {
                            dragFrom = -1
                            dragOffset = 0f
                        },
                    ) { change, drag ->
                        change.consume()
                        dragOffset += drag.y
                    }
                }.padding(start = 28.dp, end = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                Icons.Filled.DragHandle,
                null,
                Modifier.size(18.dp).padding(end = 2.dp),
                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
            )
            Text(
                title,
                Modifier.weight(1f).padding(start = 6.dp),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = MaterialTheme.typography.bodySmall,
            )
            // Named per track: a run of bare "Up" buttons tells a screen reader nothing about what moves.
            IconButton(
                onClick = { viewModel.moveMusicPlaylistTrack(playlist.name, i, i - 1) },
                enabled = i > 0,
            ) { Icon(Icons.Filled.KeyboardArrowUp, stringResource(R.string.library_move_track_up, title)) }
            IconButton(
                onClick = { viewModel.moveMusicPlaylistTrack(playlist.name, i, i + 1) },
                enabled = i < count - 1,
            ) { Icon(Icons.Filled.KeyboardArrowDown, stringResource(R.string.library_move_track_down, title)) }
            IconButton(onClick = { viewModel.removeTrackFromPlaylist(playlist.name, uri) }) {
                StoneIconArt(StoneIcon.CLOSE, stringResource(R.string.action_remove_from_playlist), Modifier.size(18.dp))
            }
        }
    }
}
