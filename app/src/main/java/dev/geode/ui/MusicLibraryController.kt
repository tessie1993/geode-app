package dev.geode.ui

import android.app.Application
import android.content.ContentUris
import android.content.SharedPreferences
import android.net.Uri
import android.provider.MediaStore
import dev.geode.data.MusicPlaylist
import dev.geode.data.MusicPlaylistStore
import dev.geode.util.bestEffort
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class DeviceTrack(
    val uri: String,
    val title: String,
    val artist: String,
    val album: String,
    val folder: String,
    val durationMs: Long,
    val addedSec: Long = 0L,
)

data class LibraryState(
    val tracks: List<LibraryTrack> = emptyList(),
    val playlists: List<MusicPlaylist> = emptyList(),
    val analyzing: Boolean = false,
    val analyzeProgress: Float = 0f,
)

private val AUDIO_EXTS = setOf("mp3", "wav", "flac", "ogg", "m4a", "aac", "opus", "wma", "aiff")

internal class MusicLibraryController(
    private val application: Application,
    private val libraryPrefs: SharedPreferences,
    private val scope: CoroutineScope,
) {
    private val trackLibrary = TrackLibrary(application)
    private val musicPlaylists = MusicPlaylistStore(application)

    private val _library = MutableStateFlow(LibraryState())
    val library: StateFlow<LibraryState> = _library

    val trackOverrides: StateFlow<Map<String, LibraryTrack>> =
        _library
            .map { st -> st.tracks.associateBy { it.uri } }
            .stateIn(
                scope,
                SharingStarted.Eagerly,
                _library.value.tracks.associateBy { it.uri },
            )

    fun refresh() {
        scope.launch(Dispatchers.IO) {
            val tracks = trackLibrary.list()
            val playlists = musicPlaylists.list()
            withContext(Dispatchers.Main) {
                _library.update {
                    if (it.tracks.isNotEmpty() || it.playlists.isNotEmpty()) it else it.copy(tracks = tracks, playlists = playlists)
                }
            }
        }
    }

    private data class FileMeta(
        val title: String,
        val artist: String = "",
        val album: String = "",
        val genre: String = "",
        val year: Int = 0,
        val trackNo: Int = 0,
        val fileName: String = "",
        val sizeBytes: Long = 0L,
    )

    private val _deviceTracks = MutableStateFlow<List<DeviceTrack>>(emptyList())

    val deviceTracks: StateFlow<List<DeviceTrack>> = _deviceTracks

    fun refreshDeviceTracks() {
        scope.launch(Dispatchers.IO) {
            _deviceTracks.value = queryDeviceTracksBlocking()
        }
    }

    private fun queryDeviceTracksBlocking(): List<DeviceTrack> {
        val permission =
            if (android.os.Build.VERSION.SDK_INT >= 33) {
                android.Manifest.permission.READ_MEDIA_AUDIO
            } else {
                android.Manifest.permission.READ_EXTERNAL_STORAGE
            }
        val granted =
            androidx.core.content.ContextCompat
                .checkSelfPermission(application, permission) ==
                android.content.pm.PackageManager.PERMISSION_GRANTED
        if (!granted) return emptyList()
        val out = mutableListOf<DeviceTrack>()
        val proj =
            arrayOf(
                MediaStore.Audio.Media._ID,
                MediaStore.Audio.Media.TITLE,
                MediaStore.Audio.Media.ARTIST,
                MediaStore.Audio.Media.ALBUM,
                MediaStore.Audio.Media.DURATION,
                MediaStore.Audio.Media.DATA,
                MediaStore.Audio.Media.DATE_ADDED,
            )
        runCatching {
            application.contentResolver
                .query(
                    MediaStore.Audio.Media.EXTERNAL_CONTENT_URI,
                    proj,
                    "${MediaStore.Audio.Media.IS_MUSIC} != 0",
                    null,
                    "${MediaStore.Audio.Media.TITLE} COLLATE NOCASE ASC",
                )?.use { c ->
                    val id = c.getColumnIndexOrThrow(MediaStore.Audio.Media._ID)
                    val ti = c.getColumnIndexOrThrow(MediaStore.Audio.Media.TITLE)
                    val ar = c.getColumnIndexOrThrow(MediaStore.Audio.Media.ARTIST)
                    val al = c.getColumnIndexOrThrow(MediaStore.Audio.Media.ALBUM)
                    val du = c.getColumnIndexOrThrow(MediaStore.Audio.Media.DURATION)
                    val da = c.getColumnIndexOrThrow(MediaStore.Audio.Media.DATA)
                    val ad = c.getColumnIndexOrThrow(MediaStore.Audio.Media.DATE_ADDED)
                    while (c.moveToNext()) {
                        val uri = ContentUris.withAppendedId(MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, c.getLong(id))
                        val path = c.getString(da).orEmpty()
                        out +=
                            DeviceTrack(
                                uri = uri.toString(),
                                title = c.getString(ti) ?: "Unknown",
                                artist = c.getString(ar) ?: "Unknown artist",
                                album = c.getString(al) ?: "Unknown album",
                                folder = path.substringBeforeLast('/', ""),
                                durationMs = c.getLong(du),
                                addedSec = c.getLong(ad),
                            )
                    }
                }
        }
        return out
    }

    private fun metadataFor(uri: Uri): FileMeta {
        var title: String? = null
        var artist: String? = null
        var album = ""
        var genre = ""
        var year = 0
        var trackNo = 0
        runCatching {
            val r = android.media.MediaMetadataRetriever()
            try {
                r.setDataSource(application, uri)

                fun tag(key: Int): String? = r.extractMetadata(key)?.trim()?.ifBlank { null }
                title = tag(android.media.MediaMetadataRetriever.METADATA_KEY_TITLE)
                artist = tag(android.media.MediaMetadataRetriever.METADATA_KEY_ARTIST)
                album = tag(android.media.MediaMetadataRetriever.METADATA_KEY_ALBUM) ?: ""
                genre = tag(android.media.MediaMetadataRetriever.METADATA_KEY_GENRE) ?: ""
                year =
                    tag(android.media.MediaMetadataRetriever.METADATA_KEY_YEAR)
                        ?.filter { it.isDigit() }
                        ?.take(4)
                        ?.toIntOrNull() ?: 0
                trackNo =
                    tag(android.media.MediaMetadataRetriever.METADATA_KEY_CD_TRACK_NUMBER)
                        ?.substringBefore('/')
                        ?.trim()
                        ?.toIntOrNull() ?: 0
            } finally {
                bestEffort(TAG, "r.release()") { r.release() }
            }
        }
        val openable = openableInfoFor(uri)
        if (title == null) title = openable.first.ifBlank { null }?.substringBeforeLast('.')
        return FileMeta(
            title = title ?: uri.lastPathSegment?.substringAfterLast('/')?.substringBeforeLast('.') ?: "Track",
            artist = artist ?: "",
            album = album,
            genre = genre,
            year = year,
            trackNo = trackNo,
            fileName = openable.first,
            sizeBytes = openable.second,
        )
    }

    private fun openableInfoFor(uri: Uri): Pair<String, Long> =
        runCatching {
            val cols = arrayOf(android.provider.OpenableColumns.DISPLAY_NAME, android.provider.OpenableColumns.SIZE)
            application
                .contentResolver
                .query(uri, cols, null, null, null)
                ?.use { c ->
                    if (!c.moveToFirst()) return@use null
                    val ni = c.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
                    val si = c.getColumnIndex(android.provider.OpenableColumns.SIZE)
                    val name = if (ni >= 0 && !c.isNull(ni)) c.getString(ni).orEmpty() else ""
                    val size = if (si >= 0 && !c.isNull(si)) c.getLong(si) else 0L
                    name to size
                }
        }.getOrNull() ?: ("" to 0L)

    private fun libraryTrackFor(
        uriStr: String,
        m: FileMeta,
    ): LibraryTrack =
        LibraryTrack(
            uri = uriStr,
            title = m.title,
            artist = m.artist,
            album = m.album,
            genre = m.genre,
            year = m.year,
            trackNo = m.trackNo,
            fileName = m.fileName,
            sizeBytes = m.sizeBytes,
        )

    fun importTracks(uris: List<Uri>) {
        if (uris.isEmpty()) return
        scope.launch(Dispatchers.IO) {
            val tracks =
                uris.map { uri ->
                    runCatching {
                        application.contentResolver.takePersistableUriPermission(
                            uri,
                            android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION,
                        )
                    }
                    libraryTrackFor(uri.toString(), metadataFor(uri))
                }
            trackLibrary.addAll(tracks)?.let { merged -> _library.update { it.copy(tracks = merged) } }
        }
    }

    fun trackOverride(uri: String): LibraryTrack? = _library.value.tracks.firstOrNull { it.uri == uri }

    suspend fun trackInfoFor(uriStr: String): LibraryTrack =
        trackOverride(uriStr) ?: withContext(Dispatchers.IO) {
            libraryTrackFor(uriStr, metadataFor(Uri.parse(uriStr)))
        }

    fun saveTrackInfo(
        uri: String,
        title: String,
        artist: String,
        album: String,
        genre: String,
        year: Int,
        trackNo: Int,
        comment: String,
    ) {
        scope.launch(Dispatchers.IO) {
            val merged = trackLibrary.updateMetadata(uri, title, artist, album, genre, year, trackNo, comment)
            merged?.let { withContext(Dispatchers.Main) { _library.update { s -> s.copy(tracks = it) } } }
        }
    }

    fun noteAnalysis(
        uri: Uri,
        timeline: dev.geode.analysis.FeatureTimeline,
    ) {
        trackLibrary
            .updateAnalysis(uri.toString(), metadataFor(uri).title, timeline.durationMs, timeline.bpm, timeline.key)
            ?.let { merged -> _library.update { it.copy(tracks = merged) } }
    }

    fun refreshNumericTitles() {
        scope.launch(Dispatchers.IO) {
            val bad =
                _library.value.tracks.filter {
                    it.title.matches(Regex("^[0-9:%A-F]{4,}$")) || it.artist.isEmpty() && it.title.matches(Regex("^\\d+$"))
                }
            var latest: List<LibraryTrack>? = null
            for (t in bad) {
                runCatching {
                    val (title, artist) = metadataFor(Uri.parse(t.uri))
                    if (title != t.title || artist != t.artist) {
                        trackLibrary.updateMetadata(t.uri, title, artist)?.let { latest = it }
                    }
                }
            }
            latest?.let { l -> withContext(Dispatchers.Main) { _library.update { it.copy(tracks = l) } } }
        }
    }

    private val _mediaRoots =
        MutableStateFlow<Set<String>>(libraryPrefs.getStringSet("roots", emptySet()) ?: emptySet())

    val mediaRoots: StateFlow<Set<String>> = _mediaRoots

    private val _libraryScanning = MutableStateFlow(false)
    val libraryScanning: StateFlow<Boolean> = _libraryScanning

    fun importFolder(treeUri: Uri) {
        runCatching {
            application.contentResolver.takePersistableUriPermission(
                treeUri,
                android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION,
            )
        }
        _mediaRoots.update { it + treeUri.toString() }
        libraryPrefs.edit().putStringSet("roots", _mediaRoots.value).apply()
        scope.launch(Dispatchers.IO) {
            _libraryScanning.value = true
            try {
                scanTreeBlocking(treeUri)
            } finally {
                _libraryScanning.value = false
            }
        }
    }

    fun removeMediaRoot(uriStr: String) {
        _mediaRoots.update { it - uriStr }
        libraryPrefs.edit().putStringSet("roots", _mediaRoots.value).apply()
    }

    fun rescanMediaRoots() {
        if (_libraryScanning.value) return
        scope.launch(Dispatchers.IO) {
            _libraryScanning.value = true
            try {
                for (root in _mediaRoots.value) {
                    scanTreeBlocking(Uri.parse(root))
                }
            } finally {
                _libraryScanning.value = false
            }
        }
    }

    private suspend fun scanTreeBlocking(treeUri: Uri) {
        val found = mutableListOf<LibraryTrack>()
        runCatching {
            val root =
                androidx.documentfile.provider.DocumentFile
                    .fromTreeUri(application, treeUri) ?: return@runCatching

            fun walk(
                dir: androidx.documentfile.provider.DocumentFile,
                depth: Int,
            ) {
                if (depth > 8) return
                dir.listFiles().forEach { f ->
                    val name = f.name ?: return@forEach
                    if (name.startsWith(".")) return@forEach
                    if (f.isDirectory) {
                        walk(f, depth + 1)
                    } else {
                        val isAudio =
                            f.type?.startsWith("audio/") == true ||
                                name.substringAfterLast('.', "").lowercase() in AUDIO_EXTS
                        if (isAudio) {
                            found += libraryTrackFor(f.uri.toString(), metadataFor(f.uri))
                        }
                    }
                }
            }
            walk(root, 0)
        }
        if (found.isNotEmpty()) {
            val merged = trackLibrary.addAll(found)
            merged?.let { withContext(Dispatchers.Main) { _library.update { s -> s.copy(tracks = it) } } }
        }
    }

    fun createMusicPlaylist(name: String) {
        if (name.isBlank()) return
        musicPlaylists.save(MusicPlaylist(name.trim()))
        _library.update { it.copy(playlists = musicPlaylists.list()) }
    }

    fun renameMusicPlaylist(
        oldName: String,
        newName: String,
    ): Boolean {
        val renamed = musicPlaylists.rename(oldName, newName.trim())
        if (renamed) {
            _library.update { it.copy(playlists = musicPlaylists.list()) }
        }
        return renamed
    }

    fun moveMusicPlaylistTrack(
        name: String,
        from: Int,
        to: Int,
    ) {
        musicPlaylists.move(name, from, to)
        _library.update { it.copy(playlists = musicPlaylists.list()) }
    }

    fun deleteMusicPlaylist(name: String) {
        musicPlaylists.delete(name)
        _library.update { it.copy(playlists = musicPlaylists.list()) }
    }

    fun addTrackToPlaylist(
        playlist: String,
        uri: String,
    ) {
        musicPlaylists.addTrack(playlist, uri)
        _library.update { it.copy(playlists = musicPlaylists.list()) }
    }

    fun removeTrackFromPlaylist(
        playlist: String,
        uri: String,
    ) {
        musicPlaylists.removeTrack(playlist, uri)
        _library.update { it.copy(playlists = musicPlaylists.list()) }
    }
}

private const val TAG = "MusicLibraryController"
