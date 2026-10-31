package dev.geode.data

import dev.geode.render.scene.SceneParams
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * A shared preset or template link is untrusted text. Numbers too big for a float, or the
 * strings "NaN" and "Infinity", used to survive decoding and crash the next JSON write.
 */
class SharedLinkNumbersTest {
    @Test
    fun presetOutOfRangeOptionalNumbersReadAsDefaults() {
        val preset =
            PresetStore.fromJson(
                """{"name":"x","sceneId":"bars","attack":0.5,"decay":0.5,"speed":1e39,"zoom":"NaN","camFov":"-Infinity"}""",
            )
        assertEquals(1f, preset.params.speed)
        assertEquals(1f, preset.params.zoom)
        assertEquals(SceneParams.DEFAULT.camFov, preset.params.camFov)
        // Round-trips: this is the write that used to throw.
        PresetStore.toJson(preset)
    }

    @Test(expected = org.json.JSONException::class)
    fun presetNonFiniteRequiredNumberIsMalformed() {
        PresetStore.fromJson("""{"name":"x","sceneId":"bars","attack":1e39,"decay":0.5}""")
    }

    @Test
    fun templateWithNonFiniteNumbersDecodesWithoutThrowing() {
        val parse =
            TemplateFormat.decode(
                """{"format":"${TemplateFormat.FORMAT}","look":{"attack":1e39,"decay":"NaN","params":{"speed":1e39}}}""",
            )
        assertTrue(parse is TemplateParse.Parsed || parse is TemplateParse.Malformed)
    }
}
