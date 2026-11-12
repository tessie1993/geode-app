package dev.geode.data

import dev.geode.render.scene.SceneParams
import dev.geode.render.scene.TestCatalogue
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Presets and shared links are a file format (ADR 0001 step 2), held to golden files in `src/test/resources/presets/`.
 * The params object inside a preset is native's (core/viz/ParamsJson), and the JVM has no libgeode, so its checks
 * (keys, JSON types, number text, retired and unknown keys, the hostile values) read these same files in
 * `core/tests/params_json_test.cpp`. Here a stand-in params object passes through and these hold the envelope.
 */
class PresetGoldenTest {
    init {
        TestCatalogue.install()
        PresetStore.writeParams = { """{"speed":2.5,"customPaletteId":"p"}""" }
        PresetStore.readParams = { _, _, _ -> SceneParams.DEFAULT }
    }

    private fun fixture(name: String): String = checkNotNull(javaClass.getResource("/presets/$name")) { "missing fixture $name" }.readText()

    @Test
    fun theEnvelopeRoundTripsBesideTheParamsObject() {
        val golden = JSONObject(fixture("current.json"))
        val written = JSONObject(PresetStore.toJson(PresetStore.fromJson(golden.toString())))
        for (key in listOf("name", "sceneId", "customShader", "milkPreset")) assertEquals(key, golden.get(key), written.get(key))
        assertEquals(golden.getDouble("attack"), written.getDouble("attack"), 0.0)
        assertEquals(golden.getDouble("decay"), written.getDouble("decay"), 0.0)
        // The params object's keys sit beside the envelope's in the one object, as they always did.
        assertEquals(2.5, written.getDouble("speed"), 0.0)
        assertEquals("p", written.getString("customPaletteId"))
    }

    @Test
    fun theParamsObjectIsReadFromTheStoredTextItself() {
        var seen: String? = null
        var printed: String? = null
        PresetStore.readParams = { document, path, fallback ->
            seen = document
            printed = fallback
            assertEquals("", path)
            SceneParams.DEFAULT
        }
        val stored = fixture("legacy.json")
        PresetStore.fromJson(stored)
        // Not printed again first: org.json would print a whole double as a long.
        assertEquals(stored, seen)
        assertEquals(1.5, JSONObject(printed).getDouble("speed"), 0.0)
    }

    @Test
    fun aLookSavedOnTheRetiredWaterStyleOpensOnPool() {
        val water = PresetStore.fromJson("""{"name":"w","sceneId":"water","attack":0.1,"decay":0.2,"waterDepth":0.7}""")
        assertEquals("pool", water.sceneId)
    }

    @Test
    fun hostilePresetClampsItsEnvelope() {
        val preset = PresetStore.fromJson(fixture("hostile.json"))
        assertEquals(PresetStore.ATTACK_RANGE.endInclusive, preset.attack)
        assertEquals(PresetStore.DECAY_RANGE.start, preset.decay)
    }
}
