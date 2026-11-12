package dev.geode

import android.content.ContentResolver
import android.content.ContentUris
import android.content.Context
import android.database.Cursor
import android.net.Uri
import android.os.Bundle
import android.provider.MediaStore
import dev.geode.data.AppStateCodec
import dev.geode.data.AppStateRepository
import dev.geode.data.AutoDirectorCodec
import dev.geode.data.AutoDirectorStore
import dev.geode.data.BehaviourCodec
import dev.geode.data.BehaviourRepository
import dev.geode.data.CustomizeGroupsCodec
import dev.geode.data.CustomizeGroupsStore
import dev.geode.data.GeneralSettingsCodec
import dev.geode.data.GeneralSettingsRepository
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.LibrarySettingsCodec
import dev.geode.data.LibrarySettingsRepository
import dev.geode.data.MacroBankCodec
import dev.geode.data.MacroStore
import dev.geode.data.MediaStoreSource
import dev.geode.data.NoteResponseCodec
import dev.geode.data.NoteResponseStore
import dev.geode.data.OpenedAudioSource
import dev.geode.data.PerformanceStore
import dev.geode.data.SoundInputSettingsCodec
import dev.geode.data.SoundInputSettingsRepository
import dev.geode.data.StyleShelfCodec
import dev.geode.data.StyleShelfStore
import dev.geode.data.VisualsSettingsCodec
import dev.geode.data.VisualsSettingsRepository
import dev.geode.data.prefs.PrefsSettingsRepository
import dev.geode.util.bestEffort
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

class GeodeContainer(
    context: Context,
) {
    val appScope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    val prefsFiles = GeodePrefsFiles(context)

    // The settings repositories (#1009): one instance each for the app, its services and the screen
    // saver, every load started here, at process start.

    val behaviour: BehaviourRepository = PrefsSettingsRepository(prefsFiles.general, BehaviourCodec, appScope)

    val general: GeneralSettingsRepository = PrefsSettingsRepository(prefsFiles.general, GeneralSettingsCodec, appScope)

    val soundInput: SoundInputSettingsRepository = PrefsSettingsRepository(prefsFiles.general, SoundInputSettingsCodec, appScope)

    val visuals: VisualsSettingsRepository = PrefsSettingsRepository(prefsFiles.general, VisualsSettingsCodec, appScope)

    val library: LibrarySettingsRepository = PrefsSettingsRepository(prefsFiles.general, LibrarySettingsCodec, appScope)

    /** What the app remembers about this install (onboarding, the tour); not a preference. */
    val appState: AppStateRepository = PrefsSettingsRepository(prefsFiles.general, AppStateCodec, appScope)

    // The process-wide engine configuration: each hands its saved value to native as soon as the
    // read is back, so the app, the wallpaper, the dream and an export all start from it.

    val macros = MacroStore(PrefsSettingsRepository(prefsFiles.general, MacroBankCodec(), appScope), appScope)

    val autoDirector =
        AutoDirectorStore(PrefsSettingsRepository(prefsFiles.general, AutoDirectorCodec, appScope), appScope) {
            withContext(Dispatchers.IO) { AutoDirectorCodec.readKept(prefsFiles.modulation) }
        }

    val noteResponse = NoteResponseStore(PrefsSettingsRepository(prefsFiles.general, NoteResponseCodec, appScope), appScope)

    /** The Styles grid's favourites and recents. */
    val styleShelf = StyleShelfStore(PrefsSettingsRepository(prefsFiles.viz, StyleShelfCodec, appScope), appScope)

    /** Customize's tab and the groups the user opened. */
    val customizeGroups = CustomizeGroupsStore(PrefsSettingsRepository(prefsFiles.general, CustomizeGroupsCodec, appScope), appScope)

    /** Settings › Performance; the wallpaper and dream read the same prefs through their own store. */
    val performance: PerformanceStore by lazy { PerformanceStore(prefsFiles.general, context.applicationContext) }

    /** The `ContentResolver` source the screens reach their consent requests and picked-folder grants through. */
    val mediaStore: MediaStoreSource by lazy {
        MediaStoreSource(context.applicationContext.contentResolver, Dispatchers.IO)
    }

    /** The checks an audio file another app opened passes before it plays ("Open with"). */
    val openedAudio: OpenedAudioSource by lazy { OpenedAudioSource(context.applicationContext, Dispatchers.IO) }

    init {
        warmPlaybackPrefs()
        sweepStaleRenderScratch(context.applicationContext.cacheDir)
        sweepOrphanedPendingExports(context.applicationContext)
        sweepRetiredAnalysisCache(context.applicationContext.filesDir)
    }

    /**
     * Deletes the per-track analysis cache (`filesDir/analysis`) older builds wrote. The offline
     * analyser that read it is gone (the sound engine hears the track itself, live and in export),
     * and its Settings row with it, so nothing else would ever reclaim the space.
     */
    private fun sweepRetiredAnalysisCache(filesDir: File) {
        appScope.launch(Dispatchers.IO) {
            bestEffort(TAG, "sweep retired analysis cache") { File(filesDir, "analysis").deleteRecursively() }
        }
    }

    /**
     * Starts the disk load of the two prefs files [dev.geode.playback.PlaybackSession] reads while
     * it is constructed: the player prefs, which its `init` loads and applies (speed, pitch,
     * ReplayGain, shuffle, repeat, bit-perfect), and the audio FX state its `AudioFxController`
     * restores.
     *
     * That construction runs on Main the first time the UI or the playback service acquires the
     * session, and it has to stay synchronous: callers get the ExoPlayer back from
     * `acquireForUi`/`acquireForService` directly, already carrying the saved options. What makes
     * the read a stall is the parse, since a SharedPreferences getter blocks until its file is
     * loaded. Opening the files here, at process start and on IO, means the parse has normally
     * finished long before the session asks, so its read is a map lookup. The session reads the
     * same [prefsFiles] (nothing outside `data/` and this class builds another), so the load started
     * here is the one it gets.
     */
    private fun warmPlaybackPrefs() {
        appScope.launch(Dispatchers.IO) {
            prefsFiles.player
            prefsFiles.audioFx
        }
    }

    /**
     * Deletes render scratch files left behind by a render that never got to clean up after
     * itself — a crash, a foreground-service timeout, a low-memory kill, a force-stop.
     *
     * Every render deletes its own scratch on both the success and the failure path, so anything
     * still here belongs to a previous process. A whole-track AAC sidecar runs to tens of
     * megabytes and a loop reel to hundreds, and nothing else ever reclaims them, so without this
     * they accumulate for the life of the install.
     *
     * Safe to sweep wholesale because this runs while the container is being built, which
     * happens before anything can start a render in this process.
     */
    private fun sweepStaleRenderScratch(cacheDir: File) {
        appScope.launch(Dispatchers.IO) {
            bestEffort(TAG, "sweep stale render scratch") {
                cacheDir
                    .listFiles()
                    .orEmpty()
                    .filter { file -> file.isFile && isRenderScratch(file.name) }
                    .forEach { file -> file.delete() }
            }
        }
    }

    /**
     * Deletes MediaStore rows a render left `IS_PENDING` when its process died mid-write — a
     * crash, a force-stop, a low-memory kill of [dev.geode.export.ExportService] before it could
     * finish. [dev.geode.export.VideoExporter] and [dev.geode.export.LoopExtend] both insert with
     * `IS_PENDING` set and clear it on success or delete the row on a clean failure; nothing runs
     * `START_STICKY` or persists the run for a retry, so a row that is still pending once the app restarts belongs to nobody and never
     * finishes on its own. It would otherwise sit in Movies/BeatRealms or Pictures/BeatRealms forever, a
     * broken, unplayable, ever-growing invisible file.
     *
     * A short grace period (rather than sweeping every pending row on sight) keeps this from
     * racing a render that is genuinely in flight in this same process.
     */
    private fun sweepOrphanedPendingExports(context: Context) {
        appScope.launch(Dispatchers.IO) {
            bestEffort(TAG, "sweep orphaned pending exports") {
                val cutoffSeconds = (System.currentTimeMillis() - PENDING_EXPORT_GRACE_MS) / 1000
                deletePendingRowsOlderThan(context, MediaStore.Video.Media.EXTERNAL_CONTENT_URI, cutoffSeconds)
                deletePendingRowsOlderThan(context, MediaStore.Images.Media.EXTERNAL_CONTENT_URI, cutoffSeconds)
            }
        }
    }

    private fun deletePendingRowsOlderThan(
        context: Context,
        collection: Uri,
        cutoffSeconds: Long,
    ) {
        val resolver = context.contentResolver
        val selection =
            "${MediaStore.MediaColumns.OWNER_PACKAGE_NAME} = ? AND " +
                "${MediaStore.MediaColumns.IS_PENDING} = 1 AND " +
                "${MediaStore.MediaColumns.DATE_ADDED} < ? AND " +
                "(${MediaStore.MediaColumns.RELATIVE_PATH} LIKE ? OR ${MediaStore.MediaColumns.RELATIVE_PATH} LIKE ? OR " +
                "${MediaStore.MediaColumns.RELATIVE_PATH} LIKE ? OR ${MediaStore.MediaColumns.RELATIVE_PATH} LIKE ?)"
        val args =
            arrayOf(
                context.packageName,
                cutoffSeconds.toString(),
                "Movies/BeatRealms%",
                "Pictures/BeatRealms%",
                // Renders made before the rename (the old product name's folders).
                "Movies/MuzViz%",
                "Pictures/MuzViz%",
            )
        queryIncludingPending(context, collection, arrayOf(MediaStore.MediaColumns._ID), selection, args)?.use { cursor ->
            val idColumn = cursor.getColumnIndexOrThrow(MediaStore.MediaColumns._ID)
            while (cursor.moveToNext()) {
                val uri = ContentUris.withAppendedId(collection, cursor.getLong(idColumn))
                bestEffort(TAG, "resolver.delete(orphaned pending row)") { resolver.delete(uri, null, null) }
            }
        }
    }

    /**
     * A MediaStore query that can see pending rows. By default MediaProvider hides every
     * `IS_PENDING` row from a query, its owner's included, so the sweep's `IS_PENDING = 1` filter
     * matched nothing and orphans were never found. API 30 takes the opt-in as a query argument
     * (`QUERY_ARG_MATCH_PENDING = MATCH_INCLUDE`), which only the Bundle form of
     * `ContentResolver.query` carries, so the selection travels in the same Bundle.
     */
    private fun queryIncludingPending(
        context: Context,
        collection: Uri,
        projection: Array<String>,
        selection: String,
        args: Array<String>,
    ): Cursor? {
        val queryArgs =
            Bundle().apply {
                putString(ContentResolver.QUERY_ARG_SQL_SELECTION, selection)
                putStringArray(ContentResolver.QUERY_ARG_SQL_SELECTION_ARGS, args)
                putInt(MediaStore.QUERY_ARG_MATCH_PENDING, MediaStore.MATCH_INCLUDE)
            }
        return context.contentResolver.query(collection, projection, queryArgs, null)
    }

    private companion object {
        const val TAG = "GeodeContainer"

        /** A render still `IS_PENDING` this long after it was inserted is treated as orphaned. */
        const val PENDING_EXPORT_GRACE_MS = 5 * 60 * 1000L

        /**
         * Kept in step with AudioTranscoder, LoopRender and RecordFiles (`studio-record-`). `studio-`
         * also covers what the Studio project render left behind (`studio-scene-*`, `studio-*`) when
         * its process died before #227 removed it, which an upgraded install may still hold.
         */
        val RENDER_SCRATCH_PREFIXES = listOf("geode_aac_", "geode_loop_", "studio-")

        fun isRenderScratch(name: String): Boolean = RENDER_SCRATCH_PREFIXES.any { name.startsWith(it) }
    }
}

val Context.geodeContainer: GeodeContainer
    get() = (applicationContext as GeodeApp).container
