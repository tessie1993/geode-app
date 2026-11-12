package dev.geode.data

import dev.geode.editor.ProjectId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

/**
 * Where a project's folder is, whatever its id says, and that a delete reaches that folder alone. The file stem rule
 * itself is native (`core/tests/look_files_test.cpp` holds these same hostile ids to it); a JVM test has no libgeode,
 * so a stand-in that keeps a plain id and renames any other stands in for it here.
 */
class EditorProjectStoreFoldersTest {
    init {
        PresetStore.fileStem = ::standInStem
    }

    private fun standInStem(id: String): String {
        val plain = id.isNotEmpty() && id.all { it in 'a'..'z' || it in '0'..'9' }
        return if (plain) id else "id-${id.hashCode().toUInt()}"
    }

    @get:Rule
    val tmp = TemporaryFolder()

    private val projects by lazy { File(tmp.root, EditorProjectStore.PROJECTS_DIR) }

    private fun store() = EditorProjectStore(projects, File(tmp.root, EditorProjectStore.TEMPLATES_DIR))

    private fun project(id: String): File = File(projects, "$id/project.json").apply { parentFile?.mkdirs() }.also { it.writeText("{}") }

    @Test
    fun deleteRemovesTheProjectFolder() {
        project("p1")
        assertTrue(store().delete(ProjectId("p1")))
        assertFalse(File(projects, "p1").exists())
    }

    /**
     * A project's id comes from its project.json (a restored backup zip, an older app), so it can be any
     * string. Whatever it is, its folder must be one folder directly under projects, never projects itself
     * or anything above it, where delete would remove every project.
     */
    @Test
    fun everyIdGetsItsOwnFolderInsideTheRoot() {
        val store = store()
        val root = projects.canonicalFile
        listOf("", ".", "..", "../x", "a/b", "a.b", "проект", " ").forEach { raw ->
            val folder = store.folderOf(ProjectId(raw)).canonicalFile
            assertEquals("id \"$raw\" must name a folder directly under projects", root, folder.parentFile)
        }
    }

    @Test
    fun deletingAnOddIdLeavesOtherProjects() {
        val kept = project("p1")
        listOf("", ".", "..", "../p1", "a/b").forEach { raw -> store().delete(ProjectId(raw)) }
        assertTrue(kept.isFile)
    }
}
