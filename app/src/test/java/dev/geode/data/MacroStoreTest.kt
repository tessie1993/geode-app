package dev.geode.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Test

/** The store's part of an edit: keep the text native returned, hand it to the engine, save it. */
class MacroStoreTest {
    private val pushes = mutableListOf<String>()

    private fun store(prefs: MemoryPrefs) = MacroStore(prefs, normalize = { it.ifEmpty { DEFAULT } }, push = { pushes += it })

    @Test
    fun `a new install loads what native calls the default and pushes it once`() {
        val store = store(MemoryPrefs())
        assertEquals(DEFAULT, store.state.value.text)
        assertEquals(listOf("Glow"), store.state.value.knobs.map { it.name })
        assertEquals(listOf(DEFAULT), pushes)
    }

    @Test
    fun `an edit is drawn, pushed and saved, and a new store loads it`() {
        val prefs = MemoryPrefs()
        store(prefs).edit { EDITED }
        assertEquals(EDITED, prefs.values["macro_bank"])
        assertEquals(listOf(DEFAULT, EDITED), pushes)
        val again = store(prefs)
        assertEquals(EDITED, again.state.value.text)
        assertEquals(listOf(MacroKnob(1, "Glow", "", 0f), MacroKnob(5, "Mine", "", 0.5f)), again.state.value.knobs)
    }

    @Test
    fun `a drag pushes every step and writes once, at commit`() {
        val prefs = MemoryPrefs()
        val store = store(prefs)
        store.edit(persist = false) { EDITED }
        assertEquals(listOf(DEFAULT, EDITED), pushes)
        assertNull(prefs.values["macro_bank"])
        store.commit()
        assertEquals(EDITED, prefs.values["macro_bank"])
    }

    @Test
    fun `an edit that throws changes nothing`() {
        val prefs = MemoryPrefs()
        val store = store(prefs)
        store.edit { error("native is gone") }
        assertEquals(DEFAULT, store.state.value.text)
        assertEquals(listOf(DEFAULT), pushes)
        assertNull(prefs.values["macro_bank"])
    }

    @Test
    fun `the old knobs' keys are dropped on load`() {
        val prefs = MemoryPrefs()
        MacroStore.RETIRED_KEYS.forEach { prefs.values[it] = "old" }
        store(prefs)
        assertFalse(MacroStore.RETIRED_KEYS.any(prefs::contains))
    }

    private companion object {
        const val DEFAULT = "macrobank|1|5\nknob|1|0|Glow|\nlink|BLOOM|1|0\n"
        const val EDITED = "macrobank|1|6\nknob|1|0|Glow|\nlink|BLOOM|1|0\nknob|5|0.5|Mine|\n"
    }
}
