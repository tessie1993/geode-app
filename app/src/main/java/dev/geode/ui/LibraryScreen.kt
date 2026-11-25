package dev.geode.ui

import android.Manifest
import android.net.Uri
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.wrapContentWidth
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Sort
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.PrimaryScrollableTabRow
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.adaptive.currentWindowAdaptiveInfo
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.window.core.layout.WindowSizeClass
import dev.geode.R
import dev.geode.model.DeviceTrack
import dev.geode.model.LibraryStartTab
import dev.geode.model.LibraryTrack
import dev.geode.model.QueueTrack
import dev.geode.model.TrackTap
import dev.geode.playback.PlaybackQueue
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt

/** Playlists sit last and behave differently: hand-ordered, so search and sort do not apply. */
private const val PLAYLISTS_TAB = 4
private const val DUPLICATES_TAB = 5

/** The tab row's position of each tab Settings › Preferences can open Library on. */
private fun LibraryStartTab.tabIndex(): Int =
    when (this) {
        LibraryStartTab.TRACKS -> 0
        LibraryStartTab.ALBUMS -> 1
        LibraryStartTab.ARTISTS -> 2
        LibraryStartTab.FOLDERS -> 3
        LibraryStartTab.PLAYLISTS -> PLAYLISTS_TAB
    }

/**
 * Settings › Preferences › Library's tap on a track, provided once by [LibraryScreen]: the rows sit
 * four lists deep (tracks, an album or artist, search, favourites), and none of the lists between
 * has any other use for it.
 */
private val LocalTrackTap = staticCompositionLocalOf { TrackTap.PLAY_LIST }

/**
 * The pane width from which a landscape or expanded window splits the library into two columns
 * or panes. Read from the pane, not only the window: on a tablet the Player pane already takes
 * half the window, and two 200 dp columns would be worse than one.
 */
private val LIBRARY_SPLIT_WIDTH = 640.dp

/** A list column never stretches past this, so a row stays one glance wide on a tablet. */
private val LIBRARY_COLUMN_MAX = 600.dp

/**
 * Landscape or expanded width, from the window size class. Portrait stays one column at any
 * width: a portrait tablet is read top to bottom, and a second column there splits a list the
 * eye expects to run down.
 */
@Composable
private fun libraryLandscapeOrExpanded(): Boolean {
    val size = currentWindowAdaptiveInfo().windowSizeClass
    return size.isWidthAtLeastBreakpoint(WindowSizeClass.WIDTH_DP_EXPANDED_LOWER_BOUND) ||
        (
            size.isWidthAtLeastBreakpoint(WindowSizeClass.WIDTH_DP_MEDIUM_LOWER_BOUND) &&
                !size.isHeightAtLeastBreakpoint(WindowSizeClass.HEIGHT_DP_MEDIUM_LOWER_BOUND)
        )
}

/** Centres a list and caps it at [LIBRARY_COLUMN_MAX] per column, so a wide pane is not one stretched row. */
internal fun Modifier.libraryListWidth(columns: Int): Modifier =
    fillMaxSize()
        .wrapContentWidth(Alignment.CenterHorizontally)
        .widthIn(max = LIBRARY_COLUMN_MAX * columns)
        .fillMaxWidth()

/**
 * [onOpenFolders] opens Settings › Library and [onOpenVisuals] opens Visuals: the empty library's
 * ways out ([LibraryEmptyHost]).
 *
 * Search is the library's own: the header's icon turns the header into the field, and while it is
 * open the tabs step aside for one result list (artists, albums, tracks). Back, or the arrow,
 * closes it and returns to the tab that was open.
 */
@Composable
fun LibraryScreen(
    onOpenFolders: () -> Unit = {},
    onOpenVisuals: () -> Unit = {},
) {
    val settings: SettingsViewModel = geodeViewModel()
    val general by settings.general.collectAsStateWithLifecycle()
    CompositionLocalProvider(LocalTrackTap provides general.trackTap) {
        LibraryContent(onOpenFolders, onOpenVisuals, general.libraryStartTab)
    }
}

/**
 * [startTab] is where the tabs stand until one is picked; read live rather than seeded once, since
 * the prefs may finish loading after the screen first draws.
 */
@Composable
private fun LibraryContent(
    onOpenFolders: () -> Unit,
    onOpenVisuals: () -> Unit,
    startTab: LibraryStartTab,
) {
    val libraryViewModel: LibraryViewModel = geodeViewModel()
    val playerViewModel: PlayerViewModel = geodeViewModel()
    val permission = Manifest.permission.READ_MEDIA_AUDIO
    // Through rememberPermissionRequest, not a one-off check: it re-reads the grant on every
    // resume. The old check ran once per composition, so music access turned on in system
    // Settings (the only way back after "don't ask again") left the library empty until the app
    // restarted. The grant flips `granted`, and the effect below then re-queries the library.
    val access = rememberPermissionRequest(permission)
    val granted = access.granted
    var reloadKey by remember { mutableStateOf(0) }
    val state by libraryViewModel.uiState.collectAsStateWithLifecycle()
    // The field shows every keystroke; the list follows the debounced query in [state].
    val query by libraryViewModel.query.collectAsStateWithLifecycle()
    val duplicates by libraryViewModel.duplicates.collectAsStateWithLifecycle()
    val hiddenShort by libraryViewModel.hiddenShortTracks.collectAsStateWithLifecycle()
    LaunchedEffect(granted, reloadKey) { if (granted) libraryViewModel.refreshDeviceTracks() }
    var pickedTab by rememberSaveable { mutableStateOf<Int?>(null) }
    val tab = pickedTab ?: startTab.tabIndex()
    var searchOpen by rememberSaveable { mutableStateOf(false) }
    // A query kept across process death reopens the search it belongs to.
    val searching = searchOpen || query.isNotEmpty()
    val closeSearch = {
        libraryViewModel.setQuery("")
        searchOpen = false
    }
    // Declared before the lists, so an album or artist opened from the results closes first.
    rememberPredictiveDismiss(enabled = searching, onDismiss = closeSearch)
    val tabs =
        listOf(
            stringResource(R.string.library_tab_tracks),
            stringResource(R.string.library_tab_albums),
            stringResource(R.string.library_tab_artists),
            stringResource(R.string.library_tab_folders),
            stringResource(R.string.library_tab_playlists),
            stringResource(R.string.library_tab_duplicates),
        )
    // Already searched, sorted and grouped by the ViewModel, off the main thread — the screen
    // draws what it is handed.
    val shown = state.tracks
    val landscapeOrExpanded = libraryLandscapeOrExpanded()

    Column(Modifier.fillMaxSize()) {
        if (searching) {
            LibrarySearchField(
                query,
                onQueryChange = {
                    libraryViewModel.setQuery(it)
                    // Emptying the field keeps the search open to type again.
                    searchOpen = true
                },
                onClose = closeSearch,
            )
        } else {
            LibraryHeader(onSearch = { searchOpen = true })
        }
        if (!granted) {
            PermissionRationales.Shown()
            val canAskAgain = access.status != PermissionStatus.PERMANENTLY_DENIED
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
                // request() opens the system dialog, or app Settings once Android stops asking.
                Button(onClick = access::request) {
                    Text(
                        stringResource(
                            if (canAskAgain) R.string.library_permission_allow else R.string.library_permission_open_settings,
                        ),
                    )
                }
            }
            return
        }
        // The results outlive a closed search by the moment the library takes to list itself again:
        // better than the tabs flashing the last hits.
        if (searching || state.isSearching) {
            BoxWithConstraints(Modifier.fillMaxWidth().weight(1f)) {
                val columns = if (landscapeOrExpanded && maxWidth >= LIBRARY_SPLIT_WIDTH) 2 else 1
                LibrarySearchResults(state, typed = query, playerViewModel, libraryViewModel, columns)
            }
            return
        }
        if (state.showEmptyLibrary && tab != PLAYLISTS_TAB) {
            // Granted, asked, and nothing: the ways out instead of an empty list (#340). Playlists
            // stay reachable, since a playlist can hold imported files MediaStore does not list.
            LibraryEmptyHost(
                libraryViewModel = libraryViewModel,
                playerViewModel = playerViewModel,
                onRescan = {
                    libraryViewModel.rescanMediaRoots()
                    reloadKey++
                },
                onChooseFolder = onOpenFolders,
                onOpenVisuals = onOpenVisuals,
                modifier = Modifier.weight(1f),
            )
            HiddenShortTracks(hiddenShort, onOpenFolders)
            TextButton(onClick = { pickedTab = PLAYLISTS_TAB }, modifier = Modifier.padding(horizontal = 16.dp)) {
                Text(stringResource(R.string.library_tab_playlists))
            }
            return
        }
        PrimaryScrollableTabRow(selectedTabIndex = tab, containerColor = Color.Transparent, divider = {}) {
            tabs.forEachIndexed { i, t -> Tab(selected = i == tab, onClick = { pickedTab = i }, text = { Text(t) }) }
        }
        // Measured below the tabs, where the lists live: the pane's own width decides the split.
        BoxWithConstraints(Modifier.fillMaxWidth().weight(1f)) {
            val wide = landscapeOrExpanded && maxWidth >= LIBRARY_SPLIT_WIDTH
            val columns = if (wide) 2 else 1
            Column(Modifier.fillMaxSize()) {
                // Sort belongs to the track-shaped tabs. Playlists are ordered by hand, and
                // re-sorting someone's running order out from under them would be a bug, not a feature.
                if (tab != PLAYLISTS_TAB && tab != DUPLICATES_TAB) {
                    SortPicker(state.sort, libraryViewModel::setSort, Modifier.padding(horizontal = GeodeSpacing.S))
                }
                if (tab == 0) HiddenShortTracks(hiddenShort, onOpenFolders)
                // Grouping walks the whole library, so the ViewModel does it off the main thread
                // (LibraryBrowse.browse); the tabs only draw the groups.
                when (tab) {
                    0 -> TrackList(shown, playerViewModel, libraryViewModel, columns)
                    1 -> GroupList(state.browse.albums, playerViewModel, libraryViewModel, columns)
                    2 -> GroupList(state.browse.artists, playerViewModel, libraryViewModel, columns)
                    3 -> FoldersTab(state.browse.folders, playerViewModel, wide)
                    PLAYLISTS_TAB -> PlaylistsTab(libraryViewModel, playerViewModel, wide)
                    DUPLICATES_TAB -> DuplicatesTab(duplicates, playerViewModel, onDeleted = { reloadKey++ })
                }
            }
        }
    }
}

/** "N short tracks hidden · Settings": the minimum length's count, opening Settings › Library where it is set. */
@Composable
private fun HiddenShortTracks(
    count: Int,
    onOpenSettings: () -> Unit,
) {
    if (count <= 0) return
    TextButton(onClick = onOpenSettings, modifier = Modifier.padding(horizontal = GeodeSpacing.S)) {
        Text(pluralStringResource(R.plurals.library_short_hidden, count, count), style = MaterialTheme.typography.bodySmall)
    }
}

/**
 * The track order as one row with its value and ▾, opening a menu with a check on the current
 * order. It was a chip per order; a menu keeps the row one control wide however many orders
 * there are, and a long label at a large font scale wraps in the menu instead of being cut.
 */
@Composable
private fun SortPicker(
    sort: LibrarySort,
    onSort: (LibrarySort) -> Unit,
    modifier: Modifier = Modifier,
) {
    LibraryDropdownRow(
        value = stringResource(sort.labelRes),
        options = LibrarySort.entries,
        label = { stringResource(it.labelRes) },
        isSelected = { it == sort },
        onPick = onSort,
        modifier = modifier,
        leading = { Icon(Icons.AutoMirrored.Filled.Sort, contentDescription = null, Modifier.size(20.dp)) },
    )
}

/**
 * One pick from a list, Material's way: the current value and a ▾ on a 48 dp row, and an M3
 * [DropdownMenu] with a check on the chosen item (TalkBack hears it as selected). Shared by the
 * library's sort, the smart playlist editor's rule pickers and the Styles page's family pick;
 * [detail] is a quiet note at an item's end (a family's style count).
 */
@Composable
internal fun <T> LibraryDropdownRow(
    value: String,
    options: List<T>,
    label: @Composable (T) -> String,
    isSelected: (T) -> Boolean,
    onPick: (T) -> Unit,
    modifier: Modifier = Modifier,
    leading: (@Composable () -> Unit)? = null,
    detail: ((T) -> String)? = null,
) {
    var open by remember { mutableStateOf(false) }
    Box(modifier) {
        Row(
            Modifier
                .heightIn(min = 48.dp)
                .clip(MaterialTheme.shapes.small)
                .clickable(role = Role.DropdownList) { open = true }
                .padding(horizontal = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(4.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            leading?.invoke()
            Text(value, style = MaterialTheme.typography.labelLarge, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Icon(Icons.Filled.ArrowDropDown, contentDescription = null)
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            options.forEach { option ->
                val chosen = isSelected(option)
                DropdownMenuItem(
                    text = { Text(label(option)) },
                    onClick = {
                        open = false
                        onPick(option)
                    },
                    modifier = Modifier.semantics { selected = chosen },
                    trailingIcon =
                        if (chosen || detail != null) {
                            {
                                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                                    detail?.let {
                                        Text(
                                            it(option),
                                            style = MaterialTheme.typography.labelMedium,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        )
                                    }
                                    // The check's place is kept on every item, so the notes line up.
                                    if (chosen) Icon(Icons.Filled.Check, contentDescription = null) else Spacer(Modifier.size(24.dp))
                                }
                            }
                        } else {
                            null
                        },
                )
            }
        }
    }
}

@Composable
private fun TrackList(
    tracks: List<DeviceTrack>,
    viewModel: PlayerViewModel,
    libraryViewModel: LibraryViewModel,
    columns: Int = 1,
) {
    val queue = remember(tracks) { tracks.map(PlaybackQueue::queueTrack) }
    // Collected once for the list: per row it was a ViewModel lookup and a collector per item.
    val overrides by libraryViewModel.trackOverrides.collectAsStateWithLifecycle()
    val favourites by libraryViewModel.favourites.collectAsStateWithLifecycle()
    // Still a list, read row by row; a wide pane only flows it into side-by-side columns.
    LazyVerticalGrid(GridCells.Fixed(columns), Modifier.libraryListWidth(columns)) {
        items(tracks, key = { it.uri }, contentType = { "track" }) { t ->
            TrackRow(t, viewModel, libraryViewModel, overrides[t.uri], t.uri in favourites, queue = queue)
        }
        if (tracks.isEmpty()) {
            item(key = "empty", span = { GridItemSpan(maxLineSpan) }, contentType = "empty") {
                Text(stringResource(R.string.library_no_music), Modifier.padding(16.dp))
            }
        }
    }
}

/** A tap on [track] as Settings › Preferences › Library says; with nothing loaded every choice plays. */
private fun PlayerViewModel.tapTrack(
    tap: TrackTap,
    track: DeviceTrack,
    queue: List<QueueTrack>,
) {
    val loaded = uiState.value.hasMedia
    when {
        tap == TrackTap.PLAY_NEXT && loaded -> playNext(track.uri)
        tap == TrackTap.ADD_TO_QUEUE && loaded -> enqueue(track.uri)
        tap == TrackTap.PLAY_ALONE -> playFrom(listOf(PlaybackQueue.queueTrack(track)), track.uri)
        queue.isEmpty() -> playTrack(track.uri)
        else -> playFrom(queue, track.uri)
    }
}

@Composable
internal fun TrackRow(
    t: DeviceTrack,
    viewModel: PlayerViewModel,
    libraryViewModel: LibraryViewModel,
    stored: LibraryTrack?,
    favourite: Boolean,
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
    val tap = LocalTrackTap.current
    Row(
        Modifier
            .fillMaxWidth()
            .clickable { viewModel.tapTrack(tap, t, queue) }
            .padding(horizontal = 16.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        TrackArtwork(t.uri, Modifier.size(44.dp), corner = 8.dp, decodeSize = 44.dp)
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
        if (favourite) {
            StoneIconArt(
                StoneIcon.FAVORITE,
                stringResource(R.string.a11y_favourite),
                Modifier.padding(start = 8.dp).size(16.dp),
                tint = MaterialTheme.colorScheme.primary,
                selected = true,
            )
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
            DropdownMenuItem(
                text = {
                    Text(stringResource(if (favourite) R.string.action_favourite_remove else R.string.action_favourite_add))
                },
                onClick = {
                    libraryViewModel.toggleFavourite(t.uri)
                    menu = false
                },
            )
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
            onName = { name -> viewModel.createMusicPlaylist(name, listOf(uri)) },
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
internal fun GroupList(
    groups: Map<String, List<DeviceTrack>>,
    viewModel: PlayerViewModel,
    libraryViewModel: LibraryViewModel,
    columns: Int = 1,
) {
    var open by rememberSaveable { mutableStateOf<String?>(null) }
    val sel = open
    val dismiss = rememberPredictiveDismiss(enabled = sel != null) { open = null }
    if (sel != null && groups.containsKey(sel)) {
        GroupDetail(
            sel,
            groups.getValue(sel),
            viewModel,
            libraryViewModel,
            columns,
            onBack = { open = null },
            Modifier.dismissTransform(dismiss),
        )
    } else {
        val names = remember(groups) { groups.keys.sorted() }
        LazyVerticalGrid(GridCells.Fixed(columns), Modifier.libraryListWidth(columns)) {
            items(names, key = { it }, contentType = { "group" }) { g ->
                val count = groups.getValue(g).size
                GroupRow(g, pluralStringResource(R.plurals.track_count, count, count), onClick = { open = g })
            }
        }
    }
}

/** One album or artist as a row: its name over [subtitle]. */
@Composable
internal fun GroupRow(
    name: String,
    subtitle: String,
    onClick: () -> Unit,
) {
    Column(
        Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .heightIn(min = 48.dp)
            .padding(horizontal = GeodeSpacing.L, vertical = 10.dp),
        verticalArrangement = Arrangement.Center,
    ) {
        Text(name.ifEmpty { stringResource(R.string.library_group_unnamed) }, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Text(
            subtitle,
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

/** An album's or artist's tracks, opened in place: back, Play all, Shuffle, then the list. */
@Composable
internal fun GroupDetail(
    name: String,
    tracks: List<DeviceTrack>,
    viewModel: PlayerViewModel,
    libraryViewModel: LibraryViewModel,
    columns: Int,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val overrides by libraryViewModel.trackOverrides.collectAsStateWithLifecycle()
    val favourites by libraryViewModel.favourites.collectAsStateWithLifecycle()
    Column(modifier) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            // A real button rather than a text link: the link's touch target was its 24 dp line height.
            TextButton(
                onClick = onBack,
                colors = ButtonDefaults.textButtonColors(contentColor = accentTextColor()),
            ) {
                Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = null, Modifier.size(18.dp))
                Spacer(Modifier.width(4.dp))
                Text(stringResource(R.string.action_back))
            }
            Text(
                name.ifEmpty { stringResource(R.string.library_group_unnamed) },
                Modifier.padding(end = 12.dp),
                style = MaterialTheme.typography.titleMedium,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        val queue = remember(tracks) { tracks.map(PlaybackQueue::queueTrack) }
        Row(Modifier.padding(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(
                onClick = { viewModel.playAll(queue) },
                contentPadding = PaddingValues(horizontal = GeodeSpacing.M, vertical = GeodeSpacing.XS),
            ) { Text(stringResource(R.string.library_play_all)) }
            FilledTonalButton(
                onClick = { viewModel.playAll(queue, shuffled = true) },
                contentPadding = PaddingValues(horizontal = GeodeSpacing.M, vertical = GeodeSpacing.XS),
            ) { Text(stringResource(R.string.action_shuffle)) }
        }
        LazyVerticalGrid(GridCells.Fixed(columns), Modifier.libraryListWidth(columns)) {
            items(tracks, key = { it.uri }, contentType = { "track" }) { t ->
                TrackRow(
                    t,
                    viewModel,
                    libraryViewModel,
                    overrides[t.uri],
                    t.uri in favourites,
                    subtitleOverride = t.album,
                    queue = queue,
                )
            }
        }
    }
}

/** The library's title and the icon that opens its search. */
@Composable
private fun LibraryHeader(onSearch: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().padding(horizontal = GeodeSpacing.L, vertical = GeodeSpacing.S),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(
                stringResource(R.string.app_name),
                style = MaterialTheme.typography.titleSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                stringResource(R.string.nav_library),
                style = MaterialTheme.typography.titleLarge,
                modifier = Modifier.semantics { heading() },
            )
        }
        IconButton(onClick = onSearch) { StoneIconArt(StoneIcon.SEARCH, stringResource(R.string.action_search)) }
    }
}
