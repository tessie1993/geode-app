package dev.geode.render.scene

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class StyleFamiliesTest {
    /** The repo root: Gradle runs a module's tests from the module directory. */
    private val root =
        generateSequence(File("").absoluteFile) { it.parentFile }
            .first { File(it, "settings.gradle.kts").exists() }

    @Test
    fun everyFamilyHasTwelveStylesUnderItsOwnPrefix() {
        StyleFamilies.all.forEach { family ->
            assertEquals(family.name, 12, family.ids.size)
            assertEquals(family.name, 12, family.ids.toSet().size)
            family.ids.forEach { assertTrue(it, it.startsWith(family.prefix)) }
        }
        assertEquals(StyleFamilies.ids.size, StyleFamilies.ids.toSet().size)
    }

    @Test
    fun theFirstStyleIsTheFreeOneAndTheRestArePro() {
        StyleFamilies.all.forEach { family ->
            assertEquals(family.ids.first(), family.freeId)
            assertEquals(family.ids.drop(1), family.proIds)
            assertTrue(family.freeId !in StyleFamilies.proIds)
        }
        assertEquals(StyleFamilies.all.size * 11, StyleFamilies.proIds.size)
    }

    @Test
    fun everyStyleIsAShaderSceneWithAnAssetAndAMarchedSubset() {
        StyleFamilies.ids.forEach { id ->
            val asset = SceneCapabilities.SHADER_SCENES[id]
            assertEquals(id, "shaders/${id}_frag.glsl", asset)
            assertTrue("$id has no shader file", File(root, "app/src/main/assets/$asset").isFile)
            assertEquals(id, SceneKind.SHADER, SceneCapabilities.kindOf(id))
        }
        assertTrue(StyleFamilies.ids.containsAll(StyleFamilies.marchedIds))
        assertTrue(SceneCapabilities.MARCHED_SCENES.containsAll(StyleFamilies.marchedIds))
    }

    @Test
    fun theNativeRegistryKnowsEveryShaderScene() {
        // core/viz/SceneRegistry.cpp keeps its own copy of the id list; a style missing there
        // would be offered in the picker and then fall back to the default on screen.
        val native = File(root, "core/viz/SceneRegistry.cpp").readText()
        val block = native.substringAfter("kShaderIds").substringBefore("};")
        val listed = Regex("\"(\\w+)\"").findAll(block).map { it.groupValues[1] }.toList()
        assertEquals(SceneCapabilities.SHADER_SCENES.keys.toList(), listed)
    }

    @Test
    fun everyFamilyLibraryIsOnTheNativeIncludeAllowList() {
        val allowList = File(root, "core/viz/ShaderSource.cpp").readText()
        StyleFamilies.all.forEach { family ->
            assertTrue(family.library, "\"${family.library}\"" in allowList)
            assertTrue(File(root, "app/src/main/assets/shaders/${family.library}.glsl").isFile)
        }
    }
}
