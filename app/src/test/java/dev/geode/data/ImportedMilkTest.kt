package dev.geode.data

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class ImportedMilkTest {
    @get:Rule val tmp = TemporaryFolder()

    private val milkDir: File get() = File(tmp.root, "milk").apply { mkdirs() }

    @Test
    fun anImportedPresetInTheFolderIsDeleted() {
        val preset = File(milkDir, "a.milk").apply { writeText("[preset00]") }
        assertTrue(ImportedMilk.remove(milkDir, preset.path))
        assertFalse(preset.exists())
    }

    @Test
    fun nothingOutsideTheFolderOrNotAMilkIsTouched() {
        val outside = File(tmp.root, "b.milk").apply { writeText("x") }
        val nested = File(File(milkDir, "textures").apply { mkdirs() }, "c.milk").apply { writeText("x") }
        val other = File(milkDir, "texture-links.json").apply { writeText("{}") }
        val escape = File(milkDir, "../b.milk").path
        listOf(outside.path, nested.path, other.path, escape).forEach { assertFalse(ImportedMilk.remove(milkDir, it)) }
        listOf(outside, nested, other).forEach { assertTrue(it.exists()) }
    }
}
