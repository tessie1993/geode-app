package dev.geode

import android.content.ContentResolver
import android.content.ContentUris
import android.content.Context
import android.content.Intent
import android.database.Cursor
import android.net.Uri
import android.os.Bundle
import android.os.Process
import android.os.SystemClock
import android.provider.DocumentsContract
import android.provider.MediaStore
import dev.geode.data.AppStateCodec
import dev.geode.data.AppStateRepository
import dev.geode.data.AtomicWrite
import dev.geode.data.AutoDirectorCodec
import dev.geode.data.AutoDirectorStore
import dev.geode.data.BehaviourCodec
import dev.geode.data.BehaviourRepository
import dev.geode.data.CacheSweeper
import dev.geode.data.CustomizeGroupsCodec
import dev.geode.data.CustomizeGroupsStore
import dev.geode.data.GeneralSettingsCodec
import dev.geode.data.GeneralSettingsRepository
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.KeepSetStore
import dev.geode.data.LibrarySettingsCodec
import dev.geode.data.LibrarySettingsRepository
import dev.geode.data.LookStore
import dev.geode.data.MacroBankCodec
import dev.geode.data.MacroStore
import dev.geode.data.MediaStoreSource
import dev.geode.data.OpenedAudioSource
import dev.geode.data.PaletteStackCodec
import dev.geode.data.PaletteStackStore
import dev.geode.data.PerformanceStore
import dev.geode.data.SoundInputSettingsCodec
import dev.geode.data.SoundInputSettingsRepository
import dev.geode.data.StyleShelfCodec
import dev.geode.data.StyleShelfStore
import dev.geode.data.UnfinishedOutputs
import dev.geode.data.VisualsSettingsCodec
import dev.geode.data.VisualsSettingsRepository
import dev.geode.data.backup.BackupRepository
import dev.geode.data.prefs.PrefsSettingsRepository
import dev.geode.data.releaseGrant
import dev.geode.diag.ExitKind
import dev.geode.diag.ExitReasons
import dev.geode.engine.bridge.GeodeNative
import dev.geode.util.bestEffort
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

class GeodeContainer(
    context: Context,
) {
    val appScope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    /**
     * The last process death a JVM handler cannot see (native or driver crash, ANR, a kill while in use), written
     * to the crash file once ([ExitReasons]). Started with the process so the wallpaper or dream reports too; the
     * crash dialog awaits it before it reads the file.
     */
    val lastExit: Deferred<ExitKind?> =
        appScope.async(Dispatchers.IO) {
            runCatching { ExitReasons(context.applicationContext).record() }
                .onFailure { RingLog.note(TAG, "last exit reason unreadable", it) }
                .getOrNull()
        }

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

    val paletteStack = PaletteStackStore(PrefsSettingsRepository(prefsFiles.general, PaletteStackCodec(), appScope), appScope)

    /** The saved look: native gets the saved Customize › Layers line once read, and keeps the layer (core/viz/LayerState). */
    val looks =
        LookStore(prefsFiles, context.applicationContext.contentResolver).also { appScope.launch(Dispatchers.IO) { it.pushLayer() } }

    /** Customize's keep set, read into native on first use (the director's start, on IO); a Keep tap goes through it. */
    val keepSet = KeepSetStore(prefsFiles.modulation)

    val autoDirector =
        AutoDirectorStore(PrefsSettingsRepository(prefsFiles.general, AutoDirectorCodec, appScope), appScope) {
            withContext(Dispatchers.IO) { keepSet.start() }
        }

    /** The Styles grid's favourites and recents. */
    val styleShelf = StyleShelfStore(PrefsSettingsRepository(prefsFiles.viz, StyleShelfCodec, appScope), appScope)

    /** Customize's tab and the groups the user opened. */
    val customizeGroups = CustomizeGroupsStore(PrefsSettingsRepository(prefsFiles.general, CustomizeGroupsCodec, appScope), appScope)

    /** Settings › Performance; the wallpaper and dream read the same prefs through their own store. */
    val performance: PerformanceStore by lazy { PerformanceStore(prefsFiles.general, context.applicationContext) }

    /** The `ContentResolver` source the screens and controllers reach consent requests, grants and picked documents through. */
    val mediaStore: MediaStoreSource by lazy {
        MediaStoreSource(context.applicationContext, Dispatchers.IO)
    }

    /** The checks an audio file another app opened passes before it plays ("Open with"). */
    val openedAudio: OpenedAudioSource by lazy { OpenedAudioSource(context.applicationContext, Dispatchers.IO) }

    init {
        warmPlaybackPrefs()
        sweepStaleRenderScratch(context.applicationContext.cacheDir)
        sweepOldShareCopies(context.applicationContext.cacheDir)
        sweepOrphanedPendingExports(context.applicationContext)
        sweepRetiredAnalysisCache(context.applicationContext.filesDir)
        discardStaleRestore(context.applicationContext)
        deleteUnfinishedOutputs(context.applicationContext)
        sweepStaleTemps(context.applicationContext)
    }

    /**
     * Deletes the `.tmp` files writes killed between their temp and their rename left in the app's own
     * folders ([AtomicWrite.staleTemps]; native writes the same way). Only those last written before
     * this process started: a write running now is never touched.
     */
    private fun sweepStaleTemps(context: Context) {
        val startedAt = System.currentTimeMillis() - (SystemClock.uptimeMillis() - Process.getStartUptimeMillis())
        appScope.launch(Dispatchers.IO) {
            bestEffort(TAG, "sweep stale temps") {
                listOf(context.filesDir, context.noBackupFilesDir).forEach { dir ->
                    AtomicWrite.staleTemps(dir, startedAt).forEach { it.delete() }
                }
            }
        }
    }

    /**
     * Deletes the files a render or a recording was writing into a place the user chose when its
     * process died ([UnfinishedOutputs]): each is broken, and nothing else would ever remove it from
     * the user's folder. A render's own grant on its picked file goes with it.
     */
    private fun deleteUnfinishedOutputs(context: Context) {
        appScope.launch(Dispatchers.IO) {
            val unfinished = UnfinishedOutputs(context)
            unfinished.leftOver().forEach { raw ->
                val uri = Uri.parse(raw)
                bestEffort(TAG, "delete an unfinished render") { DocumentsContract.deleteDocument(context.contentResolver, uri) }
                context.contentResolver.releaseGrant(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
                unfinished.end(raw)
            }
        }
    }

    /**
     * Drops a backup restore's staging (`no_backup/backup-restore`, up to the archive cap) that a
     * previous process left: a restore is only ever finished by the process that inspected it, so
     * staging found at start belongs to nobody. A swap the process did not outlive left the user's
     * originals in its `aside` folder; [BackupRepository.discard] puts back every one with no file at
     * its place before the folder goes, as the next inspect or restore would have.
     */
    private fun discardStaleRestore(context: Context) {
        appScope.launch(Dispatchers.IO) {
            bestEffort(TAG, "discard stale restore staging") { BackupRepository(context, Dispatchers.IO).discard() }
        }
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
     *
     * The UI's `PlayerSession`, built on Main before the first frame, reads the library prefs the
     * same way (`MusicLibraryRepository`), and billing, built with it, the Pro cache
     * (`PrefsEntitlementStore`).
     *
     * It also loads libgeode (`GeodeNative.ensureLoaded`, which the first `*Native` touch would otherwise
     * run on Main) and opens the `general` file, so the settings repositories' `current()` is a lookup.
     */
    private fun warmPlaybackPrefs() {
        appScope.launch(Dispatchers.IO) {
            GeodeNative.ensureLoaded()
            prefsFiles.general
            prefsFiles.player
            prefsFiles.audioFx
            prefsFiles.library
            prefsFiles.pro
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
                CacheSweeper.withPrefix(cacheDir, RENDER_SCRATCH_PREFIXES).forEach { file -> file.delete() }
            }
        }
    }

    /**
     * Trims `cacheDir/share`, the copies handed to a share sheet: each share clears the earlier ones,
     * but the last copy stayed until the next share. A day is long enough for the other app to have
     * read it; at most [SHARE_COPIES_KEPT] stay whatever their age.
     */
    private fun sweepOldShareCopies(cacheDir: File) {
        appScope.launch(Dispatchers.IO) {
            bestEffort(TAG, "sweep old share copies") {
                val shares = File(cacheDir, "share")
                val cutoff = System.currentTimeMillis() - SHARE_COPY_MAX_AGE_MS
                (CacheSweeper.olderThan(shares, cutoff) + CacheSweeper.beyondNewest(shares, SHARE_COPIES_KEPT))
                    .distinct()
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
         * Kept in step with AudioTranscoder, LoopRender, RecordFiles (`studio-record-`) and the backup
         * export's whole zip (`BackupRepository.export`, `backup*.zip`). `studio-`
         * also covers what the Studio project render left behind (`studio-scene-*`, `studio-*`) when
         * its process died before #227 removed it, which an upgraded install may still hold.
         */
        val RENDER_SCRATCH_PREFIXES = listOf("geode_aac_", "geode_loop_", "studio-", "backup")

        const val SHARE_COPY_MAX_AGE_MS = 24 * 60 * 60 * 1000L
        const val SHARE_COPIES_KEPT = 8
    }
}

val Context.geodeContainer: GeodeContainer
    get() = (applicationContext as GeodeApp).container
