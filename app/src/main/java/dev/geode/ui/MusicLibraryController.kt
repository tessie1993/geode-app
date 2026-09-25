package dev.geode.ui

import android.app.Application
import android.content.SharedPreferences
import android.net.Uri
import dev.geode.data.MusicPlaylist
import dev.geode.data.MusicPlaylistStore
import dev.geode.data.NativeTags
import dev.geode.data.PlaylistFormats
import dev.geode.data.PlaylistParse
import dev.geode.data.SmartPlaylist
import dev.geode.data.SmartPlaylistStore
import dev.geode.data.TagWriteOutcome
import dev.geode.data.TrackTagEdit
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

/** Outcome of [MusicLibraryController.importPlaylistFile], shown to the user as a single result. */
sealed interface PlaylistImportResult {
    data class Imported(
        val name: String,
        val addedCount: Int,
        val unresolvedCount: Int,
        val ambiguousCount: Int,
    ) : PlaylistImportResult

    /** [why] is a diagnostic for logs, not shown to the user; the screen always shows one generic message. */
    data class Failed(
        val why: String,
    ) : PlaylistImportResult
}

data class LibraryState(
    val tracks: List<LibraryTrack> = emptyList(),
    val playlists: List<MusicPlaylist> = emptyList(),
    val smartPlaylists: List<SmartPlaylist> = emptyList(),
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
    private val smartPlaylists = SmartPlaylistStore(application)

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
            val smart = smartPlaylists.list()
            withContext(Dispatchers.Main) {
                _library.update {
                    if (it.tracks.isNotEmpty() || it.playlists.isNotEmpty()) {
                        it.copy(smartPlaylists = smart)
                    } else {
                        it.copy(tracks = tracks, playlists = playlists, smartPlaylists = smart)
                    }
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

    private fun queryDeviceTracksBlocking(): List<DeviceTrack> = DeviceTrackQuery.query(application)

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
        folder: String = "",
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
            folder = folder,
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
                    // Taken separately: a picker that granted read only would otherwise refuse both flags.
                    runCatching {
                        application.contentResolver.takePersistableUriPermission(
                            uri,
                            android.content.Intent.FLAG_GRANT_WRITE_URI_PERMISSION,
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

    /** Writes the edit into the audio file itself, keeping its album artist; see [TagWriteOutcome] for the result. */
    suspend fun writeTrackInfo(
        uri: String,
        title: String,
        artist: String,
        album: String,
        genre: String,
        year: Int,
        trackNo: Int,
        comment: String,
    ): TagWriteOutcome =
        withContext(Dispatchers.IO) {
            val target = Uri.parse(uri)
            val resolver = application.contentResolver
            val existing = NativeTags.read(resolver, target)
            val edit =
                TrackTagEdit(
                    title = title,
                    artist = artist,
                    album = album,
                    albumArtist = existing?.albumArtist.orEmpty(),
                    genre = genre,
                    comment = comment,
                    year = year,
                    track = trackNo,
                )
            NativeTags.write(resolver, target, edit)
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
                            found += libraryTrackFor(f.uri.toString(), metadataFor(f.uri), dir.uri.toString())
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

    fun createMusicPlaylist(
        name: String,
        uris: List<String> = emptyList(),
    ) {
        if (name.isBlank()) return
        scope.launch {
            val fresh =
                withContext(Dispatchers.IO) {
                    musicPlaylists.save(MusicPlaylist(name.trim()))
                    if (uris.isNotEmpty()) {
                        musicPlaylists.addTracks(name.trim(), uris)
                    }
                    musicPlaylists.list()
                }
            _library.update { it.copy(playlists = fresh) }
        }
    }

    /**
     * Renames, asynchronously.
     *
     * The returned flag reports only that the rename was *scheduled*: the store call happens on
     * [Dispatchers.IO] after this has returned, so it cannot say whether the rename succeeded.
     */
    fun renameMusicPlaylist(
        oldName: String,
        newName: String,
    ): Boolean {
        scope.launch {
            val fresh =
                withContext(Dispatchers.IO) {
                    val renamed = musicPlaylists.rename(oldName, newName.trim())
                    if (renamed) musicPlaylists.list() else null
                }
            if (fresh != null) {
                _library.update { it.copy(playlists = fresh) }
            }
        }
        return true
    }

    fun moveMusicPlaylistTrack(
        name: String,
        from: Int,
        to: Int,
    ) {
        scope.launch {
            val fresh =
                withContext(Dispatchers.IO) {
                    musicPlaylists.move(name, from, to)
                    musicPlaylists.list()
                }
            _library.update { it.copy(playlists = fresh) }
        }
    }

    fun deleteMusicPlaylist(name: String) {
        scope.launch {
            val fresh =
                withContext(Dispatchers.IO) {
                    musicPlaylists.delete(name)
                    musicPlaylists.list()
                }
            _library.update { it.copy(playlists = fresh) }
        }
    }

    fun addTrackToPlaylist(
        playlist: String,
        uri: String,
    ) {
        scope.launch {
            val fresh =
                withContext(Dispatchers.IO) {
                    musicPlaylists.addTrack(playlist, uri)
                    musicPlaylists.list()
                }
            _library.update { it.copy(playlists = fresh) }
        }
    }

    fun removeTrackFromPlaylist(
        playlist: String,
        uri: String,
    ) {
        scope.launch {
            val fresh =
                withContext(Dispatchers.IO) {
                    musicPlaylists.removeTrack(playlist, uri)
                    musicPlaylists.list()
                }
            _library.update { it.copy(playlists = fresh) }
        }
    }

    fun saveSmartPlaylist(playlist: SmartPlaylist) {
        scope.launch {
            val fresh =
                withContext(Dispatchers.IO) {
                    smartPlaylists.save(playlist)
                    smartPlaylists.list()
                }
            _library.update { it.copy(smartPlaylists = fresh) }
        }
    }

    fun deleteSmartPlaylist(name: String) {
        scope.launch {
            val fresh =
                withContext(Dispatchers.IO) {
                    smartPlaylists.delete(name)
                    smartPlaylists.list()
                }
            _library.update { it.copy(smartPlaylists = fresh) }
        }
    }

    /**
     * Reads an M3U/PLS/XSPF file the user picked, resolves its entries against the imported
     * library by file name, and creates a new playlist from whatever matched. Entries that name a
     * file nothing in the library has stay unresolved and are only reported, never guessed at.
     */
    suspend fun importPlaylistFile(uri: Uri): PlaylistImportResult =
        withContext(Dispatchers.IO) {
            val fileName = openableInfoFor(uri).first.ifBlank { uri.lastPathSegment.orEmpty() }
            val text =
                runCatching {
                    application.contentResolver.openInputStream(uri)?.use { stream ->
                        stream.readBytes().toString(Charsets.UTF_8)
                    }
                }.getOrNull()
            if (text == null) {
                return@withContext PlaylistImportResult.Failed("could not open the picked file")
            }
            when (val parsed = PlaylistFormats.parse(fileName, text)) {
                is PlaylistParse.Unreadable -> PlaylistImportResult.Failed(parsed.why)
                is PlaylistParse.Parsed -> {
                    val urisByFileName =
                        _library.value.tracks
                            .filter { it.fileName.isNotBlank() }
                            .groupBy({ it.fileName.lowercase() }, { it.uri })
                    val resolution = PlaylistFormats.resolve(parsed.entries, urisByFileName)
                    val name = uniquePlaylistName(parsed.name.ifBlank { fileName.ifBlank { "Playlist" } })
                    musicPlaylists.save(MusicPlaylist(name, resolution.uris))
                    withContext(Dispatchers.Main) { _library.update { it.copy(playlists = musicPlaylists.list()) } }
                    PlaylistImportResult.Imported(
                        name = name,
                        addedCount = resolution.uris.size,
                        unresolvedCount = resolution.missing.size,
                        ambiguousCount = resolution.ambiguous.size,
                    )
                }
            }
        }

    /** Never overwrites an existing playlist quietly — appends "(2)", "(3)", ... until the name is free. */
    private fun uniquePlaylistName(base: String): String {
        val taken = musicPlaylists.list().map { it.name }.toSet()
        if (base !in taken) return base
        var suffix = 2
        while ("$base ($suffix)" in taken) suffix++
        return "$base ($suffix)"
    }
}

private const val TAG = "MusicLibraryController"
