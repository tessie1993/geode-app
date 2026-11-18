package dev.geode.data

import dev.geode.util.bestEffort
import java.io.File
import java.io.FileOutputStream
import java.io.OutputStream

object AtomicWrite {
    const val TEMP_SUFFIX = ".tmp"

    const val CORRUPT_SUFFIX = ".corrupt"

    /**
     * Writes to one path never overlap; a fixed set of locks picked by the path's hash, so the set
     * does not grow by one for every path ever written, as a map of one lock per path did.
     */
    private val locks = Array(LOCK_STRIPES) { Any() }

    internal fun lockFor(path: String): Any = locks[Math.floorMod(path.hashCode(), LOCK_STRIPES)]

    fun text(
        file: File,
        text: String,
    ): Boolean = stream(file) { out -> out.write(text.toByteArray(Charsets.UTF_8)) }

    fun stream(
        file: File,
        body: (OutputStream) -> Unit,
    ): Boolean {
        val parent = file.parentFile
        if (parent != null && !parent.isDirectory) {
            parent.mkdirs()
            if (!parent.isDirectory) return false
        }
        val temp = File(file.absolutePath + TEMP_SUFFIX)
        synchronized(lockFor(file.absolutePath)) {
            val ok =
                runCatching {
                    FileOutputStream(temp).use { out ->
                        body(out)
                        out.flush()
                        out.fd.sync()
                    }
                    temp.renameTo(file)
                }.onFailure { dev.geode.RingLog.note(TAG, "write threw: ${file.name}", it) }.getOrDefault(false)
            if (!ok) {
                bestEffort(TAG, "temp.delete()") { temp.delete() }
                dev.geode.RingLog.note("AtomicWrite", "write failed, previous content kept: ${file.name}")
            }
            return ok
        }
    }

    /**
     * The [TEMP_SUFFIX] files under [root] last written before [before]: what a write killed between
     * its temp and its rename left, since one still running in this process started after [before].
     */
    fun staleTemps(
        root: File,
        before: Long,
    ): List<File> = root.walkTopDown().filter { it.isFile && it.name.endsWith(TEMP_SUFFIX) && it.lastModified() < before }.toList()

    fun quarantine(file: File): Boolean =
        runCatching { file.renameTo(File(file.absolutePath + CORRUPT_SUFFIX)) }
            .getOrDefault(false)
            .also { moved -> if (moved) dev.geode.RingLog.note("AtomicWrite", "unparseable content quarantined: ${file.name}") }
}

private const val TAG = "AtomicWrite"

private const val LOCK_STRIPES = 64
