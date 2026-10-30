package dev.geode.data

import android.content.SharedPreferences
import dev.geode.RingLog
import dev.geode.render.scene.MacroSet
import dev.geode.render.scene.MacroSetCodec
import dev.geode.render.scene.StandardMacros
import dev.geode.util.bestEffort
import java.io.File

/** One saved macro set in the folder: where it lives, for load, delete and share. */
data class SavedMacroSet(
    val folder: String,
    val name: String,
    val file: File,
)

/**
 * The macro sets: the active one (in prefs, so the knobs come back where they were) and the
 * macro-preset folder (`filesDir/macro_sets/<folder>/<name>.mvzmacro`, text in
 * [MacroSetCodec]'s format). The active set is global, not per preset: switching style or
 * loading a preset keeps the same knobs.
 *
 * Small and synchronous on purpose; callers run it on the session's store dispatcher.
 */
class MacroSetStore(
    filesDir: File,
    private val prefs: SharedPreferences,
) {
    private val root = File(filesDir, ROOT_DIR)

    fun loadActive(): MacroSet =
        prefs.getString(KEY_ACTIVE, null)?.let(MacroSetCodec::decode)?.takeIf { it.macros.isNotEmpty() }
            ?: StandardMacros.SET

    fun saveActive(set: MacroSet) {
        prefs.edit().putString(KEY_ACTIVE, MacroSetCodec.encode(set)).apply()
    }

    /** Every saved set, folder by folder; the root folder is "". */
    fun list(): List<SavedMacroSet> {
        if (!root.isDirectory) return emptyList()
        return root
            .walkTopDown()
            .filter { it.isFile && it.extension == MacroSetCodec.EXTENSION }
            .map { f ->
                val folder =
                    f.parentFile
                        ?.relativeTo(root)
                        ?.path
                        .orEmpty()
                        .replace(File.separatorChar, '/')
                SavedMacroSet(folder, f.nameWithoutExtension, f)
            }.sortedWith(compareBy({ it.folder.lowercase() }, { it.name.lowercase() }))
            .toList()
    }

    /** Saves [set] under its name in [folder], replacing a set of the same name there. */
    fun save(
        set: MacroSet,
        folder: String,
    ): SavedMacroSet? =
        attempt("save macro set") {
            val dir = File(root, sanitizeFolder(folder)).apply { mkdirs() }
            val file = File(dir, "${sanitize(set.name)}.${MacroSetCodec.EXTENSION}")
            file.writeText(MacroSetCodec.encode(set))
            SavedMacroSet(folder, file.nameWithoutExtension, file)
        }

    fun load(saved: SavedMacroSet): MacroSet? = attempt("load macro set") { MacroSetCodec.decode(saved.file.readText()) }

    fun delete(saved: SavedMacroSet) {
        bestEffort(TAG, "delete macro set") { saved.file.delete() }
    }

    // A failed read or write is reported to the caller as null (the UI says so) and kept in RingLog.
    private inline fun <T> attempt(
        what: String,
        block: () -> T,
    ): T? = runCatching(block).onFailure { RingLog.note(TAG, "$what failed", it) }.getOrNull()

    private fun sanitize(name: String): String =
        name
            .trim()
            .replace(UNSAFE, "_")
            .ifBlank { "Macro set" }
            .take(MAX_NAME)

    private fun sanitizeFolder(folder: String): String =
        folder
            .split('/')
            .map { it.trim() }
            .filter { it.isNotEmpty() && it != "." && it != ".." }
            .joinToString("/") { it.replace(UNSAFE, "_").take(MAX_NAME) }

    private companion object {
        const val TAG = "MacroSetStore"
        const val ROOT_DIR = "macro_sets"
        const val KEY_ACTIVE = "macro_set_active"
        const val MAX_NAME = 60
        val UNSAFE = Regex("[\\\\/:*?\"<>|\\p{Cntrl}]")
    }
}
