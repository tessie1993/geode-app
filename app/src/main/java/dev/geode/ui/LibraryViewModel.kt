package dev.geode.ui

import android.net.Uri
import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import dev.geode.data.MusicPlaylist
import dev.geode.di.PlayerSessionProvider
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import javax.inject.Inject

private const val KEY_QUERY = "library_query"
private const val KEY_SORT = "library_sort"

/**
 * Keeps the combined state alive briefly after the last collector goes away, so a rotation or a
 * tab switch does not re-run the whole filter over the library.
 */
private val WhileUiSubscribed = SharingStarted.WhileSubscribed(5_000)

/**
 * Everything the library screen draws, in one immutable value.
 *
 * [tracks] is already searched and sorted — the screen renders what it is given rather than
 * deciding what to show, so the ordering rules live somewhere they can be reasoned about.
 *
 * There is deliberately no analysis state here. Nothing in the library waits on analysis, so
 * there is no "analysed" flag, no tempo and no key for a row to display.
 */
data class LibraryUiState(
    val tracks: List<DeviceTrack> = emptyList(),
    val playlists: List<MusicPlaylist> = emptyList(),
    val overrides: Map<String, LibraryTrack> = emptyMap(),
    val query: String = "",
    val sort: LibrarySort = LibrarySort.TITLE,
    val isScanning: Boolean = false,
) {
    /** Distinguishes "your library is empty" from "nothing matched", which need different advice. */
    val isSearching: Boolean get() = query.isNotBlank()
}

@HiltViewModel
class LibraryViewModel
    @Inject
    constructor(
        private val sessions: PlayerSessionProvider,
        private val savedStateHandle: SavedStateHandle,
    ) : ViewModel() {
        private val session: PlayerSession = sessions.get()

        // Search and sort survive process death with the rest of the screen's saved state, so
        // coming back to a filtered library does not silently reset it.
        private val queryFlow: StateFlow<String> = savedStateHandle.getStateFlow(KEY_QUERY, "")
        private val sortFlow =
            savedStateHandle
                .getStateFlow(KEY_SORT, LibrarySort.TITLE.name)
                .map { stored -> LibrarySort.entries.firstOrNull { it.name == stored } ?: LibrarySort.TITLE }

        private val browseFlow = combine(queryFlow, sortFlow) { query, sort -> query to sort }

        val uiState: StateFlow<LibraryUiState> =
            combine(
                session.deviceTracks,
                session.library,
                session.trackOverrides,
                session.libraryScanning,
                browseFlow,
            ) { tracks, library, overrides, scanning, browse ->
                val (query, sort) = browse
                LibraryUiState(
                    tracks = LibraryBrowse.sort(LibraryBrowse.search(tracks, query), sort),
                    playlists = library.playlists,
                    overrides = overrides,
                    query = query,
                    sort = sort,
                    isScanning = scanning,
                )
            }.stateIn(
                scope = viewModelScope,
                started = WhileUiSubscribed,
                initialValue = LibraryUiState(),
            )

        fun setQuery(query: String) {
            savedStateHandle[KEY_QUERY] = query
        }

        fun setSort(sort: LibrarySort) {
            savedStateHandle[KEY_SORT] = sort.name
        }

        val mediaRoots: StateFlow<Set<String>> get() = session.mediaRoots

        // Migration debt. [uiState] is the canonical read surface; these are the raw session flows
        // the other screens still collect directly, and each one should fold into the UiState of
        // the screen that uses it rather than being read straight off the session here.
        val library: StateFlow<LibraryState> get() = session.library

        val deviceTracks: StateFlow<List<DeviceTrack>> get() = session.deviceTracks

        val libraryScanning: StateFlow<Boolean> get() = session.libraryScanning

        val trackOverrides: StateFlow<Map<String, LibraryTrack>> get() = session.trackOverrides

        fun refreshDeviceTracks() = session.refreshDeviceTracks()

        fun importTracks(uris: List<Uri>) = session.importTracks(uris)

        fun importFolder(treeUri: Uri) = session.importFolder(treeUri)

        fun removeMediaRoot(uriStr: String) = session.removeMediaRoot(uriStr)

        fun rescanMediaRoots() = session.rescanMediaRoots()

        fun trackOverride(uri: String): LibraryTrack? = session.trackOverride(uri)

        suspend fun trackInfoFor(uriStr: String): LibraryTrack = session.trackInfoFor(uriStr)

        fun saveTrackInfo(
            uri: String,
            title: String,
            artist: String,
            album: String,
            genre: String,
            year: Int,
            trackNo: Int,
            comment: String,
        ) = session.saveTrackInfo(uri, title, artist, album, genre, year, trackNo, comment)

        fun createMusicPlaylist(name: String) = session.createMusicPlaylist(name)

        fun renameMusicPlaylist(
            oldName: String,
            newName: String,
        ): Boolean = session.renameMusicPlaylist(oldName, newName)

        fun moveMusicPlaylistTrack(
            name: String,
            from: Int,
            to: Int,
        ) = session.moveMusicPlaylistTrack(name, from, to)

        fun deleteMusicPlaylist(name: String) = session.deleteMusicPlaylist(name)

        fun addTrackToPlaylist(
            playlist: String,
            uri: String,
        ) = session.addTrackToPlaylist(playlist, uri)

        fun removeTrackFromPlaylist(
            playlist: String,
            uri: String,
        ) = session.removeTrackFromPlaylist(playlist, uri)

        fun playPlaylist(
            playlist: String,
            startIndex: Int = 0,
        ) = session.playPlaylist(playlist, startIndex)
    }
