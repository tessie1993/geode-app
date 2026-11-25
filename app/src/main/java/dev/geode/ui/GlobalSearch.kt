package dev.geode.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.engine.bridge.TagsNative
import dev.geode.model.QueueTrack
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import kotlinx.coroutines.delay

/** A track the search sheet offers; [fields] are title, artist, album and one more, the order native searches them in. */
private data class SearchTrackRow(
    val uri: String,
    val title: String,
    val subtitle: String,
    val fields: List<String>,
    val fromDevice: Boolean,
)

/**
 * What the search sheet searches, as UTF-8 for native (`core/library/Search`, `searchSheet`): encoded once per
 * change of the lists, so each query is one native call. Matching, ranking, one hit per song and the cap are native.
 */
private class SearchSheetRows(
    tracks: List<SearchTrackRow>,
    playlists: List<String>,
    presets: List<String>,
) {
    private val cells: Array<ByteArray?> = utf8(tracks.flatMap { it.fields + it.uri })
    private val preferred = BooleanArray(tracks.size) { tracks[it].fromDevice }
    private val playlistNames = utf8(playlists)
    private val presetNames = utf8(presets)

    /** Three counts (tracks, playlists, presets), then those rows, best first. */
    fun search(query: String): IntArray = TagsNative.searchSheet(cells, preferred, playlistNames, presetNames, query.encodeToByteArray())

    private fun utf8(texts: List<String>): Array<ByteArray?> = Array(texts.size) { texts[it].encodeToByteArray() }
}

/** The rows of list [list] (0 tracks, 1 playlists, 2 presets) in a [SearchSheetRows.search] answer. */
private fun IntArray.sheetRows(list: Int): List<Int> {
    val start = SHEET_LISTS + (0 until list).sumOf { this[it] }
    return (start until start + this[list]).map { this[it] }
}

private const val SHEET_LISTS = 3

@Composable
fun SearchScreen(
    viewModel: PlayerViewModel,
    onClose: () -> Unit,
) {
    val libraryViewModel: LibraryViewModel = geodeViewModel()
    var query by rememberSaveable { mutableStateOf("") }
    var debounced by rememberSaveable { mutableStateOf("") }
    val library by libraryViewModel.library.collectAsStateWithLifecycle()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val deviceTracks by libraryViewModel.deviceTracks.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { libraryViewModel.refreshDeviceTracks() }
    LaunchedEffect(query) {
        if (query.isNotBlank()) delay(250)
        debounced = query
    }
    val dismiss = rememberPredictiveDismiss(onDismiss = onClose)
    // Search opens ready to type: the field takes focus, which raises the keyboard.
    val searchFocus = remember { FocusRequester() }
    LaunchedEffect(Unit) { searchFocus.requestFocus() }

    val candidates =
        remember(deviceTracks, library.tracks) {
            deviceTracks.map { t ->
                SearchTrackRow(
                    uri = t.uri,
                    title = t.title,
                    subtitle = listOf(t.artist, t.album).filter { it.isNotBlank() }.joinToString(" · "),
                    fields = listOf(t.title, t.artist, t.album, t.folder),
                    fromDevice = true,
                )
            } +
                library.tracks.map { t ->
                    SearchTrackRow(
                        uri = t.uri,
                        title = t.title,
                        subtitle = listOf(t.artist, t.album).filter { it.isNotBlank() }.joinToString(" · "),
                        fields = listOf(t.title, t.artist, t.album, t.genre),
                        fromDevice = false,
                    )
                }
        }
    val playlists = library.playlists
    val presets = viz.presets
    val sheet =
        remember(candidates, playlists, presets) {
            SearchSheetRows(candidates, playlists.map { it.name }, presets.map { it.name })
        }
    val hits = remember(sheet, debounced) { sheet.search(debounced) }
    val trackResults = remember(hits) { hits.sheetRows(0).map { candidates[it] } }
    val playlistResults = remember(hits) { hits.sheetRows(1).map { playlists[it] } }
    val presetResults = remember(hits) { hits.sheetRows(2).map { presets[it] } }

    Box(
        Modifier
            .fillMaxSize()
            .dismissTransform(dismiss)
            .background(MaterialTheme.colorScheme.background)
            // The overlay owns the window: a tap in its margins must not reach the screen or the
            // tab bar under it.
            .pointerInput(Unit) { detectTapGestures { } },
    ) {
        Column(
            Modifier
                .fillMaxSize()
                // The results list runs to the bottom, under the keyboard that typing opens.
                .safeDrawingPadding()
                .padding(16.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(
                    value = query,
                    onValueChange = { query = it },
                    modifier =
                        Modifier
                            .weight(1f)
                            .focusRequester(searchFocus),
                    placeholder = { Text(stringResource(R.string.search_placeholder)) },
                    singleLine = true,
                )
                IconButton(onClick = onClose) { StoneIconArt(StoneIcon.CLOSE, stringResource(R.string.search_close)) }
            }
            LazyColumn(Modifier.fillMaxSize(), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                if (debounced.isBlank()) {
                    item {
                        Text(
                            stringResource(R.string.search_hint),
                            Modifier.padding(vertical = 16.dp),
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                } else {
                    if (trackResults.isNotEmpty()) {
                        item {
                            SearchHeading(
                                stringResource(R.string.search_heading_tracks, trackResults.size),
                                Modifier.padding(top = 8.dp),
                            )
                        }
                        items(trackResults, key = { "t:${it.uri}" }) { t ->
                            Row(
                                Modifier
                                    .fillMaxWidth()
                                    .clickable {
                                        viewModel.playFrom(
                                            trackResults.map { r -> QueueTrack(r.uri, r.title, r.subtitle) },
                                            t.uri,
                                        )
                                        onClose()
                                    },
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Column(Modifier.weight(1f).padding(vertical = 8.dp)) {
                                    Text(t.title, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                    if (t.subtitle.isNotBlank()) {
                                        Text(
                                            t.subtitle,
                                            style = MaterialTheme.typography.labelSmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                                            maxLines = 1,
                                            overflow = TextOverflow.Ellipsis,
                                        )
                                    }
                                }
                                IconButton(onClick = { viewModel.enqueue(t.uri) }) {
                                    StoneIconArt(StoneIcon.QUEUE, stringResource(R.string.action_add_to_queue))
                                }
                            }
                        }
                    }
                    if (playlistResults.isNotEmpty()) {
                        item {
                            SearchHeading(
                                stringResource(R.string.search_heading_playlists, playlistResults.size),
                                Modifier.padding(top = 8.dp),
                            )
                        }
                        items(playlistResults, key = { "p:${it.name}" }) { pl ->
                            Column(
                                Modifier
                                    .fillMaxWidth()
                                    .clickable {
                                        libraryViewModel.playPlaylist(pl.name)
                                        onClose()
                                    }.padding(vertical = 8.dp),
                            ) {
                                Text(pl.name, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                Text(
                                    pluralStringResource(
                                        R.plurals.track_count,
                                        pl.trackUris.size,
                                        pl.trackUris.size,
                                    ),
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                )
                            }
                        }
                    }
                    if (presetResults.isNotEmpty()) {
                        item {
                            SearchHeading(
                                stringResource(R.string.search_heading_presets, presetResults.size),
                                Modifier.padding(top = 8.dp),
                            )
                        }
                        items(presetResults) { p ->
                            Text(
                                p.name,
                                Modifier
                                    .fillMaxWidth()
                                    .clickable {
                                        viewModel.applyPreset(p)
                                        onClose()
                                    }.padding(vertical = 8.dp),
                            )
                        }
                    }
                    if (trackResults.isEmpty() && playlistResults.isEmpty() && presetResults.isEmpty()) {
                        item {
                            Text(
                                stringResource(R.string.search_no_results, debounced),
                                Modifier.padding(vertical = 16.dp),
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }
            }
        }
    }
}

/** A result group's heading: an M3 title in the accent, marked as a heading for TalkBack. */
@Composable
private fun SearchHeading(
    text: String,
    modifier: Modifier = Modifier,
) {
    Text(
        text,
        modifier.semantics { heading() },
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.primary,
    )
}
