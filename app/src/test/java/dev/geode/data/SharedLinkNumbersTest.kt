package dev.geode.data

import dev.geode.render.scene.SceneParams
import dev.geode.render.scene.TestCatalogue
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * A `geode://preset` or `geode://template` link is a JSON document anyone can write. Numbers that
 * are not finite as a Float (NaN, the infinities, `1e39`) used to reach `JSONObject.put` and throw,
 * crashing the app on open. The test org.json is the real one, whose `put` throws the same way.
 * A preset's envelope and its overflow cases are native's (`core/tests/look_files_test.cpp`), as are the scene params
 * object's (`core/tests/params_json_test.cpp`); here a template's numbers, with a stand-in params object.
 */
class SharedLinkNumbersTest {
    init {
        TestCatalogue.install()
        TemplateFormat.writeParams = { "{}" }
        TemplateFormat.readParams = { _, _, _ -> SceneParams.DEFAULT }
    }

    @Test
    fun templateWithOverflowingNumbersParses() {
        val json =
            """
            {"format":"${TemplateFormat.FORMAT}","name":"t",
             "look":{"attack":1e39,"decay":1e999},
             "layout":{"artworkScale":1e39,"safeArea":1e999},
             "text":{"slots":[{"sizeSp":1e39,"offsetX":-1e999}]}}
            """.trimIndent()
        val parsed = TemplateFormat.decode(json)
        assertTrue(parsed.toString(), parsed is TemplateParse.Parsed)
        val template = (parsed as TemplateParse.Parsed).template
        assertEquals(TemplateLook.DEFAULT_ATTACK, template.look.attack)
        assertEquals(TemplateLayout.DEFAULT_ARTWORK_SCALE, template.layout.artworkScale)
        assertEquals(
            TextSlot.DEFAULT_SIZE_SP,
            template.text.slots
                .single()
                .sizeSp,
        )
        // And it encodes again, which is where `put` threw.
        assertTrue(TemplateFormat.decode(TemplateFormat.encode(template)) is TemplateParse.Parsed)
    }
}
