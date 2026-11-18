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
import dev.geode.engine.bridge.BackupNative
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
 * here needs a network or a permission. The rules (what goes in, where it lives, what a restore
 * accepts, the caps, the migration) are core/backup's and arrive as plans through [BackupNative];
 * this class and [BackupArchive] only move bytes between the picker, filesDir and SharedPreferences, on [io].
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

        private val layout by lazy { BackupLayout.read() }

        @Volatile private var staged: BackupPreview? = null

        /** Writes every section to [destination]. */
        suspend fun export(destination: Uri) =
            withContext(io) {
                guarded("export") {
                    val plan = exportPlan()
                    val now = System.currentTimeMillis()
                    // Packed aside first: opening the destination in "wt" truncates the previous backup at
                    // once, so it is touched only after the new zip is whole.
                    val temp = File.createTempFile("backup", ".zip", context.cacheDir)
                    try {
                        val entries =
                            temp.outputStream().use {
                                BackupArchive.pack(
                                    it,
                                    plan.sources,
                                    plan.budget,
                                    layout.manifestPath,
                                    BuildConfig.VERSION_CODE,
                                    now,
                                ) { path -> openOrNull(File(filesDir, path)) }
                            }
                        val out =
                            context.contentResolver.openOutputStream(destination, "wt")
                                ?: fail(BackupProblem.IO_FAILED, "no output stream for the chosen file")
                        try {
                            temp.inputStream().use { src -> out.use { src.copyTo(it) } }
                        } catch (e: IOException) {
                            // The destination was truncated on open; a half-copied zip has no manifest
                            // (it goes in last), so it could never be restored. Removing it keeps it from
                            // passing for a backup.
                            bestEffort(TAG, "delete partial backup") {
                                DocumentsContract.deleteDocument(context.contentResolver, destination)
                            }
                            throw e
                        }
                        RingLog.note(TAG, "exported $entries files")
                    } finally {
                        temp.delete()
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
                        val unpacked = BackupArchive.unpack(archive, stagedDir, layout)
                        BackupPreview(
                            unpacked.createdAtMs,
                            unpacked.appVersionCode,
                            unpacked.sections.mapNotNull(BackupSection::fromId).toSet(),
                        ).also { staged = it }
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
                    var wrote = false
                    try {
                        for (section in restorePlan(staged, sections)) {
                            swapFiles(section)
                            // Files are on disk from here: the stores still hold the old copy in memory.
                            wrote = true
                            section.prefs.forEach(::restorePrefs)
                            RingLog.note(TAG, "restored ${section.id}")
                        }
                    } catch (e: BackupException) {
                        throw if (wrote) BackupException(e.problem, e.message.orEmpty(), e, wroteSomething = true) else e
                    } catch (e: IOException) {
                        throw if (wrote) BackupException(BackupProblem.IO_FAILED, "restore: ${e.message}", e, wroteSomething = true) else e
                    } finally {
                        discardStaging()
                    }
                }
            }

        suspend fun discard() = withContext(io) { discardStaging() }

        private fun discardStaging() {
            staged = null
            returnAside()
            workDir.deleteRecursively()
        }

        /**
         * A swap the process did not outlive leaves the user's originals in the aside directory. Whatever
         * has no file at its place in filesDir goes back before the work directory is deleted.
         */
        private fun returnAside() {
            val aside = asideDir
            if (!aside.isDirectory) return
            aside
                .walkTopDown()
                .filter { it.isFile }
                .forEach { file ->
                    val home = File(filesDir, file.relativeTo(aside).path)
                    if (home.exists()) return@forEach
                    runCatching {
                        home.parentFile?.mkdirs()
                        file.renameTo(home)
                    }.onFailure { RingLog.note(TAG, "could not return ${file.name} from ${aside.path}", it) }
                }
        }

        private class ExportPlan(
            val budget: Long,
            val sources: List<BackupArchive.Source>,
        )

        /** Hands native every prefs value the phone holds and every regular file under the roots; it answers what goes in. */
        private fun exportPlan(): ExportPlan {
            val prefs = arrayListOf(layout.prefsFiles.size.toString())
            for (name in layout.prefsFiles) {
                val all = prefsFile(name).all
                prefs += listOf(name, all.size.toString())
                all.forEach { (key, value) -> prefs.addPref(key, value) }
            }
            val found = ArrayList<String>()
            for (section in layout.sections) {
                for (root in section.roots) {
                    File(filesDir, root)
                        .walkTopDown()
                        .filter { it.isFile && !Files.isSymbolicLink(it.toPath()) }
                        .forEach { found += listOf(section.id, it.relativeTo(filesDir).invariantSeparatorsPath) }
                }
            }
            val answer = BackupWire.of(BackupNative.planExport(prefs.toTypedArray(), found.toTypedArray()))
            answer.failure()
            val budget = answer.long()
            val sources =
                List(answer.int()) {
                    val path = answer.str()
                    val inline = answer.flag()
                    val bytes = answer.str()
                    BackupArchive.Source(path, if (inline) bytes.toByteArray(Charsets.UTF_8) else null)
                }
            return ExportPlan(budget, sources)
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
            // zip overhead; native's cap includes the slack that keeps an honest backup of
            // incompressible files inside it.
            val hashed = input.use { stream -> archive.outputStream().use { BackupArchive.copyHashed(stream, it, layout.maxArchiveBytes) } }
            if (hashed.overBudget) fail(BackupProblem.TOO_LARGE, "over the size cap")
        }

        private class PrefsPlan(
            val file: String,
            val remove: List<String>,
            val put: List<Pair<String, Any?>>,
        )

        private class SectionPlan(
            val id: String,
            val targets: List<String>,
            val carryOver: List<String>,
            val ensureDirs: List<String>,
            val prefs: List<PrefsPlan>,
        )

        /** Hands native what was staged, what the user ticked and the keys the phone holds; it answers per section what to do. */
        private fun restorePlan(
            preview: BackupPreview?,
            sections: Set<BackupSection>,
        ): List<SectionPlan> {
            val request = arrayListOf(if (preview != null) "1" else "0")
            val stagedIds = preview?.sections.orEmpty().map { it.id }
            request += stagedIds.size.toString()
            request += stagedIds
            request += sections.size.toString()
            request += sections.map { it.id }
            val stagedPrefs =
                layout.sections
                    .flatMap {
                        it.prefs.map { (_, path) ->
                            path
                        }
                    }.distinct()
                    .filter { File(stagedDir, it).isFile }
            request += stagedPrefs.size.toString()
            stagedPrefs.forEach { request += listOf(it, File(stagedDir, it).readText()) }
            for (name in layout.prefsFiles) {
                val keys = prefsFile(name).all.keys
                request += listOf(name, keys.size.toString())
                request += keys
            }
            val answer = BackupWire.of(BackupNative.planRestore(request.toTypedArray()))
            answer.failure()
            return List(answer.int()) {
                SectionPlan(
                    id = answer.str(),
                    targets = answer.strs(),
                    carryOver = answer.strs(),
                    ensureDirs = answer.strs(),
                    prefs =
                        List(answer.int()) {
                            val file = answer.str()
                            answer.str() // the staged path, which the plan already read
                            PrefsPlan(file, answer.strs(), List(answer.int()) { answer.prefValue() })
                        },
                )
            }
        }

        /**
         * Renames every target of [section] aside, then every staged target into place. If a
         * rename fails part-way, what was placed is removed and what was set aside goes back, so
         * the section is either all old or all new. Cache subtrees ([SectionPlan.carryOver]) move
         * from the old copy into the new one before the old one is deleted.
         */
        private fun swapFiles(section: SectionPlan) {
            val targets = section.targets
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
            for (subtree in section.carryOver) {
                val old = File(asideDir, subtree)
                if (old.exists()) {
                    runCatching { move(old, File(filesDir, subtree)) }
                        .onFailure { RingLog.note(TAG, "cache not carried over: $subtree", it) }
                }
            }
            section.ensureDirs.forEach { File(filesDir, it).mkdirs() }
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
         * Writes one prefs file's part of a restore: removes the keys native named, then puts the backup's values.
         * A value of another type than its key's ([BackupFormat.hasDeclaredType]) is skipped, so the key reads as
         * its default. commit(), not apply(): the restore must know the write landed before it reports success.
         */
        @SuppressLint("ApplySharedPref")
        private fun restorePrefs(plan: PrefsPlan) {
            val editor = prefsFile(plan.file).edit()
            plan.remove.forEach(editor::remove)
            for ((key, value) in plan.put) {
                if (value == null || !BackupFormat.hasDeclaredType(plan.file, key, value)) continue
                when (value) {
                    is String -> editor.putString(key, value)
                    is Int -> editor.putInt(key, value)
                    is Long -> editor.putLong(key, value)
                    is Float -> editor.putFloat(key, value)
                    is Boolean -> editor.putBoolean(key, value)
                    is Set<*> -> editor.putStringSet(key, value.filterIsInstance<String>().toSet())
                }
            }
            if (!editor.commit()) fail(BackupProblem.IO_FAILED, "could not write ${plan.file}")
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
        }
    }
