package dev.geode.data

import dev.geode.editor.AnimatableParams
import dev.geode.editor.ParamId
import dev.geode.export.ClipEdit
import dev.geode.render.AdsrConfig
import dev.geode.render.LfoConfig
import dev.geode.render.scene.ConfigSchema
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The stores' defaults and clamps now come from the config schema (core/viz/ConfigTable.inc, #423).
 * Each expectation here is the literal the Kotlin store held before the move, written out, so a change
 * to a row that would change a store's behaviour fails here and has to be made on purpose.
 */
class StoreConfigSchemaTest {
    @Test
    fun lfoSlotsKeepTheirOldDefaultsAndRateRange() {
        // LfoConfig.DEFAULT_RATE_SECONDS, MIN_RATE_SECONDS, MAX_RATE_SECONDS and the depth default.
        assertEquals(2f, LfoConfig().rateSeconds)
        assertEquals(0.3f, LfoConfig().depth)
        assertEquals(0.05f..60f, ConfigSchema["lfo", "rateSeconds"].range)
        // Customize's depth slider.
        assertEquals(0f..1f, ConfigSchema["lfo", "depth"].range)
    }

    @Test
    fun envelopesKeepTheirOldDefaultsAndSliderRanges() {
        // AdsrConfig's defaults, which LfoStore.adsrFromJson repeated.
        val a = AdsrConfig()
        assertEquals(listOf(0.05f, 0.25f, 0.5f, 0.35f, 0.5f, 0.25f), listOf(a.attack, a.decay, a.sustain, a.release, a.amount, a.gateThreshold))
        // CustomizeAdvanced's envelope sliders.
        val ranges = listOf("attack", "decay", "sustain", "release", "amount", "gateThreshold").map { ConfigSchema["adsr", it].range }
        assertEquals(listOf(0.005f..1f, 0.01f..1.5f, 0f..1f, 0.02f..2f, 0f..1.5f, 0.05f..1f), ranges)
    }

    @Test
    fun macroKnobsAreSixFromMinusOneToOne() {
        // MacroStore.COUNT = 6 and coerceIn(-1f, 1f).
        assertEquals(6, MacroStore.COUNT)
        ConfigSchema.group("macro").forEach { assertEquals(it.name, -1f..1f, it.range) }
        assertEquals(MacroStore.COUNT, MacroState().knobs.size)
    }

    @Test
    fun paletteGradientKeepsItsDefaultsAndUnitClamp() {
        // PaletteStore's coerceIn(0f, 1f) and fromJson's defaults 0 and 1.
        assertEquals(0f, ConfigSchema["palette", "baseHue"].default)
        assertEquals(1f, ConfigSchema["palette", "hueSpan"].default)
        assertEquals(0f..1f, ConfigSchema["palette", "baseHue"].range)
        assertEquals(0f..1f, ConfigSchema["palette", "hueSpan"].range)
        assertEquals(CustomPalette("a", "a", 1f, 0f), PaletteStore.create("a", 3f, -2f))
    }

    @Test
    fun theLooksEnvelopeKeepsItsDefaultsAndRanges() {
        // TemplateLook.DEFAULT_ATTACK, DEFAULT_DECAY and PresetStore.ATTACK_RANGE, DECAY_RANGE.
        assertEquals(0.6f, TemplateLook().attack)
        assertEquals(0.12f, TemplateLook().decay)
        assertEquals(0.005f..1f, ConfigSchema["look", "attack"].range)
        assertEquals(0.01f..1.5f, ConfigSchema["look", "decay"].range)
    }

    @Test
    fun presetMorphKeepsItsDefaultAndCap() {
        // GuiPrefs.presetMorphSeconds = 2f, PRESET_MORPH_SECONDS_MAX = 8f.
        assertEquals(2f, GuiPrefs().presetMorphSeconds)
        assertEquals(0f..8f, ConfigSchema["theme", "presetMorphSeconds"].range)
    }

    @Test
    fun clipEditKeepsItsDefaultsAndKeyframeRanges() {
        val e = ClipEdit()
        assertEquals(listOf(0f, 0f, 0f, 0f, 1f, 0f), listOf(e.brightness, e.contrast, e.saturation, e.hueDegrees, e.speed, e.rotationDegrees))
        // AnimatableParams.clipTable's literal ranges.
        val want =
            mapOf(
                "brightness" to (-1f to 1f),
                "contrast" to (-1f to 1f),
                "saturation" to (-100f to 100f),
                "hueDegrees" to (-180f to 180f),
                "speed" to (0.25f to 4f),
                "rotationDegrees" to (-180f to 180f),
            )
        want.forEach { (field, range) ->
            val p = AnimatableParams.find(ParamId("clip.$field")) ?: error("no clip.$field")
            assertEquals(field, range, p.min to p.max)
        }
        assertEquals(want.size, AnimatableParams.clip.size)
    }
}
