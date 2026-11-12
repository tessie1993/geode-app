package dev.geode.ui

import android.net.Uri
import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import dev.geode.data.LibraryRepository
import dev.geode.data.LibraryState
import dev.geode.data.MusicPlaylist
import dev.geode.data.PlaylistImportResult
import dev.geode.data.SmartPlaylist
import dev.geode.data.TagWriteOutcome
import dev.geode.di.PlayerSessionProvider
import dev.geode.model.DeviceTrack
import dev.geode.model.LibraryTrack
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.debounce
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.mapLatest
import kotlinx.coroutines.flow.stateIn
import javax.inject.Inject

private const val KEY_QUERY = "library_query"
private const val KEY_SORT = "library_sort"

/**
 * How long typing must pause before the library is searched. Long enough that a burst of
 * keystrokes runs the filter once, short enough that results still feel live.
 */
internal const val QUERY_DEBOUNCE_MS = 150L

/**
 * Keeps the combined state alive briefly after the last collector goes away, so a rotation or a
 * tab switch does not re-run the whole filter over the library.
 */
private val WhileUiSubscribed = SharingStarted.WhileSubscribed(5_000)

/**
 * Everything the library screen draws, in one immutable value.
 *
 * [browse] is already searched, ranked, sorted and grouped, off the main thread — the screen
 * renders what it is given rather than deciding what to show, so the ordering rules live
 * somewhere they can be reasoned about and no tab groups the library during composition.
 * While [isSearching] it is one result list (artists, albums, tracks) and the tabs step aside.
 *
 * [query] is the query [browse] answers, which trails the text field by the debounce; the field
 * itself reads [LibraryViewModel.query].
 *
 * There is deliberately no analysis state here. Nothing in the library waits on analysis, so
 * there is no "analysed" flag, no tempo and no key for a row to display.
 */
data class LibraryUiState(
    val browse: LibraryBrowseResult = LibraryBrowseResult(),
    val playlists: List<MusicPlaylist> = emptyList(),
    val overrides: Map<String, LibraryTrack> = emptyMap(),
    val query: String = "",
    val sort: LibrarySort = LibrarySort.TITLE,
    val isScanning: Boolean = false,
    /** The whole library (no search) is empty, and the device has been asked: see [showEmptyLibrary]. */
    val libraryEmpty: Boolean = false,
) {
    val tracks: List<DeviceTrack> get() = browse.tracks

    /** Distinguishes "your library is empty" from "nothing matched", which need different advice. */
    val isSearching: Boolean get() = query.isNotBlank()

    /**
     * The empty-library help (Rescan, choose a folder, listen to another app or the mic) instead
     * of a list: MediaStore has answered with nothing, no folder scan is running, and the user is
     * not searching (an empty result is "nothing matched", not "no music").
     */
    val showEmptyLibrary: Boolean get() = libraryEmpty && !isScanning && !isSearching
}

/**
 * The flows the library screen's state is built from. In the app they are [PlayerSession]'s; a
 * test hands in its own so the pipeline runs without a session.
 */
internal class LibraryInputs(
    val deviceTracks: Flow<List<DeviceTrack>>,
    val deviceTracksLoaded: Flow<Boolean>,
    val library: Flow<LibraryState>,
    val overrides: Flow<Map<String, LibraryTrack>>,
    val scanning: Flow<Boolean>,
    /** Settings › Preferences › Library's opening order, until the screen picks one of its own. */
    val defaultSort: Flow<LibrarySort> = flowOf(LibrarySort.TITLE),
) {
    companion object {
        fun of(session: PlayerSession) =
            LibraryInputs(
                deviceTracks = session.deviceTracks,
                deviceTracksLoaded = session.musicLibrary.deviceTracksLoaded,
                library = session.library,
                overrides = session.musicLibrary.trackOverrides,
                scanning = session.musicLibrary.libraryScanning,
                defaultSort = session.container.general.value.map { LibrarySort.fromName(it.librarySort) ?: LibrarySort.TITLE },
            )
    }
}

/** One search pass; [LibraryBrowse.browse] in the app, wrapped by tests to count and place the runs. */
internal typealias BrowseFn = suspend (LibraryIndex, String, LibrarySort) -> LibraryBrowseResult

@HiltViewModel
class LibraryViewModel
    internal constructor(
        sessionRef: () -> PlayerSession,
        inputs: LibraryInputs,
        private val savedStateHandle: SavedStateHandle,
        compute: CoroutineDispatcher,
        browseFn: BrowseFn = { index, query, sort -> LibraryBrowse.browse(index, query, sort) },
        indexFn: (List<DeviceTrack>) -> LibraryIndex = { LibraryBrowse.index(it) },
    ) : ViewModel() {
        @Inject
        constructor(
            sessions: PlayerSessionProvider,
            savedStateHandle: SavedStateHandle,
        ) : this(sessions::get, LibraryInputs.of(sessions.get()), savedStateHandle, Dispatchers.Default)

        // Lazy so a test of the browse pipeline never builds a session; in the app the provider's
        // one session already exists by the time this runs.
        private val session: PlayerSession by lazy(sessionRef)

        // The library itself: everything below except the three calls that start playback.
        private val libraryRepository: LibraryRepository by lazy { session.musicLibrary }

        // Search and sort survive process death with the rest of the screen's saved state, so
        // coming back to a filtered library does not silently reset it. A sort never picked here
        // follows Settings › Preferences, read live: the prefs load after the screen may have opened.
        private val queryFlow: StateFlow<String> = savedStateHandle.getStateFlow(KEY_QUERY, "")

        /** What the search field shows: every keystroke, at once. The results follow [debouncedQuery]. */
        val query: StateFlow<String> get() = queryFlow

        private val sortFlow =
            combine(savedStateHandle.getStateFlow<String?>(KEY_SORT, null), inputs.defaultSort) { stored, preferred ->
                LibrarySort.fromName(stored) ?: preferred
            }.distinctUntilChanged()

        /** A cleared box answers at once; typing waits for a pause, so a burst filters once. */
        @OptIn(FlowPreview::class)
        private val debouncedQuery: Flow<String> =
            queryFlow.debounce { if (it.isBlank()) 0L else QUERY_DEBOUNCE_MS }.distinctUntilChanged()

        /** One index per library change; the first pass on [compute] folds it, keystrokes then walk ready text. */
        private val index: Flow<LibraryIndex> = inputs.deviceTracks.map(indexFn)

        @OptIn(ExperimentalCoroutinesApi::class)
        private val browseFlow: Flow<Pair<LibraryBrowseResult, Pair<String, LibrarySort>>> =
            combine(index, debouncedQuery, sortFlow) { idx, query, sort -> Triple(idx, query, sort) }
                // A newer query or library makes the running pass moot: drop it, start the new one.
                .mapLatest { (idx, query, sort) -> browseFn(idx, query, sort) to (query to sort) }
                .flowOn(compute)

        val uiState: StateFlow<LibraryUiState> =
            combine(
                browseFlow,
                inputs.deviceTracks.map { it.isEmpty() }.distinctUntilChanged(),
                inputs.deviceTracksLoaded,
                inputs.library,
                combine(inputs.overrides, inputs.scanning) { overrides, scanning -> overrides to scanning },
            ) { browse, empty, loaded, library, (overrides, scanning) ->
                val (result, querySort) = browse
                LibraryUiState(
                    browse = result,
                    playlists = library.playlists,
                    overrides = overrides,
                    query = querySort.first,
                    sort = querySort.second,
                    isScanning = scanning,
                    libraryEmpty = loaded && empty,
                )
            }.stateIn(
                scope = viewModelScope,
                started = WhileUiSubscribed,
                initialValue = LibraryUiState(),
            )

        /**
         * Same title, artist and length, over the whole library (never the search), found on
         * [compute] once per library change rather than in the Duplicates tab's composition.
         */
        val duplicates: StateFlow<List<DuplicateGroup>> =
            inputs.deviceTracks
                .map { LibraryDuplicates.find(it) }
                .flowOn(compute)
                .stateIn(viewModelScope, WhileUiSubscribed, emptyList())

        fun setQuery(query: String) {
            savedStateHandle[KEY_QUERY] = query
        }

        fun setSort(sort: LibrarySort) {
            savedStateHandle[KEY_SORT] = sort.name
        }

        /** Settings › Library folders' "Hide tracks shorter than", in seconds; 0 lists everything. */
        val minTrackSeconds: StateFlow<Int> get() = libraryRepository.minTrackSeconds

        fun setMinTrackSeconds(seconds: Int) = libraryRepository.setMinTrackSeconds(seconds)

        val hiddenShortTracks: StateFlow<Int> get() = libraryRepository.hiddenShortTracks

        val mediaRoots: StateFlow<Set<String>> get() = libraryRepository.mediaRoots

        // Migration debt. [uiState] is the canonical read surface; these are the raw session flows
        // the other screens still collect directly, and each one should fold into the UiState of
        // the screen that uses it rather than being read straight off the session here.
        val library: StateFlow<LibraryState> get() = libraryRepository.library

        val deviceTracks: StateFlow<List<DeviceTrack>> get() = libraryRepository.deviceTracks

        val libraryScanning: StateFlow<Boolean> get() = libraryRepository.libraryScanning

        val trackOverrides: StateFlow<Map<String, LibraryTrack>> get() = libraryRepository.trackOverrides

        fun refreshDeviceTracks() = libraryRepository.refreshDeviceTracks()

        fun importTracks(uris: List<Uri>) = libraryRepository.importTracks(uris)

        fun importFolder(treeUri: Uri) = libraryRepository.importFolder(treeUri)

        fun removeMediaRoot(uriStr: String) = libraryRepository.removeMediaRoot(uriStr)

        fun rescanMediaRoots() = libraryRepository.rescanMediaRoots()

        suspend fun trackInfoFor(uriStr: String): LibraryTrack = libraryRepository.trackInfoFor(uriStr)

        fun saveTrackInfo(
            uri: String,
            title: String,
            artist: String,
            album: String,
            genre: String,
            year: Int,
            trackNo: Int,
            comment: String,
        ) = libraryRepository.saveTrackInfo(uri, title, artist, album, genre, year, trackNo, comment)

        suspend fun writeTrackInfo(
            uri: String,
            title: String,
            artist: String,
            album: String,
            genre: String,
            year: Int,
            trackNo: Int,
            comment: String,
        ): TagWriteOutcome = libraryRepository.writeTrackInfo(uri, title, artist, album, genre, year, trackNo, comment)

        fun createMusicPlaylist(
            name: String,
            uris: List<String> = emptyList(),
        ) = libraryRepository.createMusicPlaylist(name, uris)

        suspend fun importPlaylistFile(uri: Uri): PlaylistImportResult = libraryRepository.importPlaylistFile(uri)

        fun renameMusicPlaylist(
            oldName: String,
            newName: String,
        ) = libraryRepository.renameMusicPlaylist(oldName, newName)

        fun moveMusicPlaylistTrack(
            name: String,
            from: Int,
            to: Int,
        ) = libraryRepository.moveMusicPlaylistTrack(name, from, to)

        fun deleteMusicPlaylist(name: String) = libraryRepository.deleteMusicPlaylist(name)

        fun addTrackToPlaylist(
            playlist: String,
            uri: String,
        ) = libraryRepository.addTrackToPlaylist(playlist, uri)

        fun removeTrackFromPlaylist(
            playlist: String,
            uri: String,
        ) = libraryRepository.removeTrackFromPlaylist(playlist, uri)

        fun playPlaylist(
            playlist: String,
            startIndex: Int = 0,
        ) = session.playPlaylist(playlist, startIndex)

        fun saveSmartPlaylist(playlist: SmartPlaylist) = libraryRepository.saveSmartPlaylist(playlist)

        fun deleteSmartPlaylist(name: String) = libraryRepository.deleteSmartPlaylist(name)

        fun resolveSmartPlaylist(playlist: SmartPlaylist): List<DeviceTrack> = session.resolveSmartPlaylist(playlist)

        val favourites: StateFlow<Set<String>> get() = session.favourites

        fun toggleFavourite(uri: String) = session.toggleFavourite(uri)

        fun playFavourites() = session.playFavourites()

        fun playSmartPlaylist(playlist: SmartPlaylist) = session.playSmartPlaylist(playlist)
    }
