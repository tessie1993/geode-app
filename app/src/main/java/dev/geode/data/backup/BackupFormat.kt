package dev.geode.data.backup

import dev.geode.data.GeodePrefsFiles
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject
import java.time.LocalDate
import java.util.Locale

// The rules of a MuzViz backup archive: what goes in, where it lives, what a restore will
// accept. Pure Kotlin on purpose (org.json and java.* only): every check that decides whether
// a file from outside the app may touch app data is here, where a JVM test can reach it.

/** Why a backup could not be written or read. The UI names each one; the message is for the log. */
enum class BackupProblem {
    NOT_A_BACKUP,
    NEWER_VERSION,
    UNSAFE_PATH,
    DAMAGED,
    TOO_LARGE,
    IO_FAILED,
}

class BackupException(
    val problem: BackupProblem,
    message: String,
    cause: Throwable? = null,
) : Exception(message, cause)

internal fun fail(
    problem: BackupProblem,
    message: String,
): Nothing = throw BackupException(problem, message)

/**
 * What the user can choose to restore. Each section owns whole prefs files and whole
 * filesDir-relative directories or files, so restoring one never half-replaces another.
 *
 * The LFO and modulation slots live in the `geode-mod` prefs file and favourites in
 * `geode-favourites`, so those files ride with the section the user thinks of them in rather
 * than with Settings.
 */
enum class BackupSection(
    val id: String,
    val prefsFiles: List<String>,
    val dirs: List<String>,
    val files: List<String>,
) {
    SETTINGS(
        "settings",
        listOf(
            GeodePrefsFiles.GENERAL,
            GeodePrefsFiles.VIZ,
            GeodePrefsFiles.PLAYER,
            GeodePrefsFiles.AUDIO_FX,
            GeodePrefsFiles.LIBRARY,
            GeodePrefsFiles.BACKGROUND,
        ),
        emptyList(),
        emptyList(),
    ),
    CREATIVE(
        "creative",
        listOf(GeodePrefsFiles.MODULATION),
        listOf("presets", "palettes", "templates", "editor"),
        emptyList(),
    ),
    MUSIC(
        "music",
        listOf(GeodePrefsFiles.FAVOURITES),
        listOf("music-playlists", "smart-playlists"),
        listOf("library.json", "history.json"),
    ),

    /** User-imported Milk presets, textures and texture choices. `milk-builtin` is rebuilt from assets. */
    MILK("milk", emptyList(), listOf("milk"), emptyList()),
    ;

    companion object {
        fun fromId(id: String): BackupSection? = entries.firstOrNull { it.id == id }
    }
}

/** Caps a restore checks while it reads, so a hostile or broken archive cannot fill the disk. */
data class BackupLimits(
    val maxTotalBytes: Long = 512L * 1024 * 1024,
    val maxEntries: Int = 20_000,
)

object BackupFormat {
    const val FORMAT_ID = "muzviz-backup"

    /** Bump with a new step in [BackupMigrations] that turns the previous version into this one. */
    const val CURRENT_VERSION = 1

    const val MANIFEST_PATH = "manifest.json"

    const val MAX_MANIFEST_BYTES = 8 * 1024 * 1024

    private const val PREFS_DIR = "prefs/"
    private const val PREFS_SUFFIX = ".json"
    private const val MAX_PATH_LENGTH = 512

    /**
     * Keys that describe this phone or this install rather than the user's choices. They are
     * left out of an export, and a restore neither writes nor removes them.
     *
     * - "Seen it" flags (safety notice, first-run setup, tutorial): a new phone should show
     *   the photosensitivity notice and the tour again; the user's answers themselves (reduced
     *   motion, intent, "offer the tour on a fresh install") are restored.
     * - Folder and image URIs picked through the system picker: their read grant belongs to
     *   this install and does not survive a reinstall, so a restored URI points at nothing.
     *
     * There is no crash or permission state in prefs: the crash report is `crash-latest.txt`,
     * which is never in a backup, and runtime permissions are the system's.
     */
    val deviceLocalKeys: Map<String, Set<String>> =
        mapOf(
            GeodePrefsFiles.GENERAL to
                setOf("gui_safety_acknowledged", "gui_setup_done", "gui_tutorial_seen", "preset_mirror_uri"),
            GeodePrefsFiles.LIBRARY to setOf("roots"),
            GeodePrefsFiles.BACKGROUND to setOf("background_uri"),
        )

    /**
     * Subtrees inside a section directory that are caches: `milk/generated` is regenerated
     * textures and `.links` is rebuilt by `MilkTextureLinks.relinkAll`. Never exported; a
     * restore keeps the phone's own copy.
     */
    val excludedSubtrees: List<String> = listOf("milk/generated", "milk/textures/.links")

    /** AtomicWrite's in-flight and quarantined files. */
    private val excludedSuffixes = listOf(".tmp", ".corrupt")

    fun prefsPath(prefsFile: String): String = PREFS_DIR + prefsFile + PREFS_SUFFIX

    fun isPortableKey(
        prefsFile: String,
        key: String,
    ): Boolean = key !in deviceLocalKeys[prefsFile].orEmpty()

    /**
     * A relative, forward-slash path with no empty, `.` or `..` segment: the shape a zip entry
     * must have before it is even compared with the allowlist. Rejects zip-slip (`../x`),
     * absolute paths (`/x`, `C:/x`) and backslash tricks.
     */
    fun isSafePath(path: String): Boolean {
        if (path.isEmpty() || path.length > MAX_PATH_LENGTH) return false
        if (path.startsWith("/") || '\\' in path || '\u0000' in path) return false
        if (path.length >= 2 && path[1] == ':') return false
        return path.split('/').none { it.isEmpty() || it == "." || it == ".." }
    }

    /** The section [path] belongs to, or null when it is not something a backup may hold. */
    fun sectionOf(path: String): BackupSection? {
        if (!isSafePath(path)) return null
        if (excludedSuffixes.any { path.endsWith(it) }) return null
        if (excludedSubtrees.any { path == it || path.startsWith("$it/") }) return null
        if (path.startsWith(PREFS_DIR)) {
            val name = path.removePrefix(PREFS_DIR).removeSuffix(PREFS_SUFFIX)
            if (!path.endsWith(PREFS_SUFFIX) || '/' in name) return null
            return BackupSection.entries.firstOrNull { name in it.prefsFiles }
        }
        return BackupSection.entries.firstOrNull { section ->
            path in section.files || section.dirs.any { path.startsWith("$it/") }
        }
    }

    fun defaultFileName(date: LocalDate): String =
        String.format(Locale.ROOT, "muzviz-backup-%04d%02d%02d.zip", date.year, date.monthValue, date.dayOfMonth)
}

/**
 * One SharedPreferences file as JSON, each value tagged with its type so it comes back as the
 * same `putX` call: `{"key": {"type": "float", "value": 0.5}}`. SharedPreferences cannot tell
 * an Int from a Long or a Float from a Double once it is plain JSON, and reading a key with
 * the wrong getter throws a ClassCastException at whichever screen asks first.
 */
object PrefsJson {
    private const val TYPE = "type"
    private const val VALUE = "value"

    fun encode(values: Map<String, Any?>): String {
        val root = JSONObject()
        for ((key, value) in values.toSortedMap()) {
            val typed =
                when (value) {
                    is String -> JSONObject().put(TYPE, "string").put(VALUE, value)
                    is Int -> JSONObject().put(TYPE, "int").put(VALUE, value)
                    is Long -> JSONObject().put(TYPE, "long").put(VALUE, value)
                    is Float -> JSONObject().put(TYPE, "float").put(VALUE, value.toDouble())
                    is Boolean -> JSONObject().put(TYPE, "boolean").put(VALUE, value)
                    is Set<*> -> JSONObject().put(TYPE, "stringSet").put(VALUE, JSONArray(value.filterIsInstance<String>().sorted()))
                    else -> null
                }
            if (typed != null) root.put(key, typed)
        }
        return root.toString(2)
    }

    /** Values are String, Int, Long, Float, Boolean or Set<String>. Throws DAMAGED on anything else. */
    fun decode(text: String): Map<String, Any> =
        try {
            val root = JSONObject(text)
            root.keys().asSequence().associateWith { key ->
                val typed = root.getJSONObject(key)
                when (val type = typed.getString(TYPE)) {
                    "string" -> typed.getString(VALUE)
                    "int" -> typed.getInt(VALUE)
                    "long" -> typed.getLong(VALUE)
                    "float" -> typed.getDouble(VALUE).toFloat()
                    "boolean" -> typed.getBoolean(VALUE)
                    "stringSet" -> typed.getJSONArray(VALUE).let { a -> (0 until a.length()).map(a::getString).toSet() }
                    else -> fail(BackupProblem.DAMAGED, "unknown prefs value type $type for $key")
                }
            }
        } catch (e: JSONException) {
            throw BackupException(BackupProblem.DAMAGED, "prefs file is not valid JSON", e)
        }
}
