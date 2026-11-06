package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element
import java.io.File
import javax.xml.parsers.DocumentBuilderFactory

/**
 * Pins the Android backup rules (`res/xml/backup_rules.xml` for Android 11 and below,
 * `data_extraction_rules.xml` for 12+), which the XML comments name as this test.
 *
 * Cloud backup must leave out what can blow its ~25 MB quota (imported textures, every project's
 * takes), because past the quota Android silently stops backing up everything, presets and history
 * included. It must keep `projects`, whose small `project.json` files are My videos' list. A direct
 * phone-to-phone transfer has no quota, so it drops only the crash file (#384).
 */
class BackupRulesCoverageTest {
    private fun xml(name: String): Element {
        // Unit tests run with the module directory as the working directory.
        val file = listOf(File("src/main/res/xml/$name"), File("app/src/main/res/xml/$name")).first { it.isFile }
        return DocumentBuilderFactory
            .newInstance()
            .newDocumentBuilder()
            .parse(file)
            .documentElement
    }

    private fun Element.excludedPaths(): Set<String> {
        val nodes = getElementsByTagName("exclude")
        return (0 until nodes.length)
            .map { nodes.item(it) as Element }
            .filter { it.getAttribute("domain") == "file" }
            .map { it.getAttribute("path") }
            .toSet()
    }

    private fun Element.child(tag: String): Element = getElementsByTagName(tag).item(0) as Element

    private val quotaExcludes =
        setOf(EditorProjectStore.TAKES_DIR, "milk/textures", "milk/generated", "crash-latest.txt")

    @Test
    fun autoBackupLeavesOutTakesAndTexturesButKeepsProjects() {
        val paths = xml("backup_rules.xml").excludedPaths()
        assertTrue(paths.containsAll(quotaExcludes))
        assertFalse(EditorProjectStore.PROJECTS_DIR in paths)
    }

    @Test
    fun cloudBackupLeavesOutTakesAndTexturesButKeepsProjects() {
        val paths = xml("data_extraction_rules.xml").child("cloud-backup").excludedPaths()
        assertTrue(paths.containsAll(quotaExcludes))
        assertFalse(EditorProjectStore.PROJECTS_DIR in paths)
    }

    @Test
    fun bothCloudRuleSetsMatch() {
        assertEquals(
            xml("backup_rules.xml").excludedPaths(),
            xml("data_extraction_rules.xml").child("cloud-backup").excludedPaths(),
        )
    }

    @Test
    fun deviceTransferDropsOnlyTheCrashFile() {
        val paths = xml("data_extraction_rules.xml").child("device-transfer").excludedPaths()
        assertEquals(setOf("crash-latest.txt"), paths)
    }
}
