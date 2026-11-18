package dev.geode.data

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** `relinkAll` drops the texture pins of presets that are gone, so the pins file does not keep every preset ever pinned. */
@RunWith(RobolectricTestRunner::class)
class MilkTexturePinsTest {
    private fun pins() =
        JSONObject()
            .put("kept.milk", JSONObject().put("headlights", "lights_1.png"))
            .put("deleted.milk", JSONObject().put("sky", "sky_2.jpg"))

    @Test
    fun pinsOfAPresetThatIsGoneAreDropped() {
        val overrides = pins()
        assertTrue(MilkTextureLinks.dropDeadPins(overrides) { it == "kept.milk" })
        assertEquals(setOf("kept.milk"), overrides.keys().asSequence().toSet())
        assertEquals("lights_1.png", overrides.getJSONObject("kept.milk").getString("headlights"))
    }

    @Test
    fun nothingChangesWhileEveryPinnedPresetExists() {
        val overrides = pins()
        assertFalse(MilkTextureLinks.dropDeadPins(overrides) { true })
        assertEquals(2, overrides.length())
    }
}
