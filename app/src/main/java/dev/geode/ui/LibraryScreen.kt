package dev.geode.ui

import android.Manifest
import android.net.Uri
import android.os.Build
import androidx.activity.compose.LocalActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGesturesAfterLongPress
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.DragHandle
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.outlined.Edit
import androidx.compose.material.icons.outlined.KeyboardArrowRight
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
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
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.MusicPlaylist
import dev.geode.ui.glass.GlassBubbleButton
import dev.geode.ui.glass.GlassButton
import dev.geode.ui.glass.GlassDialog
import dev.geode.ui.glass.GlassHorizontalTabs
import dev.geode.ui.glass.GlassIcons
import dev.geode.ui.glass.GlassListRow
import dev.geode.ui.glass.GlassPalette
import dev.geode.ui.glass.GlassSegmented
import dev.geode.ui.glass.GlassSheet
import dev.geode.ui.glass.GlassShapes
import dev.geode.ui.glass.GlassTextField
import dev.geode.ui.glass.floatOnWater
import dev.geode.ui.glass.glassSurface
import dev.geode.ui.glass.waterScroll
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
        LibraryHeader(onOpenSearch)
        if (!granted) {
            LibraryPermissionGate(
                permission = permission,
                onGranted = permLauncher::launch,
            )
            return
        }
        GlassHorizontalTabs(
            titles = tabs,
            selected = tab,
            onSelect = { tab = it },
            modifier = Modifier.horizontalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 4.dp),
        )
        // Search and sort belong to the track-shaped tabs. Playlists are ordered by hand, and
        // re-sorting someone's running order out from under them would be a bug, not a feature.
        if (tab != PLAYLISTS_TAB && tab != DUPLICATES_TAB) {
            GlassTextField(
                value = state.query,
                onValueChange = libraryViewModel::setQuery,
                placeholder = stringResource(R.string.library_search_hint),
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp),
            )
            GlassSegmented(
                options = LibrarySort.entries.map { stringResource(it.labelRes) },
                selected = LibrarySort.entries.indexOf(state.sort),
                onSelect = { libraryViewModel.setSort(LibrarySort.entries[it]) },
                modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp),
            )
        }
        when (tab) {
            0 -> TrackList(shown, playerViewModel, state.isSearching)
            1 -> GroupList(shown.groupBy { it.album }, playerViewModel)
            2 -> GroupList(shown.groupBy { it.artist }, playerViewModel)
            3 -> FoldersTab(shown.groupBy { it.folder }, playerViewModel)
            PLAYLISTS_TAB -> PlaylistsTab(libraryViewModel)
            DUPLICATES_TAB -> DuplicatesTab(state.tracks, playerViewModel, onDeleted = { reloadKey++ })
        }
    }
}

@Composable
private fun LibraryHeader(onOpenSearch: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(
                stringResource(R.string.app_name),
                style = MaterialTheme.typography.labelMedium,
                color = GlassPalette.textSecondary,
            )
            Text(
                stringResource(R.string.nav_library),
                style = MaterialTheme.typography.headlineLarge,
                color = GlassPalette.textPrimary,
            )
        }
        GlassBubbleButton(
            icon = GlassIcons.Search,
            contentDescription = stringResource(R.string.action_search),
            onClick = onOpenSearch,
            size = 44.dp,
        )
    }
}

@Composable
private fun LibraryPermissionGate(
    permission: String,
    onGranted: (String) -> Unit,
) {
    val activity = LocalActivity.current
    var asked by rememberSaveable { mutableStateOf(false) }
    val canAskAgain =
        activity == null ||
            !asked ||
            ActivityCompat.shouldShowRequestPermissionRationale(activity, permission)
    val context = LocalContext.current
    Box(
        Modifier
            .fillMaxWidth()
            .padding(16.dp)
            .glassSurface(shape = GlassShapes.tile)
            .floatOnWater()
            .padding(20.dp),
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
            Text(
                stringResource(
                    if (canAskAgain) R.string.library_permission_rationale else R.string.library_permission_denied_forever,
                ),
                color = GlassPalette.textPrimary,
                style = MaterialTheme.typography.bodyLarge,
            )
            if (canAskAgain) {
                GlassButton(
                    text = stringResource(R.string.library_permission_allow),
                    onClick = {
                        asked = true
                        onGranted(permission)
                    },
                )
            } else {
                GlassButton(
                    text = stringResource(R.string.library_permission_open_settings),
                    onClick = { context.openAppSettings() },
                )
            }
        }
    }
}

@Composable
private fun TrackList(
    tracks: List<DeviceTrack>,
    viewModel: PlayerViewModel,
    searching: Boolean = false,
) {
    val queue = remember(tracks) { tracks.map(PlaybackQueue::queueTrack) }
    // Collected once per list rather than per row, so a queue change recomposes this list one
    // time instead of every currently visible row independently subscribing to the same flow.
    val queueState by viewModel.queue.collectAsStateWithLifecycle()
    val currentUri = queueState.tracks.getOrNull(queueState.index)?.uri
    LazyColumn(
        Modifier.fillMaxSize().waterScroll(),
        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        items(tracks, key = { it.uri }) { t -> TrackRow(t, viewModel, queue = queue, currentUri = currentUri) }
        if (tracks.isEmpty()) {
            // An empty library and an empty result set are different problems, and telling
            // someone "no music found" mid-search would send them looking for the wrong fix.
            val empty = if (searching) R.string.library_no_results else R.string.library_no_music
            item { Text(stringResource(empty), color = GlassPalette.textSecondary) }
        }
    }
}

@Composable
private fun TrackRow(
    t: DeviceTrack,
    viewModel: PlayerViewModel,
    subtitleOverride: String? = null,
    queue: List<QueueTrack> = emptyList(),
    currentUri: String? = null,
) {
    val libraryViewModel: LibraryViewModel = geodeViewModel()
    val overrides by libraryViewModel.trackOverrides.collectAsStateWithLifecycle()
    val stored = overrides[t.uri]
    val title = stored?.title?.ifBlank { null } ?: t.title
    // Title, artist, album, duration and artwork — and nothing else. No tempo, no key, no
    // analysed badge: the library never waits on analysis, so it has nothing to report about it.
    val subtitle =
        subtitleOverride
            ?: listOf(
                stored?.artist?.ifBlank { null } ?: t.artist,
                stored?.album?.ifBlank { null } ?: t.album,
            ).filter { it.isNotBlank() }.joinToString(" · ")
    val duration = LibraryBrowse.formatDuration(t.durationMs)
    var menu by remember { mutableStateOf(false) }
    var editing by remember { mutableStateOf(false) }
    var addingToPlaylist by remember { mutableStateOf(false) }
    val current = currentUri == t.uri
    val scale by animateFloatAsState(if (current) 1.03f else 1f, label = "libraryRowScale")
    Box(Modifier.graphicsLayer { scaleX = scale; scaleY = scale }) {
        GlassListRow(
            title = title,
            subtitle = subtitle.ifBlank { null },
            selected = current,
            leading = { TrackArtwork(t.uri, Modifier.size(40.dp)) },
            trailing = {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    if (duration.isNotBlank()) {
                        Text(
                            duration,
                            style = MaterialTheme.typography.labelSmall,
                            color = GlassPalette.textSecondary,
                            modifier = Modifier.padding(end = 4.dp),
                        )
                    }
                    Box {
                        IconButton(onClick = { menu = true }) {
                            Icon(Icons.Filled.MoreVert, stringResource(R.string.action_more), tint = GlassPalette.textSecondary)
                        }
                        TrackRowMenu(
                            expanded = menu,
                            onDismiss = { menu = false },
                            viewModel = viewModel,
                            libraryViewModel = libraryViewModel,
                            uri = t.uri,
                            onEdit = { editing = true },
                            onAddToPlaylist = { addingToPlaylist = true },
                        )
                    }
                }
            },
            onClick = { if (queue.isEmpty()) viewModel.playTrack(t.uri) else viewModel.playFrom(queue, t.uri) },
        )
    }
    if (editing) {
        TrackInfoEditor(uri = t.uri, viewModel = libraryViewModel, onDismiss = { editing = false })
    }
    if (addingToPlaylist) {
        AddToPlaylistDialog(uri = t.uri, viewModel = libraryViewModel, onDismiss = { addingToPlaylist = false })
    }
}

@Composable
private fun TrackRowMenu(
    expanded: Boolean,
    onDismiss: () -> Unit,
    viewModel: PlayerViewModel,
    libraryViewModel: LibraryViewModel,
    uri: String,
    onEdit: () -> Unit,
    onAddToPlaylist: () -> Unit,
) {
    DropdownMenu(expanded = expanded, onDismissRequest = onDismiss) {
        DropdownMenuItem(text = { Text(stringResource(R.string.action_play_next)) }, onClick = {
            viewModel.playNext(uri)
            onDismiss()
        })
        DropdownMenuItem(text = { Text(stringResource(R.string.action_add_to_queue)) }, onClick = {
            viewModel.enqueue(uri)
            onDismiss()
        })
        DropdownMenuItem(text = { Text(stringResource(R.string.action_add_to_playlist)) }, onClick = {
            onAddToPlaylist()
            onDismiss()
        })
        DropdownMenuItem(text = { Text(stringResource(R.string.action_add_to_library_list)) }, onClick = {
            libraryViewModel.importTracks(listOf(Uri.parse(uri)))
            onDismiss()
        })
        DropdownMenuItem(text = { Text(stringResource(R.string.action_edit_track_info)) }, onClick = {
            onEdit()
            onDismiss()
        })
    }
}

@OptIn(ExperimentalMaterial3Api::class)
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
    GlassSheet(onDismissRequest = onDismiss) {
        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(
                stringResource(R.string.action_add_to_playlist),
                style = MaterialTheme.typography.titleLarge,
                color = GlassPalette.textPrimary,
            )
            Column(Modifier.verticalScroll(rememberScrollState())) {
                library.playlists.forEach { pl ->
                    GlassListRow(
                        title = pl.name,
                        modifier = Modifier.padding(vertical = 4.dp),
                        onClick = {
                            viewModel.addTrackToPlaylist(pl.name, uri)
                            onDismiss()
                        },
                    )
                }
                GlassButton(
                    text = stringResource(R.string.playlist_new_branch),
                    onClick = { naming = true },
                    modifier = Modifier.padding(top = 8.dp),
                )
            }
        }
    }
}

@Composable
private fun GroupList(
    groups: Map<String, List<DeviceTrack>>,
    viewModel: PlayerViewModel,
) {
    var open by rememberSaveable { mutableStateOf<String?>(null) }
    val sel = open
    val dismiss = rememberPredictiveDismiss(enabled = sel != null) { open = null }
    if (sel != null && groups.containsKey(sel)) {
        GroupDetail(sel, groups.getValue(sel), viewModel, onBack = { open = null }, dismissModifier = Modifier.dismissTransform(dismiss))
    } else {
        LazyColumn(
            Modifier.fillMaxSize().waterScroll(),
            contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            items(groups.keys.sorted()) { g ->
                GlassListRow(
                    title = g.ifEmpty { stringResource(R.string.library_group_unnamed) },
                    subtitle =
                        pluralStringResource(R.plurals.track_count, groups.getValue(g).size, groups.getValue(g).size),
                    leading = { Icon(GlassIcons.MusicNote, null, tint = GlassPalette.textSecondary) },
                    trailing = { Icon(Icons.Outlined.KeyboardArrowRight, null, tint = GlassPalette.textSecondary) },
                    onClick = { open = g },
                )
            }
        }
    }
}

@Composable
private fun GroupDetail(
    name: String,
    tracks: List<DeviceTrack>,
    viewModel: PlayerViewModel,
    onBack: () -> Unit,
    dismissModifier: Modifier,
) {
    Column(dismissModifier) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            GlassBubbleButton(
                icon = GlassIcons.Previous,
                contentDescription = stringResource(R.string.library_back),
                onClick = onBack,
                size = 32.dp,
                modifier = Modifier.padding(end = 12.dp),
            )
            Text(
                name,
                style = MaterialTheme.typography.titleMedium,
                color = GlassPalette.textPrimary,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        val queue = remember(name, tracks) { tracks.map(PlaybackQueue::queueTrack) }
        val queueState by viewModel.queue.collectAsStateWithLifecycle()
        val currentUri = queueState.tracks.getOrNull(queueState.index)?.uri
        Row(Modifier.padding(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            GlassButton(text = stringResource(R.string.library_play_all), onClick = { viewModel.playAll(queue) })
            GlassButton(text = stringResource(R.string.action_shuffle), onClick = { viewModel.playAll(queue, shuffled = true) })
        }
        LazyColumn(
            Modifier.fillMaxSize().waterScroll(),
            contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            items(tracks, key = { it.uri }) { t ->
                TrackRow(t, viewModel, subtitleOverride = t.album, queue = queue, currentUri = currentUri)
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
            GlassButton(text = stringResource(R.string.playlist_new), onClick = { creating = true })
            GlassButton(
                text = stringResource(R.string.playlist_import_action),
                onClick = { importPicker.launch(arrayOf("*/*")) },
            )
        }
        LazyColumn(
            Modifier.fillMaxSize().waterScroll(),
            contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            item { SmartPlaylistsSection(viewModel) }
            items(library.playlists, key = { it.name }) { pl ->
                PlaylistRow(
                    pl = pl,
                    expanded = expanded == pl.name,
                    onToggle = { expanded = if (expanded == pl.name) null else pl.name },
                    onRename = {
                        renaming = pl.name
                        renameText = pl.name
                    },
                    onPlay = { viewModel.playPlaylist(pl.name) },
                    onDelete = { deleting = pl.name },
                    tracks = library.tracks,
                    viewModel = viewModel,
                )
            }
            if (library.playlists.isEmpty()) {
                item { Text(stringResource(R.string.playlist_none_yet), color = GlassPalette.textSecondary) }
            }
        }
    }
    PlaylistsTabDialogs(
        viewModel = viewModel,
        library = library,
        creating = creating,
        onCreatingDone = { creating = false },
        deleting = deleting,
        onDeletingDone = { deleting = null },
        renaming = renaming,
        renameText = renameText,
        onRenameTextChange = { renameText = it },
        onRenamingDone = { renaming = null },
        importResult = importResult,
        onImportResultDone = { importResult = null },
    )
}

@Composable
private fun PlaylistRow(
    pl: MusicPlaylist,
    expanded: Boolean,
    onToggle: () -> Unit,
    onRename: () -> Unit,
    onPlay: () -> Unit,
    onDelete: () -> Unit,
    tracks: List<LibraryTrack>,
    viewModel: LibraryViewModel,
) {
    Column(Modifier.fillMaxWidth()) {
        GlassListRow(
            title = pl.name,
            subtitle = pluralStringResource(R.plurals.track_count, pl.trackUris.size, pl.trackUris.size),
            leading = { Icon(GlassIcons.ListIcon, null, tint = GlassPalette.textSecondary) },
            selected = expanded,
            trailing = {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    GlassBubbleButton(Icons.Outlined.Edit, stringResource(R.string.action_rename), onRename, size = 32.dp)
                    GlassBubbleButton(GlassIcons.Play, stringResource(R.string.action_play), onPlay, size = 32.dp)
                    GlassBubbleButton(GlassIcons.Close, stringResource(R.string.playlist_delete_title), onDelete, size = 32.dp)
                }
            },
            onClick = onToggle,
        )
        if (expanded) {
            PlaylistTracks(pl, tracks, viewModel)
        }
    }
}

@Composable
private fun PlaylistsTabDialogs(
    viewModel: LibraryViewModel,
    library: LibraryState,
    creating: Boolean,
    onCreatingDone: () -> Unit,
    deleting: String?,
    onDeletingDone: () -> Unit,
    renaming: String?,
    renameText: String,
    onRenameTextChange: (String) -> Unit,
    onRenamingDone: () -> Unit,
    importResult: PlaylistImportResult?,
    onImportResultDone: () -> Unit,
) {
    if (creating) {
        PlaylistNameDialog(
            title = stringResource(R.string.playlist_new),
            confirmLabel = stringResource(R.string.action_create),
            taken = library.playlists.map { it.name }.toSet(),
            onName = viewModel::createMusicPlaylist,
            onDismiss = onCreatingDone,
        )
    }
    deleting?.let { doomed ->
        GlassDialog(
            onDismissRequest = onDeletingDone,
            title = stringResource(R.string.playlist_delete_title),
            text = stringResource(R.string.playlist_delete_body, doomed),
            actions = {
                GlassButton(text = stringResource(R.string.action_cancel), onClick = onDeletingDone)
                GlassButton(
                    text = stringResource(R.string.action_delete),
                    tint = GlassPalette.pink,
                    onClick = {
                        viewModel.deleteMusicPlaylist(doomed)
                        onDeletingDone()
                    },
                )
            },
        )
    }
    renaming?.let { old -> RenamePlaylistSheet(viewModel, library, old, renameText, onRenameTextChange, onRenamingDone) }
    importResult?.let { result -> ImportResultDialog(result, onImportResultDone) }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun RenamePlaylistSheet(
    viewModel: LibraryViewModel,
    library: LibraryState,
    old: String,
    renameText: String,
    onRenameTextChange: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    val proposed = renameText.trim()
    val otherNames = library.playlists.map { it.name }.filterNot { it == old }.toSet()
    val nameOk = playlistNameAccepted(proposed, otherNames)
    GlassSheet(onDismissRequest = onDismiss) {
        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(stringResource(R.string.playlist_rename_title), style = MaterialTheme.typography.titleLarge, color = GlassPalette.textPrimary)
            GlassTextField(value = renameText, onValueChange = onRenameTextChange, modifier = Modifier.fillMaxWidth())
            if (!nameOk) {
                Text(
                    if (proposed.isEmpty()) {
                        stringResource(R.string.playlist_name_required)
                    } else {
                        stringResource(R.string.playlist_name_taken, proposed)
                    },
                    style = MaterialTheme.typography.labelSmall,
                    color = GlassPalette.pink,
                )
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                GlassButton(text = stringResource(R.string.action_cancel), onClick = onDismiss)
                GlassButton(
                    text = stringResource(R.string.action_rename),
                    onClick = {
                        viewModel.renameMusicPlaylist(old, proposed)
                        onDismiss()
                    },
                )
            }
        }
    }
}

@Composable
private fun ImportResultDialog(
    result: PlaylistImportResult,
    onDismiss: () -> Unit,
) {
    GlassDialog(
        onDismissRequest = onDismiss,
        title =
            stringResource(
                if (result is PlaylistImportResult.Imported) R.string.playlist_import_done_title else R.string.playlist_import_failed_title,
            ),
        text =
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
        actions = { GlassButton(text = stringResource(R.string.action_ok), onClick = onDismiss) },
    )
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
    val dropIndex = playlistDropIndex(dragFrom, dragOffset, rowHeight, count)
    val liftTint = GlassPalette.mint.copy(alpha = 0.14f)
    playlist.trackUris.forEachIndexed { i, uri ->
        val t = tracks.firstOrNull { it.uri == uri }
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
                tint = GlassPalette.textSecondary,
            )
            Text(
                t?.title ?: stringResource(R.string.playlist_untitled_track, i + 1),
                Modifier.weight(1f).padding(start = 6.dp),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = MaterialTheme.typography.bodySmall,
                color = GlassPalette.textPrimary,
            )
            IconButton(
                onClick = { viewModel.moveMusicPlaylistTrack(playlist.name, i, i - 1) },
                enabled = i > 0,
            ) { Icon(Icons.Filled.KeyboardArrowUp, stringResource(R.string.action_up), tint = GlassPalette.textSecondary) }
            IconButton(
                onClick = { viewModel.moveMusicPlaylistTrack(playlist.name, i, i + 1) },
                enabled = i < count - 1,
            ) { Icon(Icons.Filled.KeyboardArrowDown, stringResource(R.string.action_down), tint = GlassPalette.textSecondary) }
            IconButton(onClick = { viewModel.removeTrackFromPlaylist(playlist.name, uri) }) {
                Icon(
                    GlassIcons.Close,
                    stringResource(R.string.action_remove_from_playlist),
                    Modifier.size(18.dp),
                    tint = GlassPalette.textSecondary,
                )
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
            color = GlassPalette.textPrimary,
        )
        roots.sorted().forEach { root ->
            GlassListRow(
                title =
                    java.net.URLDecoder
                        .decode(root.substringAfterLast("%3A").substringAfterLast("/"), "UTF-8")
                        .ifBlank { root },
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
                trailing = {
                    GlassBubbleButton(
                        icon = GlassIcons.Close,
                        contentDescription = stringResource(R.string.folders_remove),
                        onClick = { libraryViewModel.removeMediaRoot(root) },
                        size = 32.dp,
                    )
                },
            )
        }
        Row(
            Modifier.padding(horizontal = 16.dp, vertical = 2.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            GlassButton(text = stringResource(R.string.folders_add), onClick = { folderPicker.launch(null) })
            GlassButton(
                text = stringResource(if (scanning) R.string.folders_scanning else R.string.folders_rescan),
                onClick = libraryViewModel::rescanMediaRoots,
                enabled = roots.isNotEmpty() && !scanning,
            )
        }
        Text(
            stringResource(R.string.folders_device),
            Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            style = MaterialTheme.typography.bodySmall,
            color = GlassPalette.textSecondary,
        )
        GroupList(FolderTree.rows(folders), viewModel)
    }
}
