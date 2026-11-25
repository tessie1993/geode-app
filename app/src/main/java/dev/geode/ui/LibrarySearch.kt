package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Icon
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
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextOverflow
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.playback.PlaybackQueue
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt

/**
 * The header while searching: ← closes the search, × empties the field and keeps it open. It takes
 * the keyboard when opened empty; a query restored with the screen does not raise it again. The
 * keyboard's Search key only puts the keyboard away: the results are already there.
 */
@Composable
internal fun LibrarySearchField(
    query: String,
    onQueryChange: (String) -> Unit,
    onClose: () -> Unit,
) {
    val focus = remember { FocusRequester() }
    val focusManager = LocalFocusManager.current
    Row(
        Modifier
            .fillMaxWidth()
            .padding(start = GeodeSpacing.XS, end = GeodeSpacing.L, top = GeodeSpacing.S, bottom = GeodeSpacing.S),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        IconButton(onClick = onClose) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.search_close))
        }
        OutlinedTextField(
            value = query,
            onValueChange = onQueryChange,
            singleLine = true,
            placeholder = {
                Text(stringResource(R.string.library_search_hint), maxLines = 1, overflow = TextOverflow.Ellipsis)
            },
            trailingIcon =
                if (query.isEmpty()) {
                    null
                } else {
                    {
                        IconButton(onClick = {
                            onQueryChange("")
                            focus.requestFocus()
                        }) { StoneIconArt(StoneIcon.CLOSE, stringResource(R.string.library_search_clear)) }
                    }
                },
            keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.None, imeAction = ImeAction.Search),
            // Clearing focus puts the keyboard away with it.
            keyboardActions = KeyboardActions(onSearch = { focusManager.clearFocus() }),
            modifier = Modifier.weight(1f).focusRequester(focus),
        )
    }
    LaunchedEffect(Unit) { if (query.isEmpty()) focus.requestFocus() }
}

/**
 * What the query found, as one list whatever tab was open: the artists and albums it names, then
 * the tracks, best first (ranked natively, `core/library/Search`). An artist or album opens in
 * place with all its tracks; a tapped track plays the result list from there. [typed] is the
 * field's text, which the results in [state] trail by the debounce.
 */
@Composable
internal fun LibrarySearchResults(
    state: LibraryUiState,
    typed: String,
    viewModel: PlayerViewModel,
    libraryViewModel: LibraryViewModel,
    columns: Int,
) {
    val browse = state.browse
    // Keyed by the query: typing on closes whatever was opened from the last results.
    var openAlbum by rememberSaveable(state.query) { mutableStateOf<String?>(null) }
    var openArtist by rememberSaveable(state.query) { mutableStateOf<String?>(null) }
    val opened =
        browse.albumHits.firstOrNull { it.name == openAlbum } ?: browse.artistHits.firstOrNull { it.name == openArtist }
    val closeGroup = {
        openAlbum = null
        openArtist = null
    }
    val dismiss = rememberPredictiveDismiss(enabled = opened != null, onDismiss = closeGroup)
    // Kept above the branch so coming back from an album returns to where the list was; a new
    // query starts at the top, where its best match is.
    val grid = rememberLazyGridState()
    var scrolledFor by rememberSaveable { mutableStateOf(state.query) }
    LaunchedEffect(state.query) {
        if (scrolledFor != state.query) {
            grid.scrollToItem(0)
            scrolledFor = state.query
        }
    }
    if (opened != null) {
        GroupDetail(
            opened.name,
            opened.tracks,
            viewModel,
            libraryViewModel,
            columns,
            onBack = closeGroup,
            Modifier.dismissTransform(dismiss),
        )
        return
    }
    val queue = remember(browse.tracks) { browse.tracks.map(PlaybackQueue::queueTrack) }
    val overrides by libraryViewModel.trackOverrides.collectAsStateWithLifecycle()
    val favourites by libraryViewModel.favourites.collectAsStateWithLifecycle()
    val variousArtists = stringResource(R.string.library_search_various_artists)
    LazyVerticalGrid(GridCells.Fixed(columns), Modifier.libraryListWidth(columns), state = grid) {
        if (!state.isSearching) {
            // Nothing typed yet. Mid-debounce the list stays empty for a moment instead of flashing the tip.
            if (typed.isBlank()) {
                item(key = "prompt", span = { GridItemSpan(maxLineSpan) }, contentType = "note") {
                    SearchNote(stringResource(R.string.library_search_prompt))
                }
            }
            return@LazyVerticalGrid
        }
        if (browse.nothingFound) {
            item(key = "none", span = { GridItemSpan(maxLineSpan) }, contentType = "note") {
                SearchNote(
                    stringResource(R.string.library_no_results),
                    Modifier.semantics { liveRegion = LiveRegionMode.Polite },
                    stringResource(R.string.library_search_no_results_hint),
                )
            }
        }
        if (browse.artistHits.isNotEmpty()) {
            item(key = "heading:artists", span = { GridItemSpan(maxLineSpan) }, contentType = "heading") {
                ResultsHeading(stringResource(R.string.library_tab_artists))
            }
            items(browse.artistHits, key = { "artist:${it.name}" }, contentType = { "group" }) { hit ->
                val count = hit.tracks.size
                val tracks = pluralStringResource(R.plurals.track_count, count, count)
                GroupRow(hit.name, tracks, onClick = { openArtist = hit.name })
            }
        }
        if (browse.albumHits.isNotEmpty()) {
            item(key = "heading:albums", span = { GridItemSpan(maxLineSpan) }, contentType = "heading") {
                ResultsHeading(stringResource(R.string.library_tab_albums))
            }
            items(browse.albumHits, key = { "album:${it.name}" }, contentType = { "group" }) { hit ->
                val count = hit.tracks.size
                val artists = hit.tracks.map { it.artist }.distinct()
                val artist = artists.singleOrNull() ?: variousArtists
                val tracks = pluralStringResource(R.plurals.track_count, count, count)
                GroupRow(hit.name, "$artist \u00b7 $tracks", onClick = { openAlbum = hit.name })
            }
        }
        if (browse.tracks.isNotEmpty()) {
            item(key = "heading:tracks", span = { GridItemSpan(maxLineSpan) }, contentType = "heading") {
                ResultsHeading(stringResource(R.string.search_heading_tracks, browse.tracks.size))
            }
            items(browse.tracks, key = { it.uri }, contentType = { "track" }) { t ->
                TrackRow(t, viewModel, libraryViewModel, overrides[t.uri], t.uri in favourites, queue = queue)
            }
        }
    }
}

@Composable
private fun ResultsHeading(text: String) {
    Text(
        text,
        Modifier
            .padding(start = GeodeSpacing.L, end = GeodeSpacing.L, top = GeodeSpacing.M, bottom = GeodeSpacing.XS)
            .semantics { heading() },
        style = MaterialTheme.typography.titleSmall,
        color = MaterialTheme.colorScheme.primary,
    )
}

/** A line or two in place of results: the tip before typing, or that nothing matched. */
@Composable
private fun SearchNote(
    title: String,
    modifier: Modifier = Modifier,
    detail: String? = null,
) {
    Column(modifier.padding(GeodeSpacing.L), verticalArrangement = Arrangement.spacedBy(GeodeSpacing.XS)) {
        Text(title)
        if (detail != null) {
            Text(detail, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}
