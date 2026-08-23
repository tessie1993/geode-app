package dev.geode.data

import android.content.Context
import androidx.annotation.WorkerThread
import java.io.File

data class TakeInfo(
    val name: String,
    val durationMs: Long,
    val eventCount: Int,
    val trackUri: String?,
    val sizeBytes: Long,
)

class TakeStore(
    context: Context,
) {
    private val dir = File(context.filesDir, "takes").apply { mkdirs() }

    init {
        migrateLegacyFileNames()
    }

    private fun migrateLegacyFileNames() {
        dir
            .listFiles { f -> f.isFile && f.extension == "json" }
            .orEmpty()
            .forEach { f ->
                val name = runCatching { PerformanceTake.Timeline(f.readText()).name }.getOrNull() ?: return@forEach
                val stem = PresetStore.safeFileName(name)
                if (f.nameWithoutExtension == stem) return@forEach
                val target = File(dir, "$stem.json")
                if (!target.exists()) f.renameTo(target)
            }
    }

    private fun fileOf(name: String): File = File(dir, PresetStore.safeFileName(name) + ".json")

    @WorkerThread
    fun list(): List<TakeInfo> =
        dir
            .listFiles { f -> f.isFile && f.extension == "json" }
            .orEmpty()
            .sortedByDescending { it.lastModified() }
            .mapNotNull { f ->
                runCatching {
                    val t = PerformanceTake.Timeline(f.readText())
                    TakeInfo(t.name, t.durationMs, t.eventCount, t.trackUri, f.length())
                }.getOrNull()
            }

    @WorkerThread
    fun load(name: String): PerformanceTake.Timeline? =
        runCatching { PerformanceTake.Timeline(fileOf(name).readText()) }
            .onFailure { dev.geode.RingLog.note("TakeStore", "take failed to load: $name", it) }
            .getOrNull()

    @WorkerThread
    fun save(
        name: String,
        json: String,
    ): String {
        var candidate = name
        var n = 2
        while (fileOf(candidate).exists()) {
            candidate = "$name $n"
            n++
        }
        val body =
            if (candidate == name) {
                json
            } else {
                runCatching {
                    org.json
                        .JSONObject(json)
                        .put("name", candidate)
                        .toString()
                }.getOrDefault(json)
            }
        AtomicWrite.text(fileOf(candidate), body)
        return candidate
    }

    @WorkerThread
    fun delete(name: String) {
        fileOf(name).delete()
    }

    fun rename(
        from: String,
        to: String,
    ): Boolean {
        val target = to.trim()
        val src = fileOf(from)
        if (!src.isFile || target.isEmpty()) return false
        val dest = fileOf(target)
        if (dest.exists()) return false
        val updated =
            runCatching {
                org.json
                    .JSONObject(src.readText())
                    .put("name", target)
                    .toString()
            }.getOrNull() ?: return false
        if (!AtomicWrite.text(dest, updated)) return false
        src.delete()
        return true
    }
}
