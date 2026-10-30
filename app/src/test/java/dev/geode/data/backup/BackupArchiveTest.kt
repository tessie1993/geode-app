package dev.geode.data.backup

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayOutputStream
import java.io.File
import java.security.MessageDigest
import java.time.LocalDate
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/**
 * A backup is a file from outside the app that a restore writes into app data, so the checks
 * that stand between the two are what is tested: each one must stop the restore before a byte
 * leaves staging, and must say which problem it found.
 */
class BackupArchiveTest {
    @get:Rule
    val temp = TemporaryFolder()

    private val presetBytes = """{"name":"Kyanite"}""".toByteArray()

    @Test
    fun `manifest survives encode then decode`() {
        val manifest = manifest(listOf(entry("presets/a.json", presetBytes)))
        assertEquals(manifest, BackupManifest.decode(manifest.encode()))
    }

    @Test
    fun `pack then unpack gives back the same files`() {
        val prefs = PrefsJson.encode(mapOf("gui_opacity" to 0.4f)).toByteArray()
        val out = ByteArrayOutputStream()
        BackupArchive.pack(
            out,
            listOf(
                BackupSource("presets/a.json") { presetBytes.inputStream() },
                BackupSource("prefs/geode-prefs.json") { prefs.inputStream() },
                BackupSource("presets/gone.json") { null },
            ),
            BackupSection.entries,
            appVersionCode = 33,
            createdAtMs = 1_000L,
        )
        val archive = temp.newFile("backup.zip").apply { writeBytes(out.toByteArray()) }
        val staging = temp.newFolder("staging")

        val manifest = BackupArchive.unpack(archive, staging)

        assertEquals(listOf("presets/a.json", "prefs/geode-prefs.json"), manifest.entries.map { it.path })
        assertEquals(BackupSection.entries.toSet(), manifest.knownSections())
        assertTrue(File(staging, "presets/a.json").readBytes().contentEquals(presetBytes))
        assertTrue(File(staging, "prefs/geode-prefs.json").readBytes().contentEquals(prefs))
    }

    @Test
    fun `a newer version is reported as newer before its paths are looked at`() {
        val future = manifest(listOf(entry("visuals-v2/x.json", presetBytes)), version = BackupFormat.CURRENT_VERSION + 1)
        val archive = zip(future, "visuals-v2/x.json" to presetBytes)
        assertProblem(BackupProblem.NEWER_VERSION) { BackupArchive.unpack(archive, temp.newFolder()) }
    }

    @Test
    fun `a wrong format id is not a backup`() {
        val other = manifest(emptyList()).copy(format = "some-other-app")
        assertProblem(BackupProblem.NOT_A_BACKUP) { BackupArchive.unpack(zip(other), temp.newFolder()) }
    }

    @Test
    fun `a zip without a manifest or a file that is not a zip is not a backup`() {
        val noManifest = temp.newFile("plain.zip")
        ZipOutputStream(noManifest.outputStream()).use { it.putNextEntry(ZipEntry("a.txt")) }
        assertProblem(BackupProblem.NOT_A_BACKUP) { BackupArchive.unpack(noManifest, temp.newFolder()) }
        val text = temp.newFile("notes.zip").apply { writeText("hello") }
        assertProblem(BackupProblem.NOT_A_BACKUP) { BackupArchive.unpack(text, temp.newFolder()) }
    }

    @Test
    fun `zip-slip and absolute paths are rejected and nothing lands outside staging`() {
        val root = temp.newFolder("root")
        val staging = File(root, "staging").apply { mkdirs() }
        for (path in listOf("../escaped.json", "presets/../../escaped.json", "/tmp/escaped.json")) {
            val archive = zip(manifest(listOf(entry(path, presetBytes))), path to presetBytes)
            assertProblem(BackupProblem.UNSAFE_PATH) { BackupArchive.unpack(archive, staging) }
        }
        assertFalse(File(root, "escaped.json").exists())
    }

    @Test
    fun `path shapes`() {
        assertTrue(BackupFormat.isSafePath("presets/a.json"))
        for (bad in listOf("", "/a", "a//b", "a/./b", "a/../b", "..", "a\\b", "C:/a", "presets/")) {
            assertFalse(bad, BackupFormat.isSafePath(bad))
        }
    }

    @Test
    fun `paths outside the allowlist are rejected`() {
        for (path in listOf("takes/t.json", "analysis/a.bin", "crash-latest.txt", "milk/generated/g.png", "milk-builtin/a.milk")) {
            val archive = zip(manifest(listOf(entry(path, presetBytes))), path to presetBytes)
            assertProblem(BackupProblem.UNSAFE_PATH) { BackupArchive.unpack(archive, temp.newFolder()) }
        }
    }

    @Test
    fun `the allowlist maps paths to their sections`() {
        assertEquals(BackupSection.CREATIVE, BackupFormat.sectionOf("presets/a.json"))
        assertEquals(BackupSection.CREATIVE, BackupFormat.sectionOf("prefs/geode-mod.json"))
        assertEquals(BackupSection.MUSIC, BackupFormat.sectionOf("history.json"))
        assertEquals(BackupSection.MUSIC, BackupFormat.sectionOf("prefs/geode-favourites.json"))
        assertEquals(BackupSection.MILK, BackupFormat.sectionOf("milk/textures/cloud.png"))
        assertEquals(BackupSection.SETTINGS, BackupFormat.sectionOf("prefs/geode-prefs.json"))
        assertNull(BackupFormat.sectionOf("milk/textures/.links/p/cloud.png"))
        assertNull(BackupFormat.sectionOf("presets/a.json.tmp"))
        assertNull(BackupFormat.sectionOf("prefs/geode-widget.json"))
        assertNull(BackupFormat.sectionOf("session.json"))
    }

    @Test
    fun `a checksum mismatch is damage`() {
        val promised = entry("presets/a.json", presetBytes)
        val archive = zip(manifest(listOf(promised)), "presets/a.json" to "tampered".toByteArray())
        assertProblem(BackupProblem.DAMAGED) { BackupArchive.unpack(archive, temp.newFolder()) }
    }

    @Test
    fun `a file missing from the zip or extra to the manifest is damage`() {
        val missing = zip(manifest(listOf(entry("presets/a.json", presetBytes))))
        assertProblem(BackupProblem.DAMAGED) { BackupArchive.unpack(missing, temp.newFolder()) }
        val extra = zip(manifest(emptyList()), "presets/a.json" to presetBytes)
        assertProblem(BackupProblem.DAMAGED) { BackupArchive.unpack(extra, temp.newFolder()) }
    }

    @Test
    fun `oversize input is rejected by bytes and by entry count`() {
        val big = ByteArray(4096)
        val archive = zip(manifest(listOf(entry("presets/big.bin", big))), "presets/big.bin" to big)
        assertProblem(BackupProblem.TOO_LARGE) {
            BackupArchive.unpack(archive, temp.newFolder(), BackupLimits(maxTotalBytes = 1024))
        }
        val files = (1..3).map { "presets/$it.json" to presetBytes }
        val many = zip(manifest(files.map { (p, b) -> entry(p, b) }), *files.toTypedArray())
        assertProblem(BackupProblem.TOO_LARGE) {
            BackupArchive.unpack(many, temp.newFolder(), BackupLimits(maxEntries = 2))
        }
    }

    @Test
    fun `migrations run in order up to the target and a gap is damage`() {
        val root = temp.newFolder()
        val applied = mutableListOf<Int>()
        val steps =
            listOf(
                BackupMigration(2) { m, _ -> m.also { applied += 2 } },
                BackupMigration(1) { m, _ -> m.copy(sections = m.sections + "renamed").also { applied += 1 } },
            )
        val migrated = BackupMigrations.migrate(manifest(emptyList()), root, steps, target = 3)
        assertEquals(listOf(1, 2), applied)
        assertEquals(3, migrated.version)
        assertTrue("renamed" in migrated.sections)

        assertEquals(manifest(emptyList()), BackupMigrations.migrate(manifest(emptyList()), root))
        assertProblem(BackupProblem.DAMAGED) { BackupMigrations.migrate(manifest(emptyList()), root, steps.drop(1), target = 3) }
    }

    @Test
    fun `prefs keep their types through JSON`() {
        val values =
            mapOf(
                "s" to "text",
                "i" to 7,
                "l" to 8_000_000_000L,
                "small_long" to 3L,
                "f" to 0.1f,
                "b" to true,
                "set" to setOf("content://a", "content://b"),
            )
        val back = PrefsJson.decode(PrefsJson.encode(values))
        assertEquals(values, back)
        assertTrue(back["small_long"] is Long)
        assertTrue(back["f"] is Float)
    }

    @Test
    fun `an unparseable prefs file in a backup is damage`() {
        val bad = "not json".toByteArray()
        val archive = zip(manifest(listOf(entry("prefs/geode-prefs.json", bad))), "prefs/geode-prefs.json" to bad)
        assertProblem(BackupProblem.DAMAGED) { BackupArchive.unpack(archive, temp.newFolder()) }
    }

    @Test
    fun `device-local keys stay out`() {
        assertFalse(BackupFormat.isPortableKey("geode-prefs", "gui_tutorial_seen"))
        assertFalse(BackupFormat.isPortableKey("geode-library", "roots"))
        assertTrue(BackupFormat.isPortableKey("geode-prefs", "gui_reduced_motion"))
    }

    @Test
    fun `default file name carries the date`() {
        assertEquals("muzviz-backup-20261030.zip", BackupFormat.defaultFileName(LocalDate.of(2026, 10, 30)))
    }

    private fun entry(
        path: String,
        bytes: ByteArray,
    ) = BackupEntry(path, bytes.size.toLong(), sha256(bytes))

    private fun manifest(
        entries: List<BackupEntry>,
        version: Int = BackupFormat.CURRENT_VERSION,
    ) = BackupManifest(BackupFormat.FORMAT_ID, version, 33, 1_000L, BackupSection.entries.map { it.id }, entries)

    private fun zip(
        manifest: BackupManifest,
        vararg files: Pair<String, ByteArray>,
    ): File {
        val file = temp.newFile()
        ZipOutputStream(file.outputStream()).use { zip ->
            for ((path, bytes) in files) {
                zip.putNextEntry(ZipEntry(path))
                zip.write(bytes)
                zip.closeEntry()
            }
            zip.putNextEntry(ZipEntry(BackupFormat.MANIFEST_PATH))
            zip.write(manifest.encode().toByteArray())
            zip.closeEntry()
        }
        return file
    }

    private fun sha256(bytes: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { (it.toInt() and 0xff).toString(16).padStart(2, '0') }

    private fun assertProblem(
        expected: BackupProblem,
        block: () -> Unit,
    ) {
        try {
            block()
            fail("expected $expected")
        } catch (e: BackupException) {
            assertEquals(e.message, expected, e.problem)
        }
    }
}
