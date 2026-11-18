package dev.geode.data.backup

import dev.geode.audio.AudioFxController
import dev.geode.data.AppStateCodec
import dev.geode.data.AutoDirectorCodec
import dev.geode.data.AutoVisualsPrefsStore
import dev.geode.data.BackgroundPrefsStore
import dev.geode.data.BehaviourCodec
import dev.geode.data.BookmarkStore
import dev.geode.data.BootAnimationStore
import dev.geode.data.CustomizeGroupsCodec
import dev.geode.data.ExportPrefsStore
import dev.geode.data.FavouritesStore
import dev.geode.data.GeneralSettingsCodec
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.KeepSetStore
import dev.geode.data.LfoStore
import dev.geode.data.LibraryPrefsStore
import dev.geode.data.LibrarySettingsCodec
import dev.geode.data.LookStore
import dev.geode.data.MEDIA_ROOTS_PREF_KEYS
import dev.geode.data.MacroStore
import dev.geode.data.NoteResponseCodec
import dev.geode.data.PaletteStackStore
import dev.geode.data.PerformanceStore
import dev.geode.data.PlayerPrefsCodec
import dev.geode.data.RecordPrefsStore
import dev.geode.data.SoundInputSettingsCodec
import dev.geode.data.StudioPrefsStore
import dev.geode.data.StyleShelfCodec
import dev.geode.data.VisualsSettingsCodec
import dev.geode.data.WatermarkPrefsCodec
import dev.geode.data.prefs.PrefKey
import dev.geode.data.prefs.PrefType
import dev.geode.engine.bridge.BackupNative
import dev.geode.wallpaper.WallpaperPrefsStore
import java.time.LocalDate

// What the shell keeps of a backup's rules. The rules themselves (sections, paths, caps, migration, manifest, plans)
// are C++ in core/backup, reached through BackupNative; this file holds the problem type the screens name, the
// section choices they offer, and the one check that needs the stores' own key lists.

/** Why a backup could not be written or read. The UI names each one; the message is for the log. In `Problem` order. */
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
    /** A restore that failed after at least one section was already swapped in: the app must restart. */
    val wroteSomething: Boolean = false,
) : Exception(message, cause)

internal fun fail(
    problem: BackupProblem,
    message: String,
): Nothing = throw BackupException(problem, message)

/** What the user can choose to restore: the ids of core/backup's sections. */
enum class BackupSection(
    val id: String,
) {
    SETTINGS("settings"),
    CREATIVE("creative"),
    MUSIC("music"),
    MILK("milk"),
    ;

    companion object {
        fun fromId(id: String): BackupSection? = entries.firstOrNull { it.id == id }
    }
}

object BackupFormat {
    fun defaultFileName(date: LocalDate): String = BackupNative.fileName(date.year, date.monthValue, date.dayOfMonth)

    /** The stores' own key lists for the files a backup restores, held to them by `PrefsSchemaTest`. */
    internal val declaredKeys: List<PrefKey> by lazy {
        GeneralSettingsCodec.keys + SoundInputSettingsCodec.keys + VisualsSettingsCodec.keys + LibrarySettingsCodec.keys +
            AppStateCodec.keys + BehaviourCodec.keys + ExportPrefsStore.PREF_KEYS + StudioPrefsStore.PREF_KEYS +
            BootAnimationStore.PREF_KEYS + CustomizeGroupsCodec.keys + MacroStore.PREF_KEYS + PaletteStackStore.PREF_KEYS +
            LfoStore.PREF_KEYS + AutoDirectorCodec.keys + NoteResponseCodec.keys + RecordPrefsStore.PREF_KEYS +
            WatermarkPrefsCodec.keys + PerformanceStore.PREF_KEYS + LookStore.PREF_KEYS + AutoVisualsPrefsStore.PREF_KEYS +
            StyleShelfCodec.keys + GeodePrefsFiles.MILK_PRESET_PREF_KEYS + PlayerPrefsCodec.keys + BookmarkStore.PREF_KEYS +
            AudioFxController.PREF_KEYS + LibraryPrefsStore.PREF_KEYS + MEDIA_ROOTS_PREF_KEYS + BackgroundPrefsStore.PREF_KEYS +
            WallpaperPrefsStore.PREF_KEYS + FavouritesStore.PREF_KEYS + KeepSetStore.PREF_KEYS
    }

    /**
     * False when [key] of [prefsFile] is declared with a type other than [value]'s: written back, it
     * would throw a ClassCastException at its store's getter on every start. A key with no declared
     * type passes.
     */
    fun hasDeclaredType(
        prefsFile: String,
        key: String,
        value: Any,
    ): Boolean {
        val declared = declaredKeys.firstOrNull { it.file == prefsFile && it.matches(key) }?.type ?: return true
        val type =
            when (value) {
                is Boolean -> PrefType.BOOLEAN
                is Int -> PrefType.INT
                is Long -> PrefType.LONG
                is Float -> PrefType.FLOAT
                is String -> PrefType.STRING
                is Set<*> -> PrefType.STRING_SET
                else -> null
            }
        return type == declared
    }
}
