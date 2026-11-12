package dev.geode.ui

import dev.geode.model.get
import dev.geode.model.with
import dev.geode.render.scene.ParamSchema
import dev.geode.render.scene.SceneParams
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * Customize's screens name scene params as text (`model/ParamEdit`), so a misspelt name would only throw when its
 * control first draws. This reads the names their controls pass from the sources and holds each to the params
 * schema fixture (`geode.paramsSchema`), so the typo fails here instead.
 */
class ParamNamesTest {
    private val screens = listOf("CustomizeAdvanced", "CustomizeControls", "CustomizeTabs", "PaletteStack")

    // The name argument of LabeledSlider(key, p, "name"), ChoiceChips(key, "name", p), NoteKeySlider(title, hint, p, "name"),
    // ControlRange.of("name") and ParamSchema.choices("name").
    private val call =
        Regex(
            """(?:Labeled(?:Int)?Slider\(\w+, \w+, |ChoiceChips\([\w.]+, |NoteKeySlider\([\w.]+, [\w.]+, \w+, """ +
                """|ControlRange\.of\(|ParamSchema\.choices\()"(\w+)"""",
        )

    private fun names(): List<String> {
        val root = listOf(File("src/main/java/dev/geode/ui"), File("app/src/main/java/dev/geode/ui")).first { it.isDirectory }
        val passed = screens.flatMap { screen -> call.findAll(File(root, "$screen.kt").readText()).map { it.groupValues[1] }.toList() }
        return passed + LAYER_AMOUNTS
    }

    @Test
    fun everyNameTheScreensPassIsALiveSchemaRow() {
        val names = names()
        assertTrue("found only ${names.size} names; did the controls' call shapes change?", names.size > 100)
        val missing = names.filterNot { ParamSchema[it]?.retired == false }
        assertEquals("names with no live params schema row", emptyList<String>(), missing)
    }

    @Test
    fun aKeyedEditIsTheFieldsCopy() {
        val p = SceneParams.DEFAULT.copy(customPaletteId = "kept")
        assertEquals(p.copy(noteDrums = 1.5f), p.with("noteDrums", 1.5f))
        assertEquals(1.5f, p.with("noteDrums", 1.5f)["noteDrums"], 0f)
    }
}
