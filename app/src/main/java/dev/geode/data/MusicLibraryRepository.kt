package dev.geode.data

import android.app.Application
import android.content.SharedPreferences
import android.database.ContentObserver
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import dev.geode.data.prefs.PrefType
import dev.geode.data.prefs.prefKeys
import dev.geode.model.DeviceTrack
import dev.geode.model.LibraryTrack
import dev.geode.util.bestEffort
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.debounce
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** Outcome of [MusicLibraryRepository.importPlaylistFile], shown to the user as a single result. */
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
)

/**
 * One rescan per burst of MediaStore changes: a scan of a folder notifies once per file, and each
 * rescan is a whole query, so the library waits until the collection has been quiet for [quietMs].
 */
@OptIn(FlowPreview::class)
internal fun Flow<Unit>.libraryRescans(quietMs: Long = RESCAN_QUIET_MS): Flow<Unit> = debounce(quietMs)

private const val RESCAN_QUIET_MS = 1_000L

private const val KEY_ROOTS = "roots"

internal val MEDIA_ROOTS_PREF_KEYS =
    prefKeys(GeodePrefsFiles.LIBRARY) {
        device(PrefType.STRING_SET, KEY_ROOTS)
    }

private val AUDIO_EXTS = setOf("mp3", "wav", "flac", "ogg", "m4a", "aac", "opus", "wma", "aiff")

@OptIn(ExperimentalCoroutinesApi::class)
internal class MusicLibraryRepository(
    private val application: Application,
    private val libraryPrefs: SharedPreferences,
    private val scope: CoroutineScope,
    private val playlistEdits: CoroutineDispatcher = Dispatchers.IO.limitedParallelism(1),
) : LibraryRepository {
    private val trackLibrary = TrackLibrary(application)
    private val musicPlaylists = MusicPlaylistStore(application)
    private val smartPlaylists = SmartPlaylistStore(application)

    private val _library = MutableStateFlow(LibraryState())
    override val library: StateFlow<LibraryState> = _library

    override val trackOverrides: StateFlow<Map<String, LibraryTrack>> =
        _library
            .map { st -> st.tracks.associateBy { it.uri } }
            .stateIn(
                scope,
                SharingStarted.Eagerly,
                _library.value.tracks.associateBy { it.uri },
            )

    fun refresh() {
        // In the playlist edits' lane: a list read before an edit can then never be published after it.
        scope.launch(playlistEdits) {
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

    override val deviceTracks: StateFlow<List<DeviceTrack>> = _deviceTracks

    private val _deviceTracksLoaded = MutableStateFlow(false)

    /**
     * False until the first MediaStore query has answered. [deviceTracks] starts empty, and an
     * empty list before the query is "not read yet", not "no music": the library's empty state
     * (Rescan, choose a folder, listen to another app) waits for this.
     */
    override val deviceTracksLoaded: StateFlow<Boolean> = _deviceTracksLoaded

    private val libraryPrefsStore = LibraryPrefsStore(libraryPrefs)

    private val _minTrackSeconds = MutableStateFlow(libraryPrefsStore.minTrackSeconds())

    /** Settings › Library folders' "Hide tracks shorter than"; see [dev.geode.data.MinTrackLength]. */
    override val minTrackSeconds: StateFlow<Int> = _minTrackSeconds

    override fun setMinTrackSeconds(seconds: Int) {
        libraryPrefsStore.setMinTrackSeconds(seconds)
        _minTrackSeconds.value = libraryPrefsStore.minTrackSeconds()
        refreshDeviceTracks()
    }

    override fun refreshDeviceTracks() {
        scope.launch(Dispatchers.IO) {
            _deviceTracks.value = queryDeviceTracksBlocking()
            _deviceTracksLoaded.value = true
        }
    }

    private fun queryDeviceTracksBlocking(): List<DeviceTrack> = DeviceTrackQuery.query(application, _minTrackSeconds.value)

    /**
     * A file MediaStore indexes while the app runs (a download, a copy over USB, a scan finishing)
     * reaches the library without a restart: every change to the audio collection queues a rescan,
     * one per burst ([libraryRescans]). Ends with [scope], which unregisters the observer.
     */
    init {
        scope.launch { mediaStoreChanges().libraryRescans().collect { refreshDeviceTracks() } }
    }

    private fun mediaStoreChanges(): Flow<Unit> =
        callbackFlow {
            val observer =
                object : ContentObserver(Handler(Looper.getMainLooper())) {
                    override fun onChange(selfChange: Boolean) {
                        trySend(Unit)
                    }
                }
            val resolver = application.contentResolver
            resolver.registerContentObserver(MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, true, observer)
            awaitClose { resolver.unregisterContentObserver(observer) }
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
            title =
                title ?: uri.lastPathSegment?.substringAfterLast('/')?.substringBeforeLast('.')
                    ?: application.getString(dev.geode.R.string.library_untitled_track),
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

    override fun importTracks(uris: List<Uri>) {
        if (uris.isEmpty()) return
        scope.launch(Dispatchers.IO) {
            val tracks =
                uris.map { uri ->
                    application.contentResolver.persistGrant(uri, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    // Taken separately: a picker that granted read only would otherwise refuse both flags.
                    application.contentResolver.persistGrant(uri, android.content.Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
                    libraryTrackFor(uri.toString(), metadataFor(uri))
                }
            trackLibrary.addAll(tracks)?.let { merged -> _library.update { it.copy(tracks = merged) } }
        }
    }

    fun trackOverride(uri: String): LibraryTrack? = _library.value.tracks.firstOrNull { it.uri == uri }

    override suspend fun trackInfoFor(uriStr: String): LibraryTrack =
        trackOverride(uriStr) ?: withContext(Dispatchers.IO) {
            libraryTrackFor(uriStr, metadataFor(Uri.parse(uriStr)))
        }

    override fun saveTrackInfo(
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
    override suspend fun writeTrackInfo(
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
        MutableStateFlow<Set<String>>(libraryPrefs.getStringSet(KEY_ROOTS, emptySet()) ?: emptySet())

    override val mediaRoots: StateFlow<Set<String>> = _mediaRoots

    private val _libraryScanning = MutableStateFlow(false)
    override val libraryScanning: StateFlow<Boolean> = _libraryScanning

    override fun importFolder(treeUri: Uri) {
        application.contentResolver.persistGrant(treeUri, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
        _mediaRoots.update { it + treeUri.toString() }
        libraryPrefs.edit().putStringSet(KEY_ROOTS, _mediaRoots.value).apply()
        scope.launch(Dispatchers.IO) {
            _libraryScanning.value = true
            try {
                scanTreeBlocking(treeUri)
            } finally {
                _libraryScanning.value = false
            }
        }
    }

    override fun removeMediaRoot(uriStr: String) {
        _mediaRoots.update { it - uriStr }
        libraryPrefs.edit().putStringSet(KEY_ROOTS, _mediaRoots.value).apply()
    }

    override fun rescanMediaRoots() {
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

    /**
     * Walks a picked folder, one metadata read per audio file. It is blocking work with no
     * suspension point, so it asks its job between files: once the session's scope is cancelled it
     * stops walking, and it never writes what a cancelled scan found.
     */
    private suspend fun scanTreeBlocking(treeUri: Uri) {
        val job = currentCoroutineContext().job
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
                    if (!job.isActive) return
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
        // Outside the runCatching above, which would swallow the cancellation.
        job.ensureActive()
        if (found.isNotEmpty()) {
            val merged = trackLibrary.addAll(found)
            merged?.let { withContext(Dispatchers.Main) { _library.update { s -> s.copy(tracks = it) } } }
        }
    }

    /**
     * Every playlist edit runs here, one at a time and in the order it was asked for: each one
     * reads the playlist folder, fsyncs a file and reads the folder again for the screen, which is
     * too much for the main thread a tap arrives on. One lane keeps "create, then add" in order.
     */
    private fun editPlaylists(edit: () -> Unit) {
        scope.launch(playlistEdits) {
            edit()
            val playlists = musicPlaylists.list()
            _library.update { it.copy(playlists = playlists) }
        }
    }

    private fun editSmartPlaylists(edit: () -> Unit) {
        scope.launch(playlistEdits) {
            edit()
            val smart = smartPlaylists.list()
            _library.update { it.copy(smartPlaylists = smart) }
        }
    }

    /** A new playlist holding [uris] (each once, in order), written as one file however many there are. */
    override fun createMusicPlaylist(
        name: String,
        uris: List<String>,
    ) {
        if (name.isBlank()) return
        editPlaylists { musicPlaylists.save(MusicPlaylist(name.trim(), uris.distinct())) }
    }

    override fun renameMusicPlaylist(
        oldName: String,
        newName: String,
    ) = editPlaylists { musicPlaylists.rename(oldName, newName.trim()) }

    override fun moveMusicPlaylistTrack(
        name: String,
        from: Int,
        to: Int,
    ) = editPlaylists { musicPlaylists.move(name, from, to) }

    override fun deleteMusicPlaylist(name: String) = editPlaylists { musicPlaylists.delete(name) }

    override fun addTrackToPlaylist(
        playlist: String,
        uri: String,
    ) = editPlaylists { musicPlaylists.addTrack(playlist, uri) }

    override fun removeTrackFromPlaylist(
        playlist: String,
        uri: String,
    ) = editPlaylists { musicPlaylists.removeTrack(playlist, uri) }

    override fun saveSmartPlaylist(playlist: SmartPlaylist) = editSmartPlaylists { smartPlaylists.save(playlist) }

    override fun deleteSmartPlaylist(name: String) = editSmartPlaylists { smartPlaylists.delete(name) }

    /**
     * Reads an M3U/PLS/XSPF file the user picked, resolves its entries against the imported
     * library by file name, and creates a new playlist from whatever matched. Entries that name a
     * file nothing in the library has stay unresolved and are only reported, never guessed at.
     */
    override suspend fun importPlaylistFile(uri: Uri): PlaylistImportResult =
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
                    val untitled = application.getString(dev.geode.R.string.playlist_untitled)
                    // In the edits' lane, so the free name it picks is still free when it is written.
                    val name =
                        withContext(playlistEdits) {
                            val free = uniquePlaylistName(parsed.name.ifBlank { fileName.ifBlank { untitled } })
                            musicPlaylists.save(MusicPlaylist(free, resolution.uris))
                            val playlists = musicPlaylists.list()
                            _library.update { it.copy(playlists = playlists) }
                            free
                        }
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

private const val TAG = "MusicLibraryRepository"
