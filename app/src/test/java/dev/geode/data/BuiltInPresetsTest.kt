package dev.geode.data

import dev.geode.render.scene.SceneParams
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Test

/**
 * The reader of `geode_catalogue_presets_text`. The rows below are shaped like the native table's (the
 * Chill look whole, the Chladni plate variant trimmed); what native ships, and that its text reads back bit for bit, is
 * core/tests/presets_test.cpp's job. Here: a look's values land on its params by name over the defaults.
 */
class BuiltInPresetsTest {
    private val text =
        listOf(
            "presets|1|2|3",
            "look|Chill|0.4|0.06|speed=0.5,audioDrive=0.85,trails=1,trailLength=0.8,saturation=0.8," +
                "brightness=0.9,palette=3,sway=0.35,vignette=0.4,paramFadeSec=1.5",
            "preset|julia · Chill|julia|Chill",
            "preset|lava · Chill|lava|Chill",
            "look|cymatics · Chladni plate|0.35|0.45|cymaticsGeometry=1,cymaticsFundamental=82,cymaticsModes=4," +
                "cymaticsScale=1.15,cymaticsSwirl=0,palette=4,hueRange=0.6",
            "preset|cymatics · Chladni plate|cymatics|cymatics · Chladni plate",
        ).joinToString("\n")

    @Test
    fun aLookIsItsValuesOverTheDefaults() {
        val chill = BuiltInPresets.parse(text)[0]
        assertEquals("julia · Chill", chill.name)
        assertEquals("julia", chill.sceneId)
        assertEquals(0.4f, chill.attack, 0f)
        assertEquals(0.06f, chill.decay, 0f)
        assertEquals(
            SceneParams.DEFAULT
                .with("speed", 0.5f)
                .with("audioDrive", 0.85f)
                .with("trails", true)
                .with("trailLength", 0.8f)
                .with("saturation", 0.8f)
                .with("brightness", 0.9f)
                .with("palette", 3)
                .with("sway", 0.35f)
                .with("vignette", 0.4f)
                .with("paramFadeSec", 1.5f),
            chill.params,
        )
    }

    @Test
    fun presetsKeepTheTableOrderAndShareTheirLook() {
        val presets = BuiltInPresets.parse(text)
        assertEquals(listOf("julia · Chill", "lava · Chill", "cymatics · Chladni plate"), presets.map { it.name })
        assertSame(presets[0].params, presets[1].params)
        assertEquals(
            SceneParams.DEFAULT
                .with("cymaticsGeometry", 1)
                .with("cymaticsFundamental", 82f)
                .with("cymaticsModes", 4)
                .with("cymaticsScale", 1.15f)
                .with("cymaticsSwirl", 0f)
                .with("palette", 4)
                .with("hueRange", 0.6f),
            presets[2].params,
        )
        assertEquals(0.45f, presets[2].decay, 0f)
    }

    @Test(expected = IllegalStateException::class)
    fun anUnknownParamIsRefused() {
        BuiltInPresets.parse("presets|1|1|1\nlook|X|0.5|0.1|notAParam=1\npreset|x · X|julia|X")
    }
}
