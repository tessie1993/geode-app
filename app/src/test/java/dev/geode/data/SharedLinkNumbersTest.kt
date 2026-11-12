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
 * It reads unquoted `NaN` as a string and falls back, so NaN is covered through [PresetStore.finiteOr].
 */
class SharedLinkNumbersTest {
    init {
        TestCatalogue.install()
    }

    @Test
    fun finiteOrRejectsNaNInfinityAndFloatOverflow() {
        assertEquals(0.5f, PresetStore.finiteOr(Double.NaN, 0.5f))
        assertEquals(0.5f, PresetStore.finiteOr(Double.POSITIVE_INFINITY, 0.5f))
        assertEquals(0.5f, PresetStore.finiteOr(Double.NEGATIVE_INFINITY, 0.5f))
        assertEquals(0.5f, PresetStore.finiteOr(1e39, 0.5f))
        assertEquals(0.5f, PresetStore.finiteOr(-1e39, 0.5f))
        assertEquals(2.25f, PresetStore.finiteOr(2.25, 0.5f))
    }

    @Test
    fun finiteInClampsIntoTheSliderRange() {
        assertEquals(PresetStore.ATTACK_RANGE.start, PresetStore.finiteIn(Double.NaN, PresetStore.ATTACK_RANGE))
        assertEquals(PresetStore.DECAY_RANGE.endInclusive, PresetStore.finiteIn(40.0, PresetStore.DECAY_RANGE))
        assertEquals(0.3f, PresetStore.finiteIn(0.3, PresetStore.ATTACK_RANGE))
    }

    @Test
    fun presetWithOverflowingNumbersDecodesToDefaultsAndSavesAgain() {
        val json = """{"name":"x","sceneId":"silk","attack":1e39,"decay":-1e999,"zoom":1e39,"speed":1e999,"warp":-1e999}"""
        val preset = PresetStore.fromJson(json)
        assertEquals(SceneParams.DEFAULT.zoom, preset.params.zoom)
        assertEquals(SceneParams.DEFAULT.speed, preset.params.speed)
        assertEquals(SceneParams.DEFAULT.warp, preset.params.warp)
        assertEquals(PresetStore.ATTACK_RANGE.start, preset.attack)
        assertEquals(PresetStore.DECAY_RANGE.start, preset.decay)
        // Writing it back out is what threw before.
        assertEquals(preset.params.zoom, PresetStore.fromJson(PresetStore.toJson(preset)).params.zoom)
    }

    @Test
    fun templateWithOverflowingNumbersParses() {
        val json =
            """
            {"format":"${TemplateFormat.FORMAT}","name":"t",
             "look":{"attack":1e39,"decay":1e999,"params":{"zoom":1e39,"bloom":-1e999}},
             "layout":{"artworkScale":1e39,"safeArea":1e999},
             "text":{"slots":[{"sizeSp":1e39,"offsetX":-1e999}]}}
            """.trimIndent()
        val parsed = TemplateFormat.decode(json)
        assertTrue(parsed.toString(), parsed is TemplateParse.Parsed)
        val template = (parsed as TemplateParse.Parsed).template
        assertEquals(SceneParams.DEFAULT.zoom, template.look.params.zoom)
        assertEquals(SceneParams.DEFAULT.bloom, template.look.params.bloom)
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
