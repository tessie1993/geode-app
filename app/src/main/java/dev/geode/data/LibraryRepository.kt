package dev.geode.data

import android.net.Uri
import dev.geode.model.DeviceTrack
import dev.geode.model.LibraryTrack
import kotlinx.coroutines.flow.StateFlow

/**
 * The music library as a screen sees it: the device's tracks, the folders they come from, the
 * user's playlists and each track's tags. [MusicLibraryRepository] is the one implementation; a
 * ViewModel takes this, so its tests can hand in a fake without MediaStore or a session.
 */
interface LibraryRepository {
    val library: StateFlow<LibraryState>

    val trackOverrides: StateFlow<Map<String, LibraryTrack>>

    val deviceTracks: StateFlow<List<DeviceTrack>>

    /** False until the first device-track query has answered. */
    val deviceTracksLoaded: StateFlow<Boolean>

    /** "Hide tracks shorter than", in seconds; 0 lists everything. */
    val minTrackSeconds: StateFlow<Int>

    val mediaRoots: StateFlow<Set<String>>

    val libraryScanning: StateFlow<Boolean>

    fun setMinTrackSeconds(seconds: Int)

    fun refreshDeviceTracks()

    fun importTracks(uris: List<Uri>)

    suspend fun trackInfoFor(uriStr: String): LibraryTrack

    fun saveTrackInfo(
        uri: String,
        title: String,
        artist: String,
        album: String,
        genre: String,
        year: Int,
        trackNo: Int,
        comment: String,
    )

    suspend fun writeTrackInfo(
        uri: String,
        title: String,
        artist: String,
        album: String,
        genre: String,
        year: Int,
        trackNo: Int,
        comment: String,
    ): TagWriteOutcome

    fun importFolder(treeUri: Uri)

    fun removeMediaRoot(uriStr: String)

    fun rescanMediaRoots()

    fun createMusicPlaylist(
        name: String,
        uris: List<String> = emptyList(),
    )

    fun renameMusicPlaylist(
        oldName: String,
        newName: String,
    )

    fun moveMusicPlaylistTrack(
        name: String,
        from: Int,
        to: Int,
    )

    fun deleteMusicPlaylist(name: String)

    fun addTrackToPlaylist(
        playlist: String,
        uri: String,
    )

    fun removeTrackFromPlaylist(
        playlist: String,
        uri: String,
    )

    fun saveSmartPlaylist(playlist: SmartPlaylist)

    fun deleteSmartPlaylist(name: String)

    suspend fun importPlaylistFile(uri: Uri): PlaylistImportResult
}
