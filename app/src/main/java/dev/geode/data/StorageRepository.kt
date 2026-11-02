package dev.geode.data

import android.content.ContentUris
import android.content.Context
import android.os.Build
import android.provider.MediaStore
import dagger.hilt.android.qualifiers.ApplicationContext
import dev.geode.RingLog
import dev.geode.di.IoDispatcher
import dev.geode.export.ExportRun
import dev.geode.util.bestEffort
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import java.io.File
import javax.inject.Inject
import javax.inject.Singleton

/** The three things Settings › Backup & about › Storage shows a size for and can clear. */
enum class StorageBucket {
    /**
     * Files a render writes into `cacheDir` while it runs (the AAC sidecar, loop reels, Studio's
     * intermediate MP4) and deletes when it ends. Anything here outside a render is a leftover.
     */
    RENDER_SCRATCH,

    /**
     * Everything else in `cacheDir`: compiled shader programs and the GL probe, share copies of
     * templates, the widget's cover art. All of it is rebuilt on demand; the first scenes after a
     * clear compile again, so they take a moment longer to appear. Style thumbnails are APK assets
     * and their decoded copies live in memory, so there is no thumbnail cache on disk to clear.
     */
    CACHE,

    /** Finished videos the app saved to Movies/MuzViz. Clearing deletes them from the phone. */
    RENDERS,
}

/** Bytes per [StorageBucket], plus how many finished renders [StorageBucket.RENDERS] counts. */
data class StorageUsage(
    val bytes: Map<StorageBucket, Long>,
    val renderCount: Int,
) {
    val totalBytes: Long get() = bytes.values.sum()

    operator fun get(bucket: StorageBucket): Long = bytes[bucket] ?: 0L
}

/**
 * What a [StorageRepository.clear] did. [skipped] counts what it had to leave: a render still
 * running holds its scratch, and a video saved by an earlier install of the app (or moved there
 * by another app) is not this install's to delete, so MediaStore refuses it.
 */
data class StorageClearResult(
    val freedBytes: Long,
    val skipped: Int,
)

/**
 * Sizes and clears the app's disk use for Settings › Storage. Data only: the screen (3B) shows
 * [usage], asks before a [clear] (always for [StorageBucket.RENDERS], which deletes the user's
 * videos), then reads [usage] again.
 *
 * [GeodeContainer][dev.geode.GeodeContainer] already sweeps leftover scratch and orphaned pending
 * rows at every start; this is the same cleanup on demand, plus the caches and the finished
 * renders the sweep never touches. Nothing here runs while [ExportRun] is rendering, since the
 * scratch and the pending row belong to that render.
 */
@Singleton
class StorageRepository
    @Inject
    constructor(
        @ApplicationContext private val context: Context,
        @IoDispatcher private val io: CoroutineDispatcher,
    ) {
        suspend fun usage(): StorageUsage =
            withContext(io) {
                val (scratch, cache) = cacheEntries().partition { isRenderScratch(it.name) }
                val renders = renders()
                StorageUsage(
                    bytes =
                        mapOf(
                            StorageBucket.RENDER_SCRATCH to scratch.sumOf(::sizeOf),
                            StorageBucket.CACHE to cache.sumOf(::sizeOf),
                            StorageBucket.RENDERS to renders.sumOf { it.bytes },
                        ),
                    renderCount = renders.size,
                )
            }

        suspend fun clear(bucket: StorageBucket): StorageClearResult =
            withContext(io) {
                if (ExportRun.running) {
                    StorageClearResult(freedBytes = 0, skipped = 1)
                } else {
                    when (bucket) {
                        StorageBucket.RENDER_SCRATCH -> deleteFiles(cacheEntries().filter { isRenderScratch(it.name) })
                        StorageBucket.CACHE -> deleteFiles(cacheEntries().filterNot { isRenderScratch(it.name) })
                        StorageBucket.RENDERS -> deleteRenders()
                    }.also { RingLog.note(TAG, "cleared $bucket: ${it.freedBytes} bytes, ${it.skipped} skipped") }
                }
            }

        private fun cacheEntries(): List<File> =
            context.cacheDir
                .listFiles()
                .orEmpty()
                .toList()

        private fun deleteFiles(files: List<File>): StorageClearResult {
            var freed = 0L
            var skipped = 0
            for (file in files) {
                val size = sizeOf(file)
                if (file.deleteRecursively()) freed += size else skipped++
            }
            return StorageClearResult(freed, skipped)
        }

        private fun sizeOf(file: File): Long = file.walkBottomUp().filter { it.isFile }.sumOf { it.length() }

        private data class Render(
            val id: Long,
            val bytes: Long,
        )

        /**
         * Finished videos in Movies/MuzViz. Pending rows are hidden from a query by default, so a
         * render in flight is never counted. From API 29 the query only sees rows this install may
         * read without a media permission, which are its own; below it the folder match is by path.
         */
        private fun renders(): List<Render> {
            val out = mutableListOf<Render>()
            val (selection, args) =
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    "${MediaStore.Video.Media.RELATIVE_PATH} LIKE ?" to arrayOf("$RENDERS_PATH%")
                } else {
                    "${MediaStore.Video.Media.DATA} LIKE ?" to arrayOf("%/$RENDERS_PATH/%")
                }
            bestEffort(TAG, "query renders") {
                context.contentResolver
                    .query(
                        MediaStore.Video.Media.EXTERNAL_CONTENT_URI,
                        arrayOf(MediaStore.Video.Media._ID, MediaStore.Video.Media.SIZE),
                        selection,
                        args,
                        null,
                    )?.use { c ->
                        val id = c.getColumnIndexOrThrow(MediaStore.Video.Media._ID)
                        val size = c.getColumnIndexOrThrow(MediaStore.Video.Media.SIZE)
                        while (c.moveToNext()) out += Render(c.getLong(id), c.getLong(size))
                    }
            }
            return out
        }

        /**
         * Deletes each finished render by id. A row an earlier install saved throws a
         * (Recoverable)SecurityException without the user's say-so; it is counted as skipped
         * rather than asked about, so the screen can tell the user to delete those in Files.
         * Studio projects that list a deleted render keep its entry; My videos shows it as missing.
         */
        private fun deleteRenders(): StorageClearResult {
            var freed = 0L
            var skipped = 0
            for (render in renders()) {
                val uri = ContentUris.withAppendedId(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, render.id)
                val deleted =
                    try {
                        context.contentResolver.delete(uri, null, null) > 0
                    } catch (e: SecurityException) {
                        RingLog.note(TAG, "render ${render.id} not ours to delete", e)
                        false
                    }
                if (deleted) freed += render.bytes else skipped++
            }
            return StorageClearResult(freed, skipped)
        }

        private companion object {
            const val TAG = "Storage"
            const val RENDERS_PATH = "Movies/MuzViz"

            /** Kept in step with `GeodeContainer.RENDER_SCRATCH_PREFIXES` (AudioTranscoder, LoopRender, StudioExporter). */
            val RENDER_SCRATCH_PREFIXES = listOf("geode_aac_", "geode_loop_", "studio-")

            fun isRenderScratch(name: String): Boolean = RENDER_SCRATCH_PREFIXES.any { name.startsWith(it) }
        }
    }
