package dev.geode

import android.content.ContentUris
import android.content.Context
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.ui.SharedPrefsUserDataRepository
import dev.geode.ui.UserDataRepository
import dev.geode.util.bestEffort
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import java.io.File

class GeodeContainer(
    context: Context,
) {
    val appScope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    val prefsFiles = GeodePrefsFiles(context)

    val userData: UserDataRepository = SharedPrefsUserDataRepository(prefsFiles.general, appScope)

    init {
        sweepStaleRenderScratch(context.applicationContext.cacheDir)
        sweepOrphanedPendingExports(context.applicationContext)
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
     * finish. [dev.geode.export.VideoExporter], [dev.geode.export.StudioExporter] and
     * [dev.geode.export.LoopExtend] all insert with `IS_PENDING` set and clear it on success or
     * delete the row on a clean failure; nothing runs `START_STICKY` or persists the run for a
     * retry, so a row that is still pending once the app restarts belongs to nobody and never
     * finishes on its own. It would otherwise sit in Movies/Geode or Pictures/Geode forever, a
     * broken, unplayable, ever-growing invisible file.
     *
     * `IS_PENDING` only exists from API 29 (scoped storage), so there is nothing to sweep below
     * it. A short grace period (rather than sweeping every pending row on sight) keeps this from
     * racing a render that is genuinely in flight in this same process.
     */
    private fun sweepOrphanedPendingExports(context: Context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return
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
                "(${MediaStore.MediaColumns.RELATIVE_PATH} LIKE ? OR ${MediaStore.MediaColumns.RELATIVE_PATH} LIKE ?)"
        val args =
            arrayOf(
                context.packageName,
                cutoffSeconds.toString(),
                "Movies/Geode%",
                "Pictures/Geode%",
            )
        resolver.query(collection, arrayOf(MediaStore.MediaColumns._ID), selection, args, null)?.use { cursor ->
            val idColumn = cursor.getColumnIndexOrThrow(MediaStore.MediaColumns._ID)
            while (cursor.moveToNext()) {
                val uri = ContentUris.withAppendedId(collection, cursor.getLong(idColumn))
                bestEffort(TAG, "resolver.delete(orphaned pending row)") { resolver.delete(uri, null, null) }
            }
        }
    }

    private companion object {
        const val TAG = "GeodeContainer"

        /** A render still `IS_PENDING` this long after it was inserted is treated as orphaned. */
        const val PENDING_EXPORT_GRACE_MS = 5 * 60 * 1000L

        /** Kept in step with AudioTranscoder, LoopRender and StudioExporter. */
        val RENDER_SCRATCH_PREFIXES = listOf("geode_aac_", "geode_loop_", "studio-")

        fun isRenderScratch(name: String): Boolean = RENDER_SCRATCH_PREFIXES.any { name.startsWith(it) }
    }
}

val Context.geodeContainer: GeodeContainer
    get() = (applicationContext as GeodeApp).container
