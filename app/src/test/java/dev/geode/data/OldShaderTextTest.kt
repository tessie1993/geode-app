package dev.geode.data

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The app compiles no user GLSL. Presets, templates, `geode://` links and backups saved by older
 * builds can still hold a `customShader` field; they must load as before, minus that field, and
 * never write it back out, so shared files cannot carry shader text on through this app.
 */
class OldShaderTextTest {
    @Test
    fun anOldPresetWithShaderTextLoadsWithoutIt() {
        val json = """{"name":"old","sceneId":"julia","attack":0.1,"decay":0.2,"zoom":1.25,"customShader":"void main() {}"}"""
        val preset = PresetStore.fromJson(json)
        assertEquals("old", preset.name)
        assertEquals("julia", preset.sceneId)
        assertEquals(1.25f, preset.params.zoom)
        assertFalse(JSONObject(PresetStore.toJson(preset)).has("customShader"))
    }

    @Test
    fun anOldTemplateWithShaderTextLoadsWithoutItAndKeepsItsOtherFields() {
        val json =
            """
            {"format":"${TemplateFormat.FORMAT}","name":"t",
             "look":{"sceneId":"julia","params":{"zoom":1.25},"customShader":"void main() {}","laterKey":7}}
            """.trimIndent()
        val parsed = TemplateFormat.decode(json)
        assertTrue(parsed.toString(), parsed is TemplateParse.Parsed)
        val template = (parsed as TemplateParse.Parsed).template
        assertEquals("julia", template.look.sceneId)
        assertEquals(1.25f, template.look.params.zoom)
        val look = JSONObject(TemplateFormat.encode(template)).getJSONObject("look")
        assertFalse(look.has("customShader"))
        assertEquals(7, look.getInt("laterKey"))
    }
}
