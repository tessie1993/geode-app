package dev.geode.data.backup

import dev.geode.engine.bridge.BackupNative

// The lists that cross to core/backup and back (core/api/geode_api.h, `geode_backup_*`): strings read in order, a
// count as decimal text, a flag as "1" or "0". Nothing here decides anything about a backup.

/** An answer from native, read front to back. */
internal class BackupWire private constructor(
    private val items: Array<String>,
) {
    private var at = 0

    fun str(): String = items.getOrNull(at++) ?: fail(BackupProblem.IO_FAILED, "native answer ended early")

    fun int(): Int = str().toInt()

    fun long(): Long = str().toLong()

    fun flag(): Boolean = str() == "1"

    fun strs(): List<String> = List(int()) { str() }

    /** The problem and message that open most answers; raises it as a [BackupException] unless there was none. */
    fun failure() {
        val problem = int()
        val message = str()
        if (problem != 0) throw BackupException(problemOf(problem), message)
    }

    /** A preference value as the plan hands it back: its key and value (null for a type no `putX` takes). */
    fun prefValue(): Pair<String, Any?> {
        val key = str()
        val type = str()
        val text = str()
        val items = strs()
        val value: Any? =
            when (type) {
                "s" -> text
                "i" -> text.toInt()
                "l" -> text.toLong()
                "f" -> text.toDouble().toFloat()
                "b" -> text == "1"
                "t" -> items.toSet()
                else -> null
            }
        return key to value
    }

    companion object {
        fun of(answer: Array<String>?): BackupWire = BackupWire(answer ?: fail(BackupProblem.IO_FAILED, "native gave no answer"))

        /** Core/backup's `Problem` number (1 to 6) as the shell's enum. */
        fun problemOf(number: Int): BackupProblem = BackupProblem.entries.getOrElse(number - 1) { BackupProblem.IO_FAILED }
    }
}

/** A preference as the shell hands it to a plan: key, type, text, then its items (a string set's). */
internal fun MutableList<String>.addPref(
    key: String,
    value: Any?,
) {
    add(key)
    when (value) {
        is String -> addAll(listOf("s", value, "0"))
        is Int -> addAll(listOf("i", value.toString(), "0"))
        is Long -> addAll(listOf("l", value.toString(), "0"))
        is Float -> addAll(listOf("f", value.toDouble().toString(), "0"))
        is Boolean -> addAll(listOf("b", if (value) "1" else "0", "0"))
        is Set<*> -> {
            val strings = value.filterIsInstance<String>()
            addAll(listOf("t", "", strings.size.toString()))
            addAll(strings)
        }
        else -> addAll(listOf("u", "", "0"))
    }
}

/** What native says a backup is laid out as (`geode_backup_layout`). */
internal class BackupLayout(
    val manifestPath: String,
    val maxManifestBytes: Long,
    val maxArchiveBytes: Long,
    val maxEntries: Int,
    val sections: List<Section>,
) {
    class Section(
        val id: String,
        /** SharedPreferences file name to its path in the archive. */
        val prefs: List<Pair<String, String>>,
        /** filesDir-relative directories, then files, an export walks. */
        val roots: List<String>,
    )

    /** Every prefs file a backup holds part of, once. */
    val prefsFiles: List<String> get() = sections.flatMap { it.prefs.map { (file, _) -> file } }.distinct()

    companion object {
        fun read(): BackupLayout {
            val wire = BackupWire.of(BackupNative.layout())
            val manifestPath = wire.str()
            val maxManifest = wire.long()
            val maxArchive = wire.long()
            val maxEntries = wire.int()
            val sections =
                List(wire.int()) {
                    val id = wire.str()
                    val prefs = List(wire.int()) { wire.str() to wire.str() }
                    Section(id, prefs, wire.strs())
                }
            return BackupLayout(manifestPath, maxManifest, maxArchive, maxEntries, sections)
        }
    }
}
