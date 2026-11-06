package dev.geode.data

import dev.geode.editor.ProjectId
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

/**
 * #384: a project's takes live in `project-takes/<id>/`, beside the project folder, so cloud backup
 * can leave them out by one fixed path. These pin where takes are written and that a project's
 * delete and size see them there. Moving older takes out is native: core/tests/studio_files_test.cpp.
 */
class EditorProjectStoreTakesTest {
    @get:Rule
    val tmp = TemporaryFolder()

    private val files by lazy { tmp.root }
    private val projects by lazy { File(files, EditorProjectStore.PROJECTS_DIR) }
    private val takes by lazy { File(files, EditorProjectStore.TAKES_DIR) }

    private fun store() = EditorProjectStore(projects, File(files, EditorProjectStore.TEMPLATES_DIR))

    private val id = ProjectId("p1")

    @Test
    fun aTakeIsSavedBesideTheProjectNotInsideIt() {
        val store = store()
        assertTrue(store.saveTake(id, "t1", "{\"take\":1}"))

        assertTrue(File(takes, "p1/t1.json").isFile)
        assertFalse(File(projects, "p1/takes").exists())
        assertEquals("{\"take\":1}", store.loadTake(id, "t1"))
    }

    @Test
    fun deleteRemovesTheProjectAndItsTakes() {
        val store = store()
        File(projects, "p1").mkdirs()
        File(projects, "p1/project.json").writeText("{}")
        store.saveTake(id, "t1", "{}")

        assertTrue(store.delete(id))
        assertFalse(File(projects, "p1").exists())
        assertFalse(File(takes, "p1").exists())
        assertNull(store.loadTake(id, "t1"))
    }

    @Test
    fun sizeCountsTheProjectFileAndItsTakes() {
        val store = store()
        File(projects, "p1").mkdirs()
        File(projects, "p1/project.json").writeText("12345")
        store.saveTake(id, "t1", "1234567890")

        assertEquals(15L, store.sizeOf(id))
    }

    /**
     * A project's id comes from its project.json (a restored backup zip, an older app), so it can be any
     * string. Whatever it is, its takes folder must be one folder directly under project-takes, never
     * project-takes itself or anything above it. Kills an id used raw as the folder name ("..", "a/b") and
     * an empty id resolving to the root, where delete would remove every project's takes.
     */
    @Test
    fun everyIdGetsItsOwnTakesFolderInsideTheRoot() {
        val store = store()
        val root = takes.canonicalFile
        listOf("", ".", "..", "../x", "a/b", "a.b", "проект", " ").forEach { raw ->
            val folder = store.takesFolderOf(ProjectId(raw)).canonicalFile
            assertEquals("id \"$raw\" must name a folder directly under project-takes", root, folder.parentFile)
        }
    }

    /** Deleting a project with an odd id (empty, dots, a slash) never reaches another project's takes. */
    @Test
    fun deletingAnOddIdLeavesOtherProjectsTakes() {
        val store = store()
        store.saveTake(id, "t1", "{\"take\":1}")

        listOf("", ".", "..", "../p1", "a/b").forEach { raw -> store.delete(ProjectId(raw)) }

        assertEquals("{\"take\":1}", store.loadTake(id, "t1"))
    }
}
