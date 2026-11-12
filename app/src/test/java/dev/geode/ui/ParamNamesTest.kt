package dev.geode.ui

import dev.geode.render.scene.ParamSchema
import dev.geode.render.scene.SceneParams
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * Customize's screens name scene params as text (`SceneParams.get`/`with`), so a misspelt name would only throw when its
 * control first draws. This reads the names their controls pass from the sources and holds each to the params
 * schema fixture (`geode.paramsSchema`), so the typo fails here instead.
 */
class ParamNamesTest {
    // Customize's controls and the palette stack name their params natively (core/tests/customize_rows_test holds them).
    private val screens = listOf("CustomizeAdvanced", "CustomizeTabs")

    // The name argument of LabeledSlider(key, p, "name"), ChoiceChips(key, "name", p), NoteKeySlider(title, hint, p, "name"),
    // ControlRange.of("name") and ParamSchema.choices("name").
    private val call =
        Regex(
            """(?:Labeled(?:Int)?Slider\(\w+, \w+, |ChoiceChips\([\w.]+, |NoteKeySlider\([\w.]+, [\w.]+, \w+, """ +
                """|ControlRange\.of\(|ParamSchema\.choices\()"(\w+)"""",
        )

    private fun names(): List<String> {
        val root = listOf(File("src/main/java/dev/geode/ui"), File("app/src/main/java/dev/geode/ui")).first { it.isDirectory }
        return screens.flatMap { screen -> call.findAll(File(root, "$screen.kt").readText()).map { it.groupValues[1] }.toList() }
    }

    @Test
    fun everyNameTheScreensPassIsALiveSchemaRow() {
        val names = names()
        assertTrue("found no names; did the controls' call shapes change?", names.isNotEmpty())
        val missing = names.filterNot { ParamSchema[it]?.retired == false }
        assertEquals("names with no live params schema row", emptyList<String>(), missing)
    }

    @Test
    fun aKeyedEditIsTheFieldsCopy() {
        val p = SceneParams.DEFAULT.withPaletteId("kept")
        assertEquals("kept", p.with("noteDrums", 1.5f).customPaletteId)
        assertEquals(1.5f, p.with("noteDrums", 1.5f)["noteDrums"], 0f)
        assertEquals(SceneParams.DEFAULT["noteDrums"], p["noteDrums"], 0f)
    }
}
