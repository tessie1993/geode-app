package dev.geode.data

import android.content.Context
import dev.geode.GeodeContainer
import java.io.File

/**
 * Settings › Backup & about › Storage: what the app keeps on the phone, and a Clear for the part
 * that is safe to lose. Only [cacheBytes] is cleared. Everything else is the user's work or
 * library and has its own place to delete it.
 *
 * Rendered videos are not counted: they live in the user's Movies folder or a folder they picked,
 * not in the app's storage.
 */
data class StorageUsage(
    /** Artwork, thumbnails and other caches, rebuilt on demand. */
    val cacheBytes: Long,
    /** Studio projects (with their takes) and templates. */
    val projectsBytes: Long,
    /** Saved looks, palettes and imported MilkDrop presets and textures. */
    val presetsBytes: Long,
    /** The library index, playlists and listening history. */
    val libraryBytes: Long,
) {
    val totalBytes: Long get() = cacheBytes + projectsBytes + presetsBytes + libraryBytes

    companion object {
        /** Walks the app's folders: call it off the main thread. */
        fun measure(context: Context): StorageUsage {
            val files = context.filesDir
            return StorageUsage(
                cacheBytes = cacheDirs(context).sumOf { dir -> clearable(dir).sumOf { it.sizeOnDisk() } },
                projectsBytes = sizeOf(files, "projects", "project-templates", "editor", "templates", "takes"),
                presetsBytes = sizeOf(files, "presets", "palettes", "milk"),
                libraryBytes = sizeOf(files, "music-playlists", "smart-playlists", "library.json", "history.json"),
            )
        }

        /**
         * Empties the caches and returns the bytes freed. Render scratch is left alone: a render may
         * be writing it right now, and the next launch sweeps any that is left over.
         */
        fun clearCache(context: Context): Long =
            cacheDirs(context).sumOf { dir ->
                clearable(dir).sumOf { file ->
                    val size = file.sizeOnDisk()
                    if (file.deleteRecursively()) size else 0L
                }
            }

        private fun cacheDirs(context: Context): List<File> = listOfNotNull(context.cacheDir, context.externalCacheDir)

        private fun clearable(dir: File): List<File> = dir.listFiles().orEmpty().filterNot { GeodeContainer.isRenderScratch(it.name) }

        private fun sizeOf(
            root: File,
            vararg names: String,
        ): Long = names.sumOf { File(root, it).sizeOnDisk() }

        private fun File.sizeOnDisk(): Long = if (isFile) length() else walkTopDown().filter { it.isFile }.sumOf { it.length() }
    }
}
