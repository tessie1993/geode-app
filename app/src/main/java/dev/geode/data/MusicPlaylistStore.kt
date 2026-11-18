package dev.geode.data

import android.content.Context
import androidx.annotation.VisibleForTesting
import dev.geode.engine.bridge.LibraryNative
import java.io.File

data class MusicPlaylist(
    val name: String,
    val trackUris: List<String> = emptyList(),
)

/**
 * One JSON file per playlist under `files/music-playlists`. The file format, the order, every edit (free names
 * included) and the one-time rename of legacy file names are native's (core/library/PlaylistRules); this lists the
 * folder, reads and writes the files' text and does what native answers.
 */
class MusicPlaylistStore(
    context: Context,
) {
    private val dir = File(context.filesDir, "music-playlists").apply { mkdirs() }

    private var migrated = false

    /** One edit of the folder, a `LibraryNative.MUSIC_PLAYLIST_*` [kind]. */
    data class Edit(
        val kind: Int,
        val name: String,
        val newName: String = "",
        val track: String = "",
        val tracks: List<String> = emptyList(),
        val from: Int = 0,
        val to: Int = 0,
    )

    /** What an edit does: [json] written to [name]'s file when [write], then [deleteName]'s file deleted ("" for none). */
    data class Outcome(
        val write: Boolean,
        val ok: Boolean,
        val name: String,
        val deleteName: String,
        val json: String,
    )

    /** The playlist rules, native's; a seam so a JVM test, which has no libgeode, can stand in for them. */
    interface Rules {
        fun list(texts: List<String>): List<MusicPlaylist>

        fun apply(
            edit: Edit,
            texts: List<String>,
            named: String?,
        ): Outcome?

        fun migration(
            fileNames: List<String>,
            texts: List<String>,
        ): List<Pair<String, String>>

        fun importBase(
            parsedName: String,
            fileName: String,
            untitled: String,
        ): String
    }

    /**
     * Renames files saved under older name rules, once, before the first read or write of the
     * folder. It parses every playlist, so it runs on whichever thread first uses the store (an IO
     * one for the app's own) and not in the constructor, which the session runs on the main thread.
     */
    @Synchronized
    private fun ensureMigrated() {
        if (migrated) return
        migrated = true
        migrateLegacyFileNames()
    }

    private fun migrateLegacyFileNames() {
        val entries = dir.listFiles().orEmpty().toList()
        val texts = entries.map { f -> if (f.extension == "json") runCatching { f.readText() }.getOrDefault("") else "" }
        rules.migration(entries.map { it.name }, texts).forEach { (from, to) ->
            val target = File(dir, to)
            // A rename replaces what it lands on: never over a file that is there.
            if (!target.exists()) File(dir, from).renameTo(target)
        }
    }

    fun list(): List<MusicPlaylist> {
        ensureMigrated()
        return rules.list(texts())
    }

    /** A new playlist of [uris], each once, under the first free name from [name] (nothing for a blank name). */
    fun create(
        name: String,
        uris: List<String>,
    ) {
        edit(Edit(LibraryNative.MUSIC_PLAYLIST_CREATE, name, tracks = uris))
    }

    /** An imported file's playlist under the first free name from its own, the file's or [untitled]; the name used. */
    fun importPlaylist(
        parsedName: String,
        fileName: String,
        untitled: String,
        uris: List<String>,
    ): String {
        ensureMigrated()
        val base = rules.importBase(parsedName, fileName, untitled)
        return edit(Edit(LibraryNative.MUSIC_PLAYLIST_IMPORT, base, tracks = uris))?.name ?: base
    }

    fun delete(name: String) {
        ensureMigrated()
        fileOf(name).delete()
    }

    fun addTrack(
        name: String,
        uri: String,
    ) {
        edit(Edit(LibraryNative.MUSIC_PLAYLIST_ADD, name, track = uri))
    }

    fun rename(
        oldName: String,
        newName: String,
    ): Boolean = edit(Edit(LibraryNative.MUSIC_PLAYLIST_RENAME, oldName, newName = newName))?.ok == true

    fun move(
        name: String,
        from: Int,
        to: Int,
    ) {
        edit(Edit(LibraryNative.MUSIC_PLAYLIST_MOVE, name, from = from, to = to))
    }

    fun removeTrack(
        name: String,
        uri: String,
    ) {
        edit(Edit(LibraryNative.MUSIC_PLAYLIST_REMOVE, name, track = uri))
    }

    // Native decides; this reads the folder for it, writes what it answers, and reports a failed write as not ok.
    private fun edit(edit: Edit): Outcome? {
        ensureMigrated()
        val f = fileOf(edit.name)
        val named = if (f.exists()) runCatching { f.readText() }.getOrDefault("") else null
        val out = rules.apply(edit, texts(), named) ?: return null
        if (!out.write) return out
        if (!AtomicWrite.text(fileOf(out.name), out.json)) return out.copy(ok = false)
        if (out.deleteName.isNotEmpty()) fileOf(out.deleteName).delete()
        return out
    }

    private fun texts(): List<String> =
        dir
            .listFiles { f -> f.extension == "json" }
            ?.mapNotNull { runCatching { it.readText() }.getOrNull() }
            .orEmpty()

    private fun fileOf(name: String): File = File(dir, PresetStore.safeFileName(name) + ".json")

    private object NativeRules : Rules {
        private const val FIELD = '\u001F'
        private const val RECORD = '\u001E'
        private const val OUTCOME_FIELDS = 5

        private fun records(wire: String?): List<List<String>> =
            // Every record is ended by one, so the last piece is empty (an empty playlist name is a record too).
            wire
                .orEmpty()
                .split(RECORD)
                .dropLast(1)
                .map { it.split(FIELD) }

        override fun list(texts: List<String>): List<MusicPlaylist> =
            records(LibraryNative.musicPlaylistList(texts.toTypedArray())).map { MusicPlaylist(it[0], it.drop(1)) }

        override fun apply(
            edit: Edit,
            texts: List<String>,
            named: String?,
        ): Outcome? {
            val wire =
                LibraryNative.musicPlaylistApply(
                    edit.kind,
                    edit.name,
                    edit.newName,
                    edit.track,
                    edit.tracks.toTypedArray(),
                    edit.from,
                    edit.to,
                    texts.toTypedArray(),
                    named,
                )
            val f = records(wire).firstOrNull()?.takeIf { it.size == OUTCOME_FIELDS } ?: return null
            return Outcome(write = f[0] == "1", ok = f[1] == "1", name = f[2], deleteName = f[3], json = f[4])
        }

        override fun migration(
            fileNames: List<String>,
            texts: List<String>,
        ): List<Pair<String, String>> =
            records(LibraryNative.musicPlaylistMigration(fileNames.toTypedArray(), texts.toTypedArray()))
                .filter { it.size == 2 }
                .map { it[0] to it[1] }

        override fun importBase(
            parsedName: String,
            fileName: String,
            untitled: String,
        ): String = LibraryNative.musicPlaylistImportBase(parsedName, fileName, untitled) ?: untitled
    }

    companion object {
        /** Native's rules; JVM tests, which cannot load libgeode, put their own in. */
        @VisibleForTesting
        internal var rules: Rules = NativeRules
    }
}
