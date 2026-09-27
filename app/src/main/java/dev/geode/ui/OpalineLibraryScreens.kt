package dev.geode.ui

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import android.provider.Settings
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.DeleteOutline
import androidx.compose.material.icons.filled.FolderOpen
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Shuffle
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.nav.Destination
import dev.geode.nav.LibraryView
import dev.geode.nav.Navigator
import dev.geode.nav.Overlay
import dev.geode.ui.opaline.OpalineButton
import dev.geode.ui.opaline.OpalineEmptyState
import dev.geode.ui.opaline.OpalineIconButton
import dev.geode.ui.opaline.OpalinePage
import dev.geode.ui.opaline.OpalinePanel
import dev.geode.ui.opaline.OpalineRow
import dev.geode.ui.opaline.opalinePart
import kotlinx.coroutines.launch

@Composable
internal fun OpalineLibraryRoute(
    destination: Destination.Library,
    navigator: Navigator,
    player: PlayerViewModel,
) {
    val library: LibraryViewModel = geodeViewModel()
    val tracks = rememberOpalineTracks(library)
    when (destination) {
        is Destination.Library.Browse -> OpalineLibraryBrowse(destination.view, tracks, library, navigator, player)
        is Destination.Library.Album ->
            OpalineTrackGroup(
                destination.name,
                tracks.filter { it.album == destination.name },
                navigator,
                player,
            )
        is Destination.Library.Artist ->
            OpalineTrackGroup(
                destination.name,
                tracks.filter { it.artist == destination.name },
                navigator,
                player,
            )
        is Destination.Library.Folder ->
            OpalineTrackGroup(
                destination.path,
                tracks.filter { it.folder == destination.path },
                navigator,
                player,
            )
        is Destination.Library.Playlist -> OpalinePlaylistDetail(destination.id, library, navigator)
        is Destination.Library.SmartPlaylist -> OpalineSmartPlaylist(destination.id, library, navigator)
        Destination.Library.Duplicates -> OpalineDuplicates(tracks, library, navigator, player)
    }
}

@Composable
internal fun rememberOpalineTracks(library: LibraryViewModel): List<DeviceTrack> {
    val device by library.deviceTracks.collectAsStateWithLifecycle()
    val imported by library.library.collectAsStateWithLifecycle()
    val overrides by library.trackOverrides.collectAsStateWithLifecycle()
    return remember(device, imported.tracks, overrides) {
        val local = imported.tracks.map { DeviceTrack(it.uri, it.title, it.artist, it.album, it.folder, it.durationMs) }
        (device + local).distinctBy { it.uri }.map { track ->
            val edited = overrides[track.uri]
            if (edited == null) {
                track
            } else {
                track.copy(
                    title = edited.title.ifBlank { track.title },
                    artist = edited.artist.ifBlank { track.artist },
                    album = edited.album.ifBlank { track.album },
                )
            }
        }
    }
}

@Composable
internal fun OpalineImportActions() {
    val library: LibraryViewModel = geodeViewModel()
    val tracks =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { if (it.isNotEmpty()) library.importTracks(it) }
    val folder = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { if (it != null) library.importFolder(it) }
    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        OpalineButton(stringResource(R.string.opaline_add_music), { tracks.launch(arrayOf("audio/*")) }, icon = Icons.Default.Add)
        OpalineButton(stringResource(R.string.folders_add), { folder.launch(null) }, icon = Icons.Default.FolderOpen)
    }
}

@Composable
private fun OpalineLibraryPermission(library: LibraryViewModel) {
    val context = LocalContext.current
    val lifecycle = LocalLifecycleOwner.current
    val permission = if (Build.VERSION.SDK_INT >= 33) Manifest.permission.READ_MEDIA_AUDIO else Manifest.permission.READ_EXTERNAL_STORAGE

    fun granted() = ContextCompat.checkSelfPermission(context, permission) == PackageManager.PERMISSION_GRANTED
    var allowed by remember { mutableStateOf(granted()) }
    val request =
        rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) {
            allowed = it
            if (it) library.refreshDeviceTracks()
        }
    DisposableEffect(lifecycle, permission) {
        val observer =
            LifecycleEventObserver { _, event ->
                if (event == Lifecycle.Event.ON_RESUME) {
                    allowed = granted()
                    if (allowed) library.refreshDeviceTracks()
                }
            }
        lifecycle.lifecycle.addObserver(observer)
        onDispose { lifecycle.lifecycle.removeObserver(observer) }
    }
    LaunchedEffect(allowed) { if (allowed) library.refreshDeviceTracks() }
    if (!allowed) {
        OpalinePanel {
            Text(stringResource(R.string.library_permission_rationale), style = MaterialTheme.typography.bodyMedium)
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OpalineButton(stringResource(R.string.library_permission_allow), { request.launch(permission) })
                OpalineButton(stringResource(R.string.nav_settings), {
                    context.startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:${context.packageName}")))
                })
            }
        }
    }
}

@Composable
private fun OpalineLibraryBrowse(
    view: LibraryView,
    tracks: List<DeviceTrack>,
    library: LibraryViewModel,
    navigator: Navigator,
    player: PlayerViewModel,
) {
    val state by library.uiState.collectAsStateWithLifecycle()
    val favourites by player.favourites.collectAsStateWithLifecycle()
    val historyTick by player.historyTick.collectAsStateWithLifecycle()
    val history = remember(historyTick) { player.recentlyPlayed() }
    val results =
        remember(tracks, state.query, state.sort, view, favourites, history) {
            val filtered =
                when (view) {
                    LibraryView.FAVOURITES -> tracks.filter { it.uri in favourites }
                    LibraryView.RECENT -> history.mapNotNull { entry -> tracks.firstOrNull { it.uri == entry.uri } }
                    else -> tracks
                }
            val searched = LibraryBrowse.search(filtered, state.query)
            if (view == LibraryView.RECENT) searched else LibraryBrowse.sort(searched, state.sort)
        }
    OpalinePage(stringResource(R.string.nav_library), stringResource(R.string.opaline_library_subtitle, tracks.size), actions = {
        OpalineIconButton(Icons.Default.Search, stringResource(R.string.action_search), { navigator.open(Overlay.Search) })
        OpalineIconButton(Icons.Default.Refresh, stringResource(R.string.folders_rescan), library::refreshDeviceTracks)
    }) {
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            LibraryView.entries.forEach { option ->
                OpalineButton(
                    libraryViewLabel(option),
                    { navigator.go(Destination.Library.Browse(option)) },
                    selected =
                        option == view,
                )
            }
        }
        if (view == LibraryView.PLAYLISTS) {
            OpalinePlaylists(library, navigator)
        } else {
            OutlinedTextField(
                state.query,
                library::setQuery,
                singleLine = true,
                modifier =
                    Modifier.fillMaxWidth().opalinePart(
                        "A05",
                    ),
                label = {
                    Text(stringResource(R.string.library_search_hint))
                },
            )
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                LibrarySort.entries.forEach { sort ->
                    OpalineButton(
                        stringResource(sort.labelRes),
                        { library.setSort(sort) },
                        selected =
                            sort == state.sort,
                    )
                }
            }
            LazyColumn(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                item { OpalineLibraryPermission(library) }
                item { OpalineImportActions() }
                if (view == LibraryView.FOLDERS) item { OpalineFolderRoots(library) }
                if (view == LibraryView.TRACKS && results.isNotEmpty()) {
                    item {
                        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            OpalineButton(
                                stringResource(R.string.library_play_all),
                                { player.playAll(results.toQueue()) },
                                icon = Icons.Default.PlayArrow,
                            )
                            OpalineButton(
                                stringResource(R.string.action_shuffle),
                                { player.playAll(results.toQueue(), true) },
                                icon = Icons.Default.Shuffle,
                            )
                            OpalineButton(stringResource(R.string.library_tab_duplicates), { navigator.go(Destination.Library.Duplicates) })
                        }
                    }
                }
                when (view) {
                    LibraryView.ALBUMS, LibraryView.ARTISTS, LibraryView.FOLDERS -> {
                        val groups =
                            results.groupBy {
                                when (view) {
                                    LibraryView.ALBUMS -> it.album
                                    LibraryView.ARTISTS -> it.artist
                                    else -> it.folder
                                }
                            }
                        items(groups.keys.sorted(), key = { it }) { name ->
                            OpalineRow(
                                folderLabel(name, view).ifBlank {
                                    stringResource(R.string.library_group_unnamed)
                                },
                                stringResource(R.string.opaline_tracks_count, groups.getValue(name).size),
                                onClick = {
                                    navigator.go(
                                        when (view) {
                                            LibraryView.ALBUMS -> Destination.Library.Album(name)
                                            LibraryView.ARTISTS -> Destination.Library.Artist(name)
                                            else -> Destination.Library.Folder(name)
                                        },
                                    )
                                },
                                leading = { TrackArtwork(groups.getValue(name).first().uri, Modifier.size(54.dp)) },
                            )
                        }
                    }
                    else -> items(results, key = { it.uri }) { track -> OpalineTrackRow(track, results, navigator, player) }
                }
                if (results.isEmpty()) {
                    item {
                        OpalineEmptyState(
                            stringResource(if (state.query.isBlank()) R.string.library_no_music else R.string.library_no_results),
                            stringResource(R.string.opaline_import_hint),
                        )
                    }
                }
            }
        }
    }
}

private fun folderLabel(
    name: String,
    view: LibraryView,
): String =
    if (view == LibraryView.FOLDERS && name.startsWith("content://")) {
        Uri
            .parse(name)
            .lastPathSegment
            ?.let(Uri::decode)
            .orEmpty()
    } else {
        name
    }

@Composable
private fun OpalineFolderRoots(library: LibraryViewModel) {
    val roots by library.mediaRoots.collectAsStateWithLifecycle()
    val scanning by library.libraryScanning.collectAsStateWithLifecycle()
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        roots.forEach { root ->
            OpalineRow(Uri.decode(root.substringAfterLast('/')), onClick = {}, trailing = {
                OpalineIconButton(Icons.Default.DeleteOutline, stringResource(R.string.folders_remove), { library.removeMediaRoot(root) })
            })
        }
        OpalineButton(
            stringResource(if (scanning) R.string.folders_scanning else R.string.folders_rescan),
            library::rescanMediaRoots,
            enabled =
                roots.isNotEmpty() && !scanning,
        )
    }
}

@Composable
private fun libraryViewLabel(view: LibraryView): String =
    stringResource(
        when (view) {
            LibraryView.TRACKS -> R.string.library_tab_tracks
            LibraryView.ALBUMS -> R.string.library_tab_albums
            LibraryView.ARTISTS -> R.string.library_tab_artists
            LibraryView.FOLDERS -> R.string.library_tab_folders
            LibraryView.PLAYLISTS -> R.string.library_tab_playlists
            LibraryView.FAVOURITES -> R.string.opaline_favourites
            LibraryView.RECENT -> R.string.opaline_recent
        },
    )

@Composable
internal fun OpalineTrackRow(
    track: DeviceTrack,
    tracks: List<DeviceTrack>,
    navigator: Navigator,
    player: PlayerViewModel,
) {
    var menu by remember { mutableStateOf(false) }
    val favourites by player.favourites.collectAsStateWithLifecycle()
    val queue by player.queue.collectAsStateWithLifecycle()
    OpalineRow(
        track.title.ifBlank {
            stringResource(R.string.title_untitled)
        },
        listOf(track.artist, formatClock(track.durationMs)).filter { it.isNotBlank() }.joinToString(" · "),
        onClick = { player.playFrom(tracks.toQueue(), track.uri) },
        modifier = Modifier.opalinePart("C01", selected = queue.tracks.getOrNull(queue.index)?.uri == track.uri),
        leading = { TrackArtwork(track.uri, Modifier.size(44.dp)) },
        trailing = {
            Column {
                OpalineIconButton(Icons.Default.MoreVert, stringResource(R.string.opaline_track_actions), { menu = true })
                DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                    DropdownMenuItem(text = { Text(stringResource(R.string.opaline_play_next)) }, onClick = {
                        player.playNext(track.uri)
                        menu =
                            false
                    })
                    DropdownMenuItem(text = { Text(stringResource(R.string.opaline_add_queue)) }, onClick = {
                        player.enqueue(track.uri)
                        menu =
                            false
                    })
                    DropdownMenuItem(text = {
                        Text(
                            stringResource(
                                if (track.uri in
                                    favourites
                                ) {
                                    R.string.action_favourite_remove
                                } else {
                                    R.string.action_favourite_add
                                },
                            ),
                        )
                    }, onClick = {
                        player.toggleFavourite(track.uri)
                        menu =
                            false
                    })
                    DropdownMenuItem(text = { Text(stringResource(R.string.opaline_add_playlist)) }, onClick = {
                        navigator.go(Destination.Shared.AddToPlaylist(track.uri))
                        menu =
                            false
                    })
                    DropdownMenuItem(text = { Text(stringResource(R.string.opaline_track_info)) }, onClick = {
                        navigator.go(Destination.Shared.TrackInfo(track.uri))
                        menu =
                            false
                    })
                }
            }
        },
    )
}

@Composable
private fun OpalineTrackGroup(
    title: String,
    tracks: List<DeviceTrack>,
    navigator: Navigator,
    player: PlayerViewModel,
) {
    OpalinePage(
        title.ifBlank {
            stringResource(R.string.library_group_unnamed)
        },
        stringResource(R.string.opaline_tracks_count, tracks.size),
        onBack = { navigator.back() },
    ) {
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OpalineButton(stringResource(R.string.library_play_all), {
                player.playAll(tracks.toQueue())
            }, enabled = tracks.isNotEmpty(), icon = Icons.Default.PlayArrow)
            OpalineButton(stringResource(R.string.action_shuffle), {
                player.playAll(tracks.toQueue(), true)
            }, enabled = tracks.isNotEmpty(), icon = Icons.Default.Shuffle)
        }
        LazyColumn(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            items(tracks, key = { it.uri }) { OpalineTrackRow(it, tracks, navigator, player) }
        }
    }
}

@Composable
private fun OpalinePlaylists(
    library: LibraryViewModel,
    navigator: Navigator,
) {
    val state by library.library.collectAsStateWithLifecycle()
    val scope = rememberCoroutineScope()
    val resources = LocalResources.current
    val context = LocalContext.current
    var creating by rememberSaveable { mutableStateOf(false) }
    var name by rememberSaveable { mutableStateOf("") }
    val import =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) {
                scope.launch {
                    val result = library.importPlaylistFile(uri)
                    Toast
                        .makeText(
                            context,
                            resources.getString(
                                if (result is PlaylistImportResult.Imported) {
                                    R.string.playlist_import_done_title
                                } else {
                                    R.string.playlist_import_failed_title
                                },
                            ),
                            Toast.LENGTH_LONG,
                        ).show()
                }
            }
        }
    LazyColumn(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        item {
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OpalineButton(stringResource(R.string.playlist_new), { creating = true }, icon = Icons.Default.Add)
                OpalineButton(stringResource(R.string.playlist_import_action), { import.launch(arrayOf("*/*")) })
                OpalineButton(stringResource(R.string.smart_new), { navigator.go(Destination.Library.SmartPlaylist()) })
            }
        }
        items(state.playlists, key = { it.name }) { playlist ->
            OpalineRow(playlist.name, stringResource(R.string.opaline_tracks_count, playlist.trackUris.size), onClick = {
                navigator.go(Destination.Library.Playlist(playlist.name))
            }, trailing = {
                OpalineIconButton(Icons.Default.PlayArrow, stringResource(R.string.action_play), { library.playPlaylist(playlist.name) })
            })
        }
        items(state.smartPlaylists, key = { "smart:${it.name}" }) { playlist ->
            OpalineRow(playlist.name, stringResource(R.string.smart_playlists), onClick = {
                navigator.go(Destination.Library.SmartPlaylist(playlist.name))
            }, trailing = {
                OpalineIconButton(Icons.Default.PlayArrow, stringResource(R.string.action_play), { library.playSmartPlaylist(playlist) })
            })
        }
        if (state.playlists.isEmpty() &&
            state.smartPlaylists.isEmpty()
        ) {
            item { OpalineEmptyState(stringResource(R.string.playlist_none_yet), stringResource(R.string.opaline_playlist_hint)) }
        }
    }
    if (creating) {
        AlertDialog(onDismissRequest = { creating = false }, title = { Text(stringResource(R.string.playlist_new)) }, text = {
            OutlinedTextField(name, { name = it }, singleLine = true, label = { Text(stringResource(R.string.opaline_playlist_name)) })
        }, confirmButton = {
            OpalineButton(stringResource(R.string.action_create), {
                library.createMusicPlaylist(name.trim())
                name = ""
                creating =
                    false
            }, enabled = name.isNotBlank() && state.playlists.none { it.name.equals(name.trim(), true) })
        }, dismissButton = {
            OpalineButton(stringResource(R.string.action_cancel), {
                creating =
                    false
            })
        })
    }
}

@Composable
private fun OpalineDuplicates(
    tracks: List<DeviceTrack>,
    library: LibraryViewModel,
    navigator: Navigator,
    player: PlayerViewModel,
) {
    val context = LocalContext.current
    val groups = remember(tracks) { LibraryDuplicates.find(tracks) }
    val delete = rememberLauncherForActivityResult(ActivityResultContracts.StartIntentSenderForResult()) { library.refreshDeviceTracks() }
    OpalinePage(stringResource(R.string.library_tab_duplicates), onBack = { navigator.back() }) {
        LazyColumn(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            if (groups.isEmpty()) {
                item {
                    OpalineEmptyState(stringResource(R.string.opaline_duplicates_none), stringResource(R.string.opaline_duplicates_body))
                }
            }
            groups.forEach { group ->
                item { Text(group.title, style = MaterialTheme.typography.titleMedium) }
                items(group.tracks, key = { it.uri }) { track ->
                    OpalineRow(track.title, track.folder, onClick = { player.playTrack(track.uri) }, trailing = {
                        if (Build.VERSION.SDK_INT >= 30 &&
                            track.uri.startsWith("content://media/")
                        ) {
                            OpalineIconButton(Icons.Default.DeleteOutline, stringResource(R.string.action_delete), {
                                val request = MediaStore.createDeleteRequest(context.contentResolver, listOf(Uri.parse(track.uri)))
                                delete.launch(IntentSenderRequest.Builder(request.intentSender).build())
                            })
                        }
                    })
                }
            }
        }
    }
}

internal fun List<DeviceTrack>.toQueue(): List<QueueTrack> = map { QueueTrack(it.uri, it.title, it.artist) }
