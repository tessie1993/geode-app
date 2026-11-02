package dev.geode.data

import android.content.Context
import dagger.hilt.android.qualifiers.ApplicationContext
import dev.geode.RingLog
import dev.geode.di.IoDispatcher
import dev.geode.export.ExportRun
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import java.io.File
import javax.inject.Inject
import javax.inject.Singleton

/**
 * The caches Settings › Backup & about › Storage shows a size for and can empty. The owner's rule:
 * Storage empties caches, nothing else. The user's videos, projects and library are never touched.
 */
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
}

/** Bytes per [StorageBucket]. */
data class StorageUsage(
    val bytes: Map<StorageBucket, Long>,
) {
    val totalBytes: Long get() = bytes.values.sum()

    operator fun get(bucket: StorageBucket): Long = bytes[bucket] ?: 0L
}

/** What a [StorageRepository.clear] did. [skipped] counts what it had to leave: a render still running holds its scratch. */
data class StorageClearResult(
    val freedBytes: Long,
    val skipped: Int,
)

/**
 * Sizes and empties the app's caches for Settings › Storage. Data only: the screen (3B) shows
 * [usage], runs a [clear], then reads [usage] again.
 *
 * [GeodeContainer][dev.geode.GeodeContainer] already sweeps leftover scratch at every start; this
 * is the same cleanup on demand, plus the caches the sweep never touches. Nothing here runs while
 * [ExportRun] is rendering, since the scratch belongs to that render.
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
                StorageUsage(
                    bytes =
                        mapOf(
                            StorageBucket.RENDER_SCRATCH to scratch.sumOf(::sizeOf),
                            StorageBucket.CACHE to cache.sumOf(::sizeOf),
                        ),
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

        private companion object {
            const val TAG = "Storage"

            /** Kept in step with `GeodeContainer.RENDER_SCRATCH_PREFIXES` (AudioTranscoder, LoopRender, StudioExporter). */
            val RENDER_SCRATCH_PREFIXES = listOf("geode_aac_", "geode_loop_", "studio-")

            fun isRenderScratch(name: String): Boolean = RENDER_SCRATCH_PREFIXES.any { name.startsWith(it) }
        }
    }
