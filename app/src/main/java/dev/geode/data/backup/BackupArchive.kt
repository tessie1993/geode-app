package dev.geode.data.backup

import dev.geode.engine.bridge.BackupNative
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.security.MessageDigest
import java.util.zip.ZipEntry
import java.util.zip.ZipException
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream

/**
 * Reads and writes backup zips: the platform's half. java.util.zip, SHA-256 and the files; every decision (what goes
 * in, which entries come out and where, the caps, the migration, the allowlist) is core/backup's, asked once per
 * plan through [BackupNative].
 */
internal object BackupArchive {
    private const val BUFFER_BYTES = 64 * 1024

    /** One source of a backup zip: [bytes] when native holds the content, else the file at [path] under filesDir. */
    class Source(
        val path: String,
        val bytes: ByteArray?,
    )

    /** What [copyHashed] saw: the byte count, the lowercase hex SHA-256 and whether it stopped at the budget. */
    class Hashed(
        val size: Long,
        val sha256: String,
        val overBudget: Boolean,
    )

    /** What an unpack leaves for the preview. */
    class Unpacked(
        val createdAtMs: Long,
        val appVersionCode: Int,
        val sections: List<String>,
    )

    /**
     * Streams [sources] into a zip on [out], hashing each as it is copied, then writes the manifest native makes from
     * what was written, last. A source whose file vanished ([open] returns null) is left out. [budget] is the byte cap
     * on the files together.
     */
    fun pack(
        out: OutputStream,
        sources: List<Source>,
        budget: Long,
        manifestPath: String,
        appVersionCode: Int,
        createdAtMs: Long,
        open: (String) -> InputStream?,
    ): Int {
        val written = ArrayList<String>()
        var total = 0L
        val zip = ZipOutputStream(out)
        for (source in sources) {
            val input = source.bytes?.inputStream() ?: open(source.path) ?: continue
            zip.putNextEntry(ZipEntry(source.path))
            val hashed = input.use { copyHashed(it, zip, budget - total) }
            if (hashed.overBudget) fail(BackupProblem.TOO_LARGE, "over the size cap")
            zip.closeEntry()
            total += hashed.size
            written += listOf(source.path, hashed.size.toString(), hashed.sha256)
        }
        val answer = BackupWire.of(BackupNative.finishExport(written.toTypedArray(), appVersionCode, createdAtMs))
        answer.failure()
        zip.putNextEntry(ZipEntry(manifestPath))
        zip.write(answer.str().toByteArray(Charsets.UTF_8))
        zip.closeEntry()
        zip.finish()
        return written.size / 3
    }

    /**
     * Unpacks [archive] into [staging]; nothing outside it is written. Native is asked once per plan: what to extract
     * from the zip's listing and manifest, whether each target stays inside staging, each extracted item against its
     * promise, and the finish (migration, allowlist, preview).
     */
    fun unpack(
        archive: File,
        staging: File,
        layout: BackupLayout,
    ): Unpacked {
        val (manifestText, readForFinish) =
            try {
                ZipFile(archive).use { extract(it, staging, layout) }
            } catch (e: ZipException) {
                throw BackupException(BackupProblem.NOT_A_BACKUP, "not a readable zip", e)
            }
        val request = arrayListOf(manifestText)
        for (path in readForFinish) {
            val file = File(staging, path)
            if (file.isFile) request += listOf(path, file.readText())
        }
        val answer = BackupWire.of(BackupNative.finishUnpack(request.toTypedArray()))
        answer.failure()
        val createdAtMs = answer.long()
        val appVersionCode = answer.int()
        val sections = answer.strs()
        repeat(answer.int()) {
            val target = File(staging, answer.str()).apply { parentFile?.mkdirs() }
            target.writeBytes(answer.str().toByteArray(Charsets.UTF_8))
        }
        return Unpacked(createdAtMs, appVersionCode, sections)
    }

    private class Item(
        val index: Int,
        val path: String,
        val promisedSize: String,
        val promisedSha: String,
        val budget: String,
    )

    private fun extract(
        zip: ZipFile,
        staging: File,
        layout: BackupLayout,
    ): Pair<String, List<String>> {
        // Past the entry cap the count alone is rejected, so the listing need not be any longer than that.
        val entries =
            zip
                .entries()
                .asSequence()
                .take(layout.maxEntries + 2)
                .toList()
        var manifestText = ""
        var overCap = false
        val manifest = zip.getEntry(layout.manifestPath)
        if (manifest != null) {
            val bytes = ByteArrayOutputStream()
            overCap = zip.getInputStream(manifest).use { copyHashed(it, bytes, layout.maxManifestBytes) }.overBudget
            if (!overCap) manifestText = bytes.toString(Charsets.UTF_8.name())
        }
        val request = arrayListOf(zip.size().toString(), flag(manifest != null), flag(overCap), manifestText)
        entries.forEach { request += listOf(it.name, flag(it.isDirectory)) }
        val plan = BackupWire.of(BackupNative.planUnpack(request.toTypedArray()))
        plan.failure()
        val afterProblem = plan.int()
        val afterMessage = plan.str()
        val readForFinish = plan.strs()
        val items = List(plan.int()) { Item(plan.int(), plan.str(), plan.str(), plan.str(), plan.str()) }

        val root = staging.canonicalFile
        val staged = arrayListOf(root.path, File.separator)
        items.forEach { staged += listOf(it.path, File(root, it.path).canonicalFile.path) }
        BackupWire.of(BackupNative.checkStaging(staged.toTypedArray())).failure()

        for (item in items) {
            val target = File(root, item.path).canonicalFile
            target.parentFile?.mkdirs()
            val hashed =
                zip.getInputStream(entries[item.index]).use { input ->
                    target.outputStream().use { copyHashed(input, it, item.budget.toLong()) }
                }
            val checked =
                arrayOf(
                    item.path,
                    item.promisedSize,
                    item.promisedSha,
                    item.budget,
                    hashed.size.toString(),
                    hashed.sha256,
                    flag(hashed.overBudget),
                )
            BackupWire.of(BackupNative.checkExtracted(checked)).failure()
        }
        // What was wrong with an entry after the last one extracted: an unsafe name, a repeat, a file the manifest lacks.
        if (afterProblem != 0) throw BackupException(BackupWire.problemOf(afterProblem), afterMessage)
        return manifestText to readForFinish
    }

    private fun flag(on: Boolean): String = if (on) "1" else "0"

    /**
     * Copies [input] to [out] and returns the byte count and lowercase hex SHA-256. Stops, flagged over budget, as
     * soon as more than [budget] bytes have passed, before they reach the disk.
     */
    fun copyHashed(
        input: InputStream,
        out: OutputStream,
        budget: Long,
    ): Hashed {
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(BUFFER_BYTES)
        var size = 0L
        while (true) {
            val n = input.read(buffer)
            if (n < 0) break
            size += n
            if (size > budget) return Hashed(size, "", true)
            digest.update(buffer, 0, n)
            out.write(buffer, 0, n)
        }
        return Hashed(size, digest.digest().joinToString("") { (it.toInt() and 0xff).toString(16).padStart(2, '0') }, false)
    }
}
