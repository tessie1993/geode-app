package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Test

/** The Palettes card's read of native's editor text; core/tests/palette_test pins what C++ emits. */
class PaletteStackStoreTest {
    @Test
    fun `an unset stack has no layers and the whole library to pick from`() {
        val state = PaletteStackStore.parse("", "palettestack|1|0|8|SHARE\npick|0|Aurora\npick|1|Ember\n")
        assertEquals(emptyList<String>(), state.layers)
        assertEquals(8, state.room)
        assertEquals("SHARE", state.blend)
        assertEquals(listOf(PalettePick(0, "Aurora"), PalettePick(1, "Ember")), state.picks)
    }

    @Test
    fun `layers come in stack order`() {
        val state = PaletteStackStore.parse("s", "palettestack|1|2|6|MIX\nlayer|0|Ember\nlayer|1|Aurora\npick|0|Aurora\n")
        assertEquals("s", state.text)
        assertEquals(listOf("Ember", "Aurora"), state.layers)
        assertEquals(6, state.room)
        assertEquals("MIX", state.blend)
    }

    @Test
    fun `text that is not the editor's reads as nothing`() {
        assertEquals(PaletteStackState(text = "s"), PaletteStackStore.parse("s", "garbage\n\n"))
    }
}
