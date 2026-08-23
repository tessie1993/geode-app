package dev.geode.data

import dev.geode.util.bestEffort
import java.io.File
import java.io.FileOutputStream
import java.io.OutputStream

object AtomicWrite {
    const val TEMP_SUFFIX = ".tmp"

    const val CORRUPT_SUFFIX = ".corrupt"

    private val locks = java.util.concurrent.ConcurrentHashMap<String, Any>()

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
        synchronized(locks.computeIfAbsent(file.absolutePath) { Any() }) {
            val ok =
                runCatching {
                    FileOutputStream(temp).use { out ->
                        body(out)
                        out.flush()
                        out.fd.sync()
                    }
                    temp.renameTo(file)
                }.getOrDefault(false)
            if (!ok) {
                bestEffort(TAG, "temp.delete()") { temp.delete() }
                dev.geode.RingLog.note("AtomicWrite", "write failed, previous content kept: ${file.name}")
            }
            return ok
        }
    }

    fun quarantine(file: File): Boolean =
        runCatching { file.renameTo(File(file.absolutePath + CORRUPT_SUFFIX)) }
            .getOrDefault(false)
            .also { moved -> if (moved) dev.geode.RingLog.note("AtomicWrite", "unparseable content quarantined: ${file.name}") }
}

private const val TAG = "AtomicWrite"
