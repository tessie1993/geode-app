package dev.geode.render.scene

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/** The families as the shell reads them from the catalogue, held against the files they name. */
class StyleFamiliesTest {
    /** The repo root: Gradle runs a module's tests from the module directory. */
    private val root =
        generateSequence(File("").absoluteFile) { it.parentFile }
            .first { File(it, "settings.gradle.kts").exists() }

    @Test
    fun everyFamilyHasItsOwnCountOfStylesUnderItsOwnPrefixOpenerFirst() {
        assertEquals(
            listOf(
                "arbor",
                "bulb",
                "chrysanthemum",
                "lucid",
                "dive",
                "glass",
                "nebula",
                "terrain",
                "truchet",
                "foam",
                "flame",
                "opal",
                "hollow",
            ),
            StyleFamilies.all.map { it.name },
        )
        // The first ten families hold twelve styles, the ones after them ten.
        val tens = setOf("flame", "opal", "hollow")
        StyleFamilies.all.forEach { family ->
            assertEquals(family.name, if (family.name in tens) 10 else 12, family.ids.toSet().size)
            assertEquals(family.ids.first(), family.opener)
            family.ids.forEach { assertTrue(it, it.startsWith(family.prefix)) }
        }
        assertEquals("arbor_grove", StyleFamilies.all.first().opener)
    }

    @Test
    fun everyStyleIsAShaderSceneWithAnAsset() {
        StyleFamilies.ids.forEach { id ->
            val asset = SceneCapabilities.SHADER_SCENES[id]
            assertEquals(id, "shaders/${id}_frag.glsl", asset)
            assertTrue("$id has no shader file", File(root, "app/src/main/assets/$asset").isFile)
            assertEquals(id, SceneKind.SHADER, SceneCapabilities.kindOf(id))
        }
    }

    @Test
    fun everyFamilyLibraryIsOnTheNativeIncludeAllowList() {
        val allowList = File(root, "core/viz/ShaderIncludes.inc").readText()
        StyleFamilies.all.forEach { family ->
            assertTrue(family.library, "\"${family.library}\"" in allowList)
            assertTrue(File(root, "app/src/main/assets/shaders/${family.library}.glsl").isFile)
        }
    }
}
