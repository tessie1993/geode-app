package dev.geode.data

import dev.geode.render.bridge.SceneParamsCodec
import dev.geode.render.scene.ParamSchema
import dev.geode.render.scene.SceneParams
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Presets and shared links are a file format (ADR 0001 step 2), so these hold it to golden files in
 * `src/test/resources/presets/` now that the scene params are written and read by the native schema
 * ([ParamSchema]) instead of a hand list. The test org.json is the real one (see `libs.versions.toml`).
 *
 * - `current.json` is a preset with every key the pre-schema writer put, in its order, each set to a
 *   value off its default: it must read and write back to the same keys and values.
 * - `legacy.json` is an early preset: most keys missing, every retired key and an unknown key.
 * - `hostile.json` is a shared link out of range, non-finite and of the wrong type.
 */
class PresetGoldenTest {
    private fun fixture(name: String): String = checkNotNull(javaClass.getResource("/presets/$name")) { "missing fixture $name" }.readText()

    /** JSON equality: 240 and 240.0 are the same number. */
    private fun same(
        a: Any,
        b: Any,
    ): Boolean = if (a is Number && b is Number) a.toDouble() == b.toDouble() else a == b

    // The key set via keys(): the test compile classpath sees android.jar's org.json stub first, which has nothing newer.
    private fun JSONObject.keyNames(): Set<String> = keys().asSequence().toSet()

    @Test
    fun currentPresetRoundTripsToTheSameKeysAndValues() {
        val golden = JSONObject(fixture("current.json"))
        val written = JSONObject(PresetStore.toJson(PresetStore.fromJson(golden.toString())))
        assertEquals(golden.keyNames(), written.keyNames())
        for (key in golden.keyNames()) {
            assertTrue("$key: ${golden.get(key)} became ${written.get(key)}", same(golden.get(key), written.get(key)))
            // Booleans stay booleans, ints stay ints: the kind decides the JSON type, as before.
            assertEquals(key, golden.get(key) is Boolean, written.get(key) is Boolean)
        }
    }

    @Test
    fun currentPresetReadsEveryField() {
        val p = PresetStore.fromJson(fixture("current.json")).params
        // Every value in the fixture is off its default, so a key the reader missed would show here.
        val defaults = PresetStore.paramsToJson(SceneParams.DEFAULT)
        val read = PresetStore.paramsToJson(p)
        for (key in read.keyNames()) {
            assertFalse("$key read as its default", same(read.get(key), defaults.get(key)))
        }
        assertEquals("palette-a", p.customPaletteId)
        assertEquals("palette-b", p.customPalette2Id)
        assertEquals(12.5f, p.flowCurl)
    }

    @Test
    fun theWrittenKeysAreTheLiveSchemaNamesPlusThePaletteIds() {
        val keys = PresetStore.paramsToJson(SceneParams.DEFAULT).keyNames()
        val live = ParamSchema.params.filterNot { it.retired }
        val expected = live.map { it.name }.toSet() + setOf("customPaletteId", "customPalette2Id")
        assertEquals(expected, keys)
    }

    @Test
    fun aPresetWithOnlyItsEnvelopeReadsAsTheDefaults() {
        val p = PresetStore.fromJson("""{"name":"e","sceneId":"silk","attack":0.1,"decay":0.2}""")
        assertEquals(SceneParams.DEFAULT, p.params)
    }

    @Test
    fun legacyPresetFillsMissingKeysWithDefaultsAndDropsRetiredOnes() {
        val p = PresetStore.fromJson(fixture("legacy.json")).params
        assertEquals(1.5f, p.speed)
        assertEquals(1.25f, p.zoom)
        assertEquals(3, p.palette)
        assertEquals(8, p.symmetry)
        assertTrue(p.trails)
        assertTrue(p.kaleidoscope)
        assertEquals("old-palette", p.customPaletteId)
        // Retired and unknown keys are not read: the frame carries each retired row's default.
        val frame = SceneParamsCodec.pack(p)
        val retired = ParamSchema.params.filter { it.retired }
        assertEquals(setOf("pulse", "shake", "flash", "glitch", "formDrive", "motionOrbit"), retired.map { it.name }.toSet())
        for (spec in retired) assertEquals(spec.name, spec.default, frame[spec.slot])
        // The strobe is live and still read.
        assertEquals(0.5f, p.strobe)
        // Keys the old file never had come back as the schema's defaults.
        assertEquals(SceneParams.DEFAULT.flowCurl, p.flowCurl)
        assertEquals(SceneParams.DEFAULT.camFly, p.camFly)
        assertEquals(SceneParams.DEFAULT.fluidCatchPoints, p.fluidCatchPoints)
        assertEquals(SceneParams.DEFAULT.liquidBorn, p.liquidBorn)
        assertEquals(SceneParams.DEFAULT.noteDrums, p.noteDrums)
        val written = JSONObject(PresetStore.toJson(PresetStore.fromJson(fixture("legacy.json"))))
        for (spec in retired) assertFalse(spec.name, written.has(spec.name))
        assertTrue(written.has("strobe"))
        assertFalse(written.has("beatFlash"))
    }

    @Test
    fun hostilePresetIsClampedToTheSchemaRangesAndFallsBackToDefaults() {
        val preset = PresetStore.fromJson(fixture("hostile.json"))
        val p = preset.params
        assertEquals(PresetStore.ATTACK_RANGE.endInclusive, preset.attack)
        assertEquals(PresetStore.DECAY_RANGE.start, preset.decay)
        assertEquals(4f, p.speed)
        assertEquals(0.3f, p.zoom)
        assertEquals(SceneParams.DEFAULT.rotation, p.rotation)
        assertEquals(SceneParams.DEFAULT.warp, p.warp)
        assertEquals(SceneParams.DEFAULT.endlessZoomSpeed, p.endlessZoomSpeed)
        assertEquals(SceneParams.DEFAULT.camFov, p.camFov)
        assertTrue(p.trails)
        assertEquals(16, p.symmetry)
        assertEquals(0, p.palette)
        assertEquals(2, p.particleShape)
        assertEquals(16_777_215, p.reactSeed)
        assertEquals(4, p.paletteLut)
        // The schema's ranges, not the narrower slider stops (brightness 1.25, bloom 0.25).
        assertEquals(1.875f, p.brightness)
        assertEquals(0.625f, p.bloom)
        assertEquals(2f, p.intensity)
        assertEquals(40f, p.cymaticsFundamental)
        assertEquals(0.9f, p.waterDamping)
        assertEquals(50f, p.flowCurl)
        // A circular param wraps natively and is not clamped.
        assertEquals(1.5f, p.colorShift)
        // And it saves again.
        assertEquals(p, PresetStore.fromJson(PresetStore.toJson(preset)).params)
    }
}
