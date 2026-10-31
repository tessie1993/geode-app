package dev.geode.data.backup

import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.security.MessageDigest
import java.util.zip.ZipEntry
import java.util.zip.ZipException
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream

/** A file to put in a backup. [open] returns null when the file vanished after it was listed. */
class BackupSource(
    val path: String,
    val open: () -> InputStream?,
)

/**
 * Writes and reads backup zips. Plain java.util.zip, no Android: the repository hands it
 * streams and directories, and every check a restore makes happens here.
 */
object BackupArchive {
    private const val BUFFER_BYTES = 64 * 1024

    /**
     * Streams [sources] into a zip on [out], hashing each file as it is copied, then writes the
     * manifest last. Hashing on the way through rather than in a first pass means a store that
     * rewrites a file mid-export cannot leave a manifest that disagrees with the bytes.
     */
    fun pack(
        out: OutputStream,
        sources: List<BackupSource>,
        sections: List<BackupSection>,
        appVersionCode: Int,
        createdAtMs: Long,
        limits: BackupLimits = BackupLimits(),
    ): BackupManifest {
        if (sources.size > limits.maxEntries) fail(BackupProblem.TOO_LARGE, "${sources.size} files")
        val entries = ArrayList<BackupEntry>(sources.size)
        var total = 0L
        val zip = ZipOutputStream(out)
        for (source in sources) {
            check(BackupFormat.sectionOf(source.path) != null) { "not a backup path: ${source.path}" }
            val input = source.open() ?: continue
            zip.putNextEntry(ZipEntry(source.path))
            val (size, sha) = input.use { copyHashed(it, zip, limits.maxTotalBytes - total) }
            zip.closeEntry()
            total += size
            entries += BackupEntry(source.path, size, sha)
        }
        val manifest =
            BackupManifest(
                format = BackupFormat.FORMAT_ID,
                version = BackupFormat.CURRENT_VERSION,
                appVersionCode = appVersionCode,
                createdAtMs = createdAtMs,
                sections = sections.map { it.id },
                entries = entries,
            )
        zip.putNextEntry(ZipEntry(BackupFormat.MANIFEST_PATH))
        zip.write(manifest.encode().toByteArray(Charsets.UTF_8))
        zip.closeEntry()
        zip.finish()
        return manifest
    }

    /**
     * Unpacks [archive] into [staging] and returns its manifest, migrated to the current version.
     * Nothing outside [staging] is written. In order:
     *
     * 1. the format id and version (a newer backup is NEWER_VERSION before anything else);
     * 2. per entry: a safe relative path that stays inside [staging], listed in the manifest
     *    once, within the size and entry caps (counted on the real bytes, not the zip header),
     *    with the promised size and SHA-256; and every manifest entry present;
     * 3. the migration chain;
     * 4. every path inside a known section's allowlist, every prefs file parseable.
     *
     * The allowlist runs after migration because it is the current version's allowlist: an
     * older backup may keep a file somewhere a migration step moves it from.
     */
    fun unpack(
        archive: File,
        staging: File,
        limits: BackupLimits = BackupLimits(),
        steps: List<BackupMigration> = BackupMigrations.steps,
    ): BackupManifest {
        val manifest =
            try {
                ZipFile(archive).use { zip ->
                    if (zip.size() > limits.maxEntries + 1) fail(BackupProblem.TOO_LARGE, "${zip.size()} entries")
                    readManifest(zip).also { extract(zip, it, staging, limits) }
                }
            } catch (e: ZipException) {
                throw BackupException(BackupProblem.NOT_A_BACKUP, "not a readable zip", e)
            }
        val migrated = BackupMigrations.migrate(manifest, staging, steps)
        checkContents(migrated, staging)
        return migrated
    }

    private fun readManifest(zip: ZipFile): BackupManifest {
        val entry = zip.getEntry(BackupFormat.MANIFEST_PATH) ?: fail(BackupProblem.NOT_A_BACKUP, "no manifest.json")
        val bytes = zip.getInputStream(entry).use { readBounded(it, BackupFormat.MAX_MANIFEST_BYTES) }
        return BackupManifest.decode(bytes.toString(Charsets.UTF_8))
    }

    private fun extract(
        zip: ZipFile,
        manifest: BackupManifest,
        staging: File,
        limits: BackupLimits,
    ) {
        val expected = manifest.entries.associateBy { it.path }
        if (expected.size != manifest.entries.size) fail(BackupProblem.DAMAGED, "manifest lists a path twice")
        val root = staging.canonicalFile
        val seen = HashSet<String>()
        var total = 0L
        for (entry in zip.entries()) {
            val name = entry.name
            // Directories are implied by the files; nothing is created for them.
            if (entry.isDirectory || name == BackupFormat.MANIFEST_PATH) continue
            if (!BackupFormat.isSafePath(name)) fail(BackupProblem.UNSAFE_PATH, "unsafe path '$name'")
            if (!seen.add(name)) fail(BackupProblem.DAMAGED, "'$name' appears twice")
            val promised = expected[name] ?: fail(BackupProblem.DAMAGED, "'$name' is not in the manifest")
            val target = File(root, name).canonicalFile
            if (!target.path.startsWith(root.path + File.separator)) fail(BackupProblem.UNSAFE_PATH, "'$name' leaves staging")
            val (size, sha) = extractEntry(zip, entry, target, limits.maxTotalBytes - total)
            total += size
            if (size != promised.size || sha != promised.sha256) fail(BackupProblem.DAMAGED, "'$name' does not match its checksum")
        }
        if (seen.size != expected.size) fail(BackupProblem.DAMAGED, "${expected.size - seen.size} files are missing")
    }

    private fun extractEntry(
        zip: ZipFile,
        entry: ZipEntry,
        target: File,
        budget: Long,
    ): Pair<Long, String> {
        target.parentFile?.mkdirs()
        return zip.getInputStream(entry).use { input ->
            target.outputStream().use { out -> copyHashed(input, out, budget) }
        }
    }

    private fun checkContents(
        manifest: BackupManifest,
        staging: File,
    ) {
        val unknown = manifest.sections.filter { BackupSection.fromId(it) == null }
        if (unknown.isNotEmpty()) fail(BackupProblem.DAMAGED, "unknown sections $unknown")
        val sections = manifest.knownSections()
        for (entry in manifest.entries) {
            val section =
                BackupFormat.sectionOf(entry.path)
                    ?: fail(BackupProblem.UNSAFE_PATH, "'${entry.path}' is outside what a backup may restore")
            if (section !in sections) fail(BackupProblem.DAMAGED, "'${entry.path}' belongs to a section not listed")
            if (section.prefsFiles.any { BackupFormat.prefsPath(it) == entry.path }) {
                PrefsJson.decode(File(staging, entry.path).readText())
            }
        }
    }

    /**
     * Copies [input] to [out] and returns the byte count and lowercase hex SHA-256. Fails with
     * TOO_LARGE as soon as more than [budget] bytes have passed, before they reach the disk.
     */
    internal fun copyHashed(
        input: InputStream,
        out: OutputStream,
        budget: Long,
    ): Pair<Long, String> {
        val digest = MessageDigest.getInstance("SHA-256")
        val buffer = ByteArray(BUFFER_BYTES)
        var size = 0L
        while (true) {
            val n = input.read(buffer)
            if (n < 0) break
            size += n
            if (size > budget) fail(BackupProblem.TOO_LARGE, "over the size cap")
            digest.update(buffer, 0, n)
            out.write(buffer, 0, n)
        }
        return size to digest.digest().joinToString("") { (it.toInt() and 0xff).toString(16).padStart(2, '0') }
    }

    private fun readBounded(
        input: InputStream,
        max: Int,
    ): ByteArray {
        val out = ByteArrayOutputStream()
        copyHashed(input, out, max.toLong())
        return out.toByteArray()
    }
}
