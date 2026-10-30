package dev.geode.data.backup

import android.annotation.SuppressLint
import android.content.Context
import android.content.SharedPreferences
import android.net.Uri
import android.provider.DocumentsContract
import dagger.hilt.android.qualifiers.ApplicationContext
import dev.geode.BuildConfig
import dev.geode.RingLog
import dev.geode.di.IoDispatcher
import dev.geode.util.bestEffort
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileInputStream
import java.io.FileNotFoundException
import java.io.IOException
import java.nio.file.Files
import javax.inject.Inject
import javax.inject.Singleton

/** What an inspected backup holds, for the screen that asks which parts to restore. */
data class BackupPreview(
    val createdAtMs: Long,
    val appVersionCode: Int,
    val sections: Set<BackupSection>,
)

/**
 * The manual backup: a zip the user saves and opens through the system file picker, so nothing
 * here needs a network or a permission. Format and checks are in [BackupArchive]; this class
 * only moves bytes between the picker, filesDir and SharedPreferences, on [io].
 *
 * A restore is two calls. [inspect] copies the chosen file in, unpacks it into a staging
 * directory and runs every check, touching no app data. [restore] then swaps the chosen
 * sections in, one at a time: each section's directories are renamed aside, the staged ones
 * renamed into place, and the old ones deleted only once all of that section's renames have
 * worked. Prefs are written with commit() after the files. Staging lives in noBackupFilesDir,
 * on the same filesystem as filesDir so the renames are renames, and outside Auto Backup.
 *
 * Stores and controllers read their files once and keep them in memory, so a restore asks the
 * user to restart the app instead of trying to reload twenty owners in place.
 */
@Singleton
class BackupRepository
    @Inject
    constructor(
        @ApplicationContext private val context: Context,
        @IoDispatcher private val io: CoroutineDispatcher,
    ) {
        private val filesDir: File get() = context.filesDir
        private val workDir: File get() = File(context.noBackupFilesDir, "backup-restore")
        private val stagedDir: File get() = File(workDir, "staged")
        private val asideDir: File get() = File(workDir, "aside")

        @Volatile private var staged: BackupPreview? = null

        /** Writes every section to [destination]. */
        suspend fun export(destination: Uri) =
            withContext(io) {
                guarded("export") {
                    val sources = BackupSection.entries.flatMap(::sourcesFor)
                    val out =
                        context.contentResolver.openOutputStream(destination, "wt")
                            ?: fail(BackupProblem.IO_FAILED, "no output stream for the chosen file")
                    val now = System.currentTimeMillis()
                    try {
                        val manifest = out.use { BackupArchive.pack(it, sources, BackupSection.entries, BuildConfig.VERSION_CODE, now) }
                        RingLog.note(TAG, "exported ${manifest.entries.size} files")
                    } catch (e: BackupException) {
                        // A half-written zip has no manifest (it goes in last), so it could never
                        // be restored; removing it keeps it from passing for a backup.
                        bestEffort(TAG, "delete partial backup") {
                            DocumentsContract.deleteDocument(context.contentResolver, destination)
                        }
                        throw e
                    }
                }
            }

        /** Reads and checks [source] into staging. Nothing outside staging changes. */
        suspend fun inspect(source: Uri): BackupPreview =
            withContext(io) {
                guarded("inspect") {
                    discardStaging()
                    val archive = File(workDir, "import.zip")
                    try {
                        copyIn(source, archive)
                        val manifest = BackupArchive.unpack(archive, stagedDir)
                        BackupPreview(manifest.createdAtMs, manifest.appVersionCode, manifest.knownSections())
                            .also { staged = it }
                    } finally {
                        archive.delete()
                        // Any failure, checked or not, leaves no half-unpacked staging behind.
                        if (staged == null) workDir.deleteRecursively()
                    }
                }
            }

        /** Replaces [sections] with what [inspect] staged, then drops the staging directory. */
        suspend fun restore(sections: Set<BackupSection>) =
            withContext(io) {
                guarded("restore") {
                    val preview = staged ?: fail(BackupProblem.IO_FAILED, "nothing staged")
                    try {
                        for (section in BackupSection.entries.filter { it in sections && it in preview.sections }) {
                            swapFiles(section)
                            section.prefsFiles.forEach(::restorePrefs)
                            RingLog.note(TAG, "restored ${section.id}")
                        }
                    } finally {
                        discardStaging()
                    }
                }
            }

        suspend fun discard() = withContext(io) { discardStaging() }

        private fun discardStaging() {
            staged = null
            workDir.deleteRecursively()
        }

        private fun sourcesFor(section: BackupSection): List<BackupSource> {
            val prefs =
                section.prefsFiles.map { name ->
                    val values = prefsFile(name).all.filterKeys { BackupFormat.isPortableKey(name, it) }
                    val bytes = PrefsJson.encode(values).toByteArray(Charsets.UTF_8)
                    BackupSource(BackupFormat.prefsPath(name)) { bytes.inputStream() }
                }
            val files =
                (section.dirs + section.files)
                    .map { File(filesDir, it) }
                    .flatMap { root -> root.walkTopDown().filter { it.isFile && !Files.isSymbolicLink(it.toPath()) } }
                    .map { it.relativeTo(filesDir).invariantSeparatorsPath to it }
                    .filter { (path, _) -> BackupFormat.sectionOf(path) == section }
                    .map { (path, file) -> BackupSource(path) { openOrNull(file) } }
            return prefs + files
        }

        private fun openOrNull(file: File): FileInputStream? =
            try {
                FileInputStream(file)
            } catch (ignored: FileNotFoundException) {
                null
            }

        private fun copyIn(
            source: Uri,
            archive: File,
        ) {
            archive.parentFile?.mkdirs()
            val input =
                context.contentResolver.openInputStream(source)
                    ?: fail(BackupProblem.IO_FAILED, "no input stream for the chosen file")
            // The archive itself is compressed, so it can only exceed the uncompressed cap by
            // zip overhead; the slack keeps an honest backup of incompressible textures inside it.
            val cap = BackupLimits().maxTotalBytes + ARCHIVE_SLACK_BYTES
            input.use { stream -> archive.outputStream().use { BackupArchive.copyHashed(stream, it, cap) } }
        }

        /**
         * Renames every target of [section] aside, then every staged target into place. If a
         * rename fails part-way, what was placed is removed and what was set aside goes back, so
         * the section is either all old or all new. Cache subtrees ([BackupFormat.excludedSubtrees])
         * move from the old copy into the new one before the old one is deleted.
         */
        private fun swapFiles(section: BackupSection) {
            val targets = section.dirs + section.files
            val setAside = mutableListOf<String>()
            val placed = mutableListOf<String>()
            try {
                for (target in targets.filter { File(filesDir, it).exists() }) {
                    move(File(filesDir, target), File(asideDir, target))
                    setAside += target
                }
                for (target in targets.filter { File(stagedDir, it).exists() }) {
                    move(File(stagedDir, target), File(filesDir, target))
                    placed += target
                }
            } catch (e: BackupException) {
                placed.forEach { File(filesDir, it).deleteRecursively() }
                setAside.forEach { target ->
                    runCatching { move(File(asideDir, target), File(filesDir, target)) }
                        .onFailure { RingLog.note(TAG, "rollback left $target in ${asideDir.path}", it) }
                }
                throw e
            }
            for (subtree in BackupFormat.excludedSubtrees) {
                val old = File(asideDir, subtree)
                if (old.exists() && targets.any { subtree.startsWith("$it/") }) {
                    runCatching { move(old, File(filesDir, subtree)) }
                        .onFailure { RingLog.note(TAG, "cache not carried over: $subtree", it) }
                }
            }
            section.dirs.forEach { File(filesDir, it).mkdirs() }
            setAside.forEach { File(asideDir, it).deleteRecursively() }
        }

        private fun move(
            from: File,
            to: File,
        ) {
            to.parentFile?.mkdirs()
            if (to.exists()) to.deleteRecursively()
            if (!from.renameTo(to)) fail(BackupProblem.IO_FAILED, "could not move ${from.name}")
        }

        /**
         * Replaces the portable keys of one prefs file with the backup's. Device-local keys on
         * this phone are left as they are, and any in the backup are ignored. commit(), not
         * apply(): the restore must know the write landed before it reports success.
         */
        @SuppressLint("ApplySharedPref")
        private fun restorePrefs(name: String) {
            val file = File(stagedDir, BackupFormat.prefsPath(name))
            if (!file.isFile) return
            val values = PrefsJson.decode(file.readText()).filterKeys { BackupFormat.isPortableKey(name, it) }
            val prefs = prefsFile(name)
            val editor = prefs.edit()
            for (key in prefs.all.keys) {
                if (BackupFormat.isPortableKey(name, key)) editor.remove(key)
            }
            for ((key, value) in values) {
                when (value) {
                    is String -> editor.putString(key, value)
                    is Int -> editor.putInt(key, value)
                    is Long -> editor.putLong(key, value)
                    is Float -> editor.putFloat(key, value)
                    is Boolean -> editor.putBoolean(key, value)
                    is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                }
            }
            if (!editor.commit()) fail(BackupProblem.IO_FAILED, "could not write $name")
        }

        // The framework caches one instance per name for the process, so this is the same
        // object GeodePrefsFiles and every store already hold: a commit here is what they read.
        private fun prefsFile(name: String): SharedPreferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)

        /** Turns platform failures into [BackupException]s, so callers handle one type. */
        private inline fun <T> guarded(
            what: String,
            block: () -> T,
        ): T =
            try {
                block()
            } catch (e: IOException) {
                RingLog.note(TAG, "$what failed", e)
                throw BackupException(BackupProblem.IO_FAILED, "$what: ${e.message}", e)
            } catch (e: SecurityException) {
                RingLog.note(TAG, "$what failed", e)
                throw BackupException(BackupProblem.IO_FAILED, "$what: no access to the chosen file", e)
            }

        private companion object {
            const val TAG = "Backup"
            const val ARCHIVE_SLACK_BYTES = 64L * 1024 * 1024
        }
    }
