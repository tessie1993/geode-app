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
import dev.geode.engine.bridge.LibraryNative
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
import java.util.concurrent.atomic.AtomicInteger

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

internal const val KEY_ROOTS = "roots"

/** The music folders saved in `geode-library`, for a caller with no repository at hand. */
internal fun savedMusicFolders(context: android.content.Context): Set<String> =
    context
        .getSharedPreferences(GeodePrefsFiles.LIBRARY, android.content.Context.MODE_PRIVATE)
        .getStringSet(KEY_ROOTS, emptySet())
        .orEmpty()

internal val MEDIA_ROOTS_PREF_KEYS =
    prefKeys(GeodePrefsFiles.LIBRARY) {
        device(PrefType.STRING_SET, KEY_ROOTS)
    }

// The tags rawFor asks the reader for, in the order native's `geode_track_tag_fallbacks` takes them.
private val RAW_TAGS =
    intArrayOf(
        android.media.MediaMetadataRetriever.METADATA_KEY_TITLE,
        android.media.MediaMetadataRetriever.METADATA_KEY_ARTIST,
        android.media.MediaMetadataRetriever.METADATA_KEY_ALBUM,
        android.media.MediaMetadataRetriever.METADATA_KEY_GENRE,
        android.media.MediaMetadataRetriever.METADATA_KEY_YEAR,
        android.media.MediaMetadataRetriever.METADATA_KEY_CD_TRACK_NUMBER,
    )

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

    /** What the tag reader and the platform say about one file; native's fallback chain turns it into a track. */
    private class RawFile(
        val uri: String,
        val tags: Array<String?>,
        val displayName: String,
        val sizeBytes: Long,
        val lastPathSegment: String?,
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

    private val _hiddenShortTracks = MutableStateFlow(0)

    override val hiddenShortTracks: StateFlow<Int> = _hiddenShortTracks

    // The newest scan asked for; scans run side by side, and one asked for earlier may finish later.
    private val deviceScans = AtomicInteger()

    override fun refreshDeviceTracks() {
        val scanId = deviceScans.incrementAndGet()
        scope.launch(Dispatchers.IO) {
            val scan = DeviceTrackQuery.scan(application, _minTrackSeconds.value)
            // Checked and published under one lock: a scan published after a newer one is stale.
            synchronized(deviceScans) {
                if (scanId != deviceScans.get()) return@launch
                _deviceTracks.value = scan.tracks
                _hiddenShortTracks.value = scan.hiddenShort
                _deviceTracksLoaded.value = true
            }
        }
    }

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

    private fun rawFor(uri: Uri): RawFile {
        // title, artist, album, genre, year, track number: as the reader gave them, null for none.
        val tags = arrayOfNulls<String>(RAW_TAGS.size)
        runCatching {
            val r = android.media.MediaMetadataRetriever()
            try {
                r.setDataSource(application, uri)
                RAW_TAGS.forEachIndexed { at, key -> tags[at] = r.extractMetadata(key) }
            } finally {
                bestEffort(TAG, "r.release()") { r.release() }
            }
        }
        val openable = openableInfoFor(uri)
        return RawFile(uri.toString(), tags, openable.first, openable.second, uri.lastPathSegment)
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

    /** One track per file, the whole batch in one native call (`core/library/TrackRules`, `tagFallbacks`). */
    private fun libraryTracksFor(files: List<RawFile>): List<LibraryTrack> {
        if (files.isEmpty()) return emptyList()
        val cells =
            files.flatMap { f -> f.tags.toList() + listOf(f.uri, f.displayName, f.lastPathSegment) }
        return TrackWire.tracks(
            LibraryNative.trackTagFallbacks(
                cells.toTypedArray(),
                files.map { it.sizeBytes }.toLongArray(),
                application.getString(dev.geode.R.string.library_untitled_track),
            ),
        )
    }

    override fun importTracks(uris: List<Uri>) {
        if (uris.isEmpty()) return
        scope.launch(Dispatchers.IO) {
            val files =
                uris.map { uri ->
                    application.contentResolver.persistGrant(uri, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
                    // Taken separately: a picker that granted read only would otherwise refuse both flags.
                    application.contentResolver.persistGrant(uri, android.content.Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
                    rawFor(uri)
                }
            val tracks = libraryTracksFor(files)
            if (tracks.isNotEmpty()) writeTracks { it.addAll(tracks) }
        }
    }

    fun trackOverride(uri: String): LibraryTrack? = _library.value.tracks.firstOrNull { it.uri == uri }

    override suspend fun trackInfoFor(uriStr: String): LibraryTrack =
        trackOverride(uriStr) ?: withContext(Dispatchers.IO) {
            libraryTracksFor(listOf(rawFor(Uri.parse(uriStr)))).firstOrNull()
                ?: LibraryTrack(uri = uriStr, title = application.getString(dev.geode.R.string.library_untitled_track))
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
        scope.launch(Dispatchers.IO) { writeTracks { it.updateMetadata(uri, title, artist, album, genre, year, trackNo, comment) } }
    }

    /**
     * Writes the track library and publishes the list it wrote under one lock, so writes on different
     * threads publish in the order they wrote and an older list never replaces a newer one.
     */
    private fun writeTracks(write: (TrackLibrary) -> List<LibraryTrack>?) {
        synchronized(trackLibrary) {
            write(trackLibrary)?.let { merged -> _library.update { it.copy(tracks = merged) } }
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
            val held = _library.value.tracks
            val flags = LibraryNative.trackTitleRefresh(held.flatMap { listOf(it.title, it.artist) }.toTypedArray())
            val files =
                held
                    .filterIndexed { at, _ -> flags.getOrNull(at) == 1 }
                    .mapNotNull { runCatching { rawFor(Uri.parse(it.uri)) }.getOrNull() }
            val fresh = libraryTracksFor(files).map { TrackLibrary.Fresh(it.uri, it.title, it.artist) }
            if (fresh.isNotEmpty()) writeTracks { it.applyFreshTitles(fresh) }
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
        // Gives back the grant importFolder kept; one never released is a permission leak.
        runCatching {
            application.contentResolver.releasePersistableUriPermission(
                Uri.parse(uriStr),
                android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION,
            )
        }
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
        val found = mutableListOf<RawFile>()
        runCatching {
            val root =
                androidx.documentfile.provider.DocumentFile
                    .fromTreeUri(application, treeUri) ?: return@runCatching

            // One native call per folder listing says what each entry is (`core/library/TrackRules`, `walkAction`).
            fun walk(
                dir: androidx.documentfile.provider.DocumentFile,
                depth: Int,
            ) {
                val entries = dir.listFiles()
                val actions =
                    LibraryNative.trackWalkActions(
                        Array(entries.size) { entries[it].name },
                        Array(entries.size) { entries[it].type },
                        IntArray(entries.size) { if (entries[it].isDirectory) 1 else 0 },
                        depth,
                    )
                entries.forEachIndexed { at, f ->
                    if (!job.isActive) return
                    when (actions.getOrNull(at)) {
                        LibraryNative.WALK_DESCEND -> walk(f, depth + 1)
                        LibraryNative.WALK_KEEP_AUDIO -> found += rawFor(f.uri)
                    }
                }
            }
            walk(root, 0)
        }
        // Outside the runCatching above, which would swallow the cancellation.
        job.ensureActive()
        val tracks = libraryTracksFor(found)
        if (tracks.isNotEmpty()) writeTracks { it.addAll(tracks) }
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

    /**
     * A new playlist holding [uris] (each once, in order), written as one file however many there are; a name whose
     * file is already taken gets a free one (native's rule, `MusicPlaylistStore.create`).
     */
    override fun createMusicPlaylist(
        name: String,
        uris: List<String>,
    ) = editPlaylists { musicPlaylists.create(name, uris) }

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
            val bytes =
                runCatching {
                    application.contentResolver.openInputStream(uri)?.use(ReadCap.PLAYLIST::read)
                }.getOrNull()
            if (bytes == null) {
                return@withContext PlaylistImportResult.Failed("could not open the picked file")
            }
            when (val parsed = PlaylistFormats.parse(fileName, bytes)) {
                is PlaylistParse.Unreadable -> PlaylistImportResult.Failed(parsed.why)
                is PlaylistParse.Parsed -> {
                    val resolution =
                        PlaylistFormats.resolve(parsed.entries, _library.value.tracks)
                            ?: return@withContext PlaylistImportResult.Failed("could not match the entries")
                    val untitled = application.getString(dev.geode.R.string.playlist_untitled)
                    // In the edits' lane, so the free name it picks is still free when it is written.
                    val name =
                        withContext(playlistEdits) {
                            val free = musicPlaylists.importPlaylist(parsed.name, fileName, untitled, resolution.uris)
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
}

private const val TAG = "MusicLibraryRepository"
