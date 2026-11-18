package dev.geode.data

import androidx.annotation.WorkerThread
import java.io.File

/** Removing `.milk` presets from `filesDir/milk`, the folder the MilkDrop list reads. */
object ImportedMilk {
    /**
     * Deletes the preset at [path] when it is a `.milk` directly in [milkDir], and nothing else: the
     * path comes from the list, so a stale or foreign one must never reach another file. False when
     * it was not one or would not go.
     */
    @WorkerThread
    fun remove(
        milkDir: File,
        path: String,
    ): Boolean {
        val file = File(path)
        val inDir = runCatching { file.canonicalFile.parentFile == milkDir.canonicalFile }.getOrDefault(false)
        return inDir && file.extension == "milk" && file.isFile && file.delete()
    }
}
