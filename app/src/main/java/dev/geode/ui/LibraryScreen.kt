package dev.geode.ui

import android.Manifest
import android.net.Uri
import android.os.Build
import androidx.activity.compose.LocalActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGesturesAfterLongPress
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.DragHandle
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.zIndex
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.LifecycleResumeEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.MusicPlaylist
import dev.geode.model.DeviceTrack
import dev.geode.model.LibraryTrack
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import kotlinx.coroutines.launch
import kotlin.math.roundToInt

/** Playlists sit last and behave differently: hand-ordered, so search and sort do not apply. */
private const val PLAYLISTS_TAB = 4
private const val DUPLICATES_TAB = 5

@Composable
fun LibraryScreen(onOpenSearch: () -> Unit) {
    val libraryViewModel: LibraryViewModel = geodeViewModel()
    val playerViewModel: PlayerViewModel = geodeViewModel()
    val context = LocalContext.current
    val permission =
        if (Build.VERSION.SDK_INT >= 33) Manifest.permission.READ_MEDIA_AUDIO else Manifest.permission.READ_EXTERNAL_STORAGE
    var granted by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(context, permission) == android.content.pm.PackageManager.PERMISSION_GRANTED,
        )
    }
    val permLauncher =
        rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted = it }
    // "Open settings" sends the user away to grant it; coming back doesn't recreate the screen,
    // so re-read the grant on every resume or the library stays empty until a restart.
    LifecycleResumeEffect(permission) {
        if (!granted && ContextCompat.checkSelfPermission(context, permission) == android.content.pm.PackageManager.PERMISSION_GRANTED) {
            granted = true
        }
        onPauseOrDispose { }
    }
    var reloadKey by remember { mutableStateOf(0) }
    val state by libraryViewModel.uiState.collectAsStateWithLifecycle()
    LaunchedEffect(granted, reloadKey) { if (granted) libraryViewModel.refreshDeviceTracks() }
    var tab by rememberSaveable { mutableStateOf(0) }
    val tabs =
        listOf(
            stringResource(R.string.library_tab_tracks),
            stringResource(R.string.library_tab_albums),
            stringResource(R.string.library_tab_artists),
            stringResource(R.string.library_tab_folders),
            stringResource(R.string.library_tab_playlists),
            stringResource(R.string.library_tab_duplicates),
        )
    // Already searched and sorted by the ViewModel — the screen draws what it is handed.
    val shown = state.tracks

    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                CrystalOverline(stringResource(R.string.app_name))
                GlowTitle(stringResource(R.string.nav_library))
            }
            IconButton(onClick = onOpenSearch) { StoneIconArt(StoneIcon.SEARCH, stringResource(R.string.action_search)) }
        }
        if (!granted) {
            val activity = LocalActivity.current
            var asked by rememberSaveable { mutableStateOf(false) }
            val canAskAgain =
                activity == null ||
                    !asked ||
                    ActivityCompat.shouldShowRequestPermissionRationale(activity, permission)
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text(
                    stringResource(
                        if (canAskAgain) {
                            R.string.library_permission_rationale
                        } else {
                            R.string.library_permission_denied_forever
                        },
                    ),
                )
                if (canAskAgain) {
                    CrystalButton(onClick = {
                        asked = true
                        permLauncher.launch(permission)
                    }) { Text(stringResource(R.string.library_permission_allow)) }
                } else {
                    val context = LocalContext.current
                    CrystalButton(
                        onClick = { context.openAppSettings() },
                    ) { Text(stringResource(R.string.library_permission_open_settings)) }
                }
            }
            return
        }
        CrystalTabs(titles = tabs, selected = tab, onSelect = { tab = it })
        // Search and sort belong to the track-shaped tabs. Playlists are ordered by hand, and
        // re-sorting someone's running order out from under them would be a bug, not a feature.
        if (tab != PLAYLISTS_TAB && tab != DUPLICATES_TAB) {
            OutlinedTextField(
                value = state.query,
                onValueChange = libraryViewModel::setQuery,
                singleLine = true,
                label = { Text(stringResource(R.string.library_search_hint)) },
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp),
            )
            // Chips in a scrolling row, not a segmented bar: equal-width segments split "Artist"
            // mid-word at large font scales, while a chip keeps its label whole and scrolls instead.
            Row(
                Modifier
                    .fillMaxWidth()
                    .selectableGroup()
                    .horizontalScroll(rememberScrollState())
                    .padding(horizontal = 16.dp, vertical = 4.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                LibrarySort.entries.forEach { sort ->
                    FilterChip(
                        selected = sort == state.sort,
                        onClick = { libraryViewModel.setSort(sort) },
                        label = { Text(stringResource(sort.labelRes), maxLines = 1, softWrap = false) },
                    )
                }
            }
        }
        // Grouping walks the whole library, so it runs when the shown tracks change, not on
        // every recomposition of the screen.
        when (tab) {
            0 -> TrackList(shown, playerViewModel, libraryViewModel, state.isSearching)
            1 -> GroupList(remember(shown) { shown.groupBy { it.album } }, playerViewModel, libraryViewModel)
            2 -> GroupList(remember(shown) { shown.groupBy { it.artist } }, playerViewModel, libraryViewModel)
            3 -> FoldersTab(remember(shown) { shown.groupBy { it.folder } }, playerViewModel)
            PLAYLISTS_TAB -> PlaylistsTab(libraryViewModel)
            DUPLICATES_TAB -> DuplicatesTab(state.tracks, playerViewModel, onDeleted = { reloadKey++ })
        }
    }
}

@Composable
private fun TrackList(
    tracks: List<DeviceTrack>,
    viewModel: PlayerViewModel,
    libraryViewModel: LibraryViewModel,
    searching: Boolean = false,
) {
    val queue = remember(tracks) { tracks.map(PlaybackQueue::queueTrack) }
    // Collected once for the list: per row it was a ViewModel lookup and a collector per item.
    val overrides by libraryViewModel.trackOverrides.collectAsStateWithLifecycle()
    LazyColumn(Modifier.fillMaxSize()) {
        items(tracks, key = { it.uri }, contentType = { "track" }) { t ->
            TrackRow(t, viewModel, libraryViewModel, overrides[t.uri], queue = queue)
        }
        if (tracks.isEmpty()) {
            // An empty library and an empty result set are different problems, and telling
            // someone "no music found" mid-search would send them looking for the wrong fix.
            val empty = if (searching) R.string.library_no_results else R.string.library_no_music
            item(key = "empty", contentType = "empty") { Text(stringResource(empty), Modifier.padding(16.dp)) }
        }
    }
}

@Composable
private fun TrackRow(
    t: DeviceTrack,
    viewModel: PlayerViewModel,
    libraryViewModel: LibraryViewModel,
    stored: LibraryTrack?,
    subtitleOverride: String? = null,
    queue: List<QueueTrack> = emptyList(),
) {
    val title = stored?.title?.ifBlank { null } ?: t.title
    // Title, artist, album, duration and artwork \u2014 and nothing else. No tempo, no key, no
    // analysed badge: the library never waits on analysis, so it has nothing to report about it.
    val subtitle =
        subtitleOverride
            ?: listOf(
                stored?.artist?.ifBlank { null } ?: t.artist,
                stored?.album?.ifBlank { null } ?: t.album,
            ).filter { it.isNotBlank() }.joinToString(" \u00b7 ")
    val duration = LibraryBrowse.formatDuration(t.durationMs)
    var menu by remember { mutableStateOf(false) }
    var editing by remember { mutableStateOf(false) }
    var addingToPlaylist by remember { mutableStateOf(false) }
    Row(
        Modifier
            .fillMaxWidth()
            .clickable {
                if (queue.isEmpty()) viewModel.playTrack(t.uri) else viewModel.playFrom(queue, t.uri)
            }.padding(horizontal = 16.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        TrackArtwork(t.uri, Modifier.size(44.dp), corner = 8.dp)
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(title, maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (subtitle.isNotBlank()) {
                Text(
                    subtitle,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        if (duration.isNotBlank()) {
            Text(
                duration,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(start = 8.dp),
            )
        }
        IconButton(onClick = { menu = true }) { Icon(Icons.Filled.MoreVert, stringResource(R.string.action_more)) }
        DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
            DropdownMenuItem(text = { Text(stringResource(R.string.action_play_next)) }, onClick = {
                viewModel.playNext(t.uri)
                menu = false
            })
            DropdownMenuItem(text = { Text(stringResource(R.string.action_add_to_queue)) }, onClick = {
                viewModel.enqueue(t.uri)
                menu = false
            })
            DropdownMenuItem(text = { Text(stringResource(R.string.action_add_to_playlist)) }, onClick = {
                addingToPlaylist = true
                menu = false
            })
            DropdownMenuItem(text = { Text(stringResource(R.string.action_add_to_library_list)) }, onClick = {
                libraryViewModel.importTracks(listOf(Uri.parse(t.uri)))
                menu = false
            })
            DropdownMenuItem(text = { Text(stringResource(R.string.action_edit_track_info)) }, onClick = {
                editing = true
                menu = false
            })
        }
    }
    if (editing) {
        TrackInfoEditor(uri = t.uri, viewModel = libraryViewModel, onDismiss = { editing = false })
    }
    if (addingToPlaylist) {
        AddToPlaylistDialog(uri = t.uri, viewModel = libraryViewModel, onDismiss = { addingToPlaylist = false })
    }
}

@Composable
private fun AddToPlaylistDialog(
    uri: String,
    viewModel: LibraryViewModel,
    onDismiss: () -> Unit,
) {
    val library by viewModel.library.collectAsStateWithLifecycle()
    var naming by remember { mutableStateOf(false) }
    if (naming) {
        PlaylistNameDialog(
            title = stringResource(R.string.playlist_new),
            confirmLabel = stringResource(R.string.action_create),
            taken = library.playlists.map { it.name }.toSet(),
            onName = { name ->
                viewModel.createMusicPlaylist(name)
                viewModel.addTrackToPlaylist(name, uri)
            },
            onDismiss = onDismiss,
        )
        return
    }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.action_add_to_playlist)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                library.playlists.forEach { pl ->
                    Text(
                        pl.name,
                        Modifier
                            .fillMaxWidth()
                            .clickable {
                                viewModel.addTrackToPlaylist(pl.name, uri)
                                onDismiss()
                            }.padding(vertical = 10.dp),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
                Text(
                    stringResource(R.string.playlist_new_branch),
                    Modifier.fillMaxWidth().clickable { naming = true }.padding(vertical = 10.dp),
                    color = accentTextColor(),
                )
            }
        },
        confirmButton = {},
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) } },
    )
}

@Composable
private fun GroupList(
    groups: Map<String, List<DeviceTrack>>,
    viewModel: PlayerViewModel,
    libraryViewModel: LibraryViewModel,
) {
    var open by rememberSaveable { mutableStateOf<String?>(null) }
    val sel = open
    val dismiss = rememberPredictiveDismiss(enabled = sel != null) { open = null }
    if (sel != null && groups.containsKey(sel)) {
        val overrides by libraryViewModel.trackOverrides.collectAsStateWithLifecycle()
        Column(Modifier.dismissTransform(dismiss)) {
            Row(Modifier.fillMaxWidth().padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                // A real button rather than a text link: the link's touch target was its 24 dp line height.
                TextButton(
                    onClick = { open = null },
                    colors = ButtonDefaults.textButtonColors(contentColor = accentTextColor()),
                ) {
                    Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = null, Modifier.size(18.dp))
                    Spacer(Modifier.width(4.dp))
                    Text(stringResource(R.string.action_back))
                }
                Text(
                    sel,
                    Modifier.padding(end = 12.dp),
                    style = MaterialTheme.typography.titleMedium,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            val queue = remember(sel, groups) { groups.getValue(sel).map(PlaybackQueue::queueTrack) }
            Row(Modifier.padding(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                CrystalButton(compact = true, onClick = { viewModel.playAll(queue) }) { Text(stringResource(R.string.library_play_all)) }
                CrystalButton(compact = true, filled = false, onClick = {
                    viewModel.playAll(queue, shuffled = true)
                }) { Text(stringResource(R.string.action_shuffle)) }
            }
            LazyColumn(Modifier.fillMaxSize()) {
                items(groups.getValue(sel), key = { it.uri }, contentType = { "track" }) { t ->
                    TrackRow(t, viewModel, libraryViewModel, overrides[t.uri], subtitleOverride = t.album, queue = queue)
                }
            }
        }
    } else {
        val names = remember(groups) { groups.keys.sorted() }
        LazyColumn(Modifier.fillMaxSize()) {
            items(names, key = { it }, contentType = { "group" }) { g ->
                Row(
                    Modifier.fillMaxWidth().clickable { open = g }.padding(horizontal = 16.dp, vertical = 10.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Column(Modifier.weight(1f)) {
                        Text(
                            g.ifEmpty { stringResource(R.string.library_group_unnamed) },
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                        Text(
                            pluralStringResource(
                                R.plurals.track_count,
                                groups.getValue(g).size,
                                groups.getValue(g).size,
                            ),
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun PlaylistsTab(viewModel: LibraryViewModel) {
    val library by viewModel.library.collectAsStateWithLifecycle()
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
            CrystalButton(compact = true, filled = false, onClick = { creating = true }) { Text(stringResource(R.string.playlist_new)) }
            CrystalButton(
                compact = true,
                filled = false,
                onClick = { importPicker.launch(arrayOf("*/*")) },
            ) { Text(stringResource(R.string.playlist_import_action)) }
        }
        // Three kinds of item share this list, so each declares its type and lazy layout reuses
        // a playlist row's slot only for another playlist row. Playlist keys carry a prefix so a
        // playlist named like the fixed items cannot collide with them.
        LazyColumn(Modifier.fillMaxSize()) {
            item(key = "smart", contentType = "smart") { SmartPlaylistsSection(viewModel) }
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
    }
    if (creating) {
        PlaylistNameDialog(
            title = stringResource(R.string.playlist_new),
            confirmLabel = stringResource(R.string.action_create),
            taken = library.playlists.map { it.name }.toSet(),
            onName = viewModel::createMusicPlaylist,
            onDismiss = { creating = false },
        )
    }
    deleting?.let { doomed ->
        AlertDialog(
            onDismissRequest = { deleting = null },
            title = { Text(stringResource(R.string.playlist_delete_title)) },
            text = { Text(stringResource(R.string.playlist_delete_body, doomed)) },
            confirmButton = {
                CrystalButton(onClick = {
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
                CrystalButton(enabled = nameOk, onClick = {
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
    playlist.trackUris.forEachIndexed { i, uri ->
        val title = byUri[uri]?.title ?: stringResource(R.string.playlist_untitled_track, i + 1)
        val dragging = i == dragFrom
        val shift by animateFloatAsState(
            if (dragFrom < 0) 0f else (playlistRowShift(i, dragFrom, dropIndex) * rowHeight).toFloat(),
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

@Composable
private fun FoldersTab(
    folders: Map<String, List<DeviceTrack>>,
    viewModel: PlayerViewModel,
) {
    val libraryViewModel: LibraryViewModel = geodeViewModel()
    val roots by libraryViewModel.mediaRoots.collectAsStateWithLifecycle()
    val scanning by libraryViewModel.libraryScanning.collectAsStateWithLifecycle()
    val folderPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
            if (uri != null) libraryViewModel.importFolder(uri)
        }
    Column {
        Text(
            stringResource(R.string.folders_library),
            Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            style = MaterialTheme.typography.titleSmall,
        )
        roots.sorted().forEach { root ->
            Row(
                Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    java.net.URLDecoder
                        .decode(root.substringAfterLast("%3A").substringAfterLast("/"), "UTF-8")
                        .ifBlank { root },
                    Modifier.weight(1f),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.bodySmall,
                )
                IconButton(onClick = { libraryViewModel.removeMediaRoot(root) }) {
                    StoneIconArt(StoneIcon.CLOSE, stringResource(R.string.folders_remove))
                }
            }
        }
        Row(
            Modifier.padding(horizontal = 16.dp, vertical = 2.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            CrystalButton(
                compact = true,
                filled = false,
                onClick = { folderPicker.launch(null) },
            ) { Text(stringResource(R.string.folders_add)) }
            CrystalButton(
                compact = true,
                filled = false,
                onClick = libraryViewModel::rescanMediaRoots,
                enabled = roots.isNotEmpty() && !scanning,
            ) {
                Text(stringResource(if (scanning) R.string.folders_scanning else R.string.folders_rescan))
            }
        }
        Text(
            stringResource(R.string.folders_device),
            Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            style = MaterialTheme.typography.bodySmall,
        )
        GroupList(remember(folders) { FolderTree.rows(folders) }, viewModel, libraryViewModel)
    }
}
