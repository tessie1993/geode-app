package dev.geode.data

import dev.geode.data.prefs.PrefsSettingsRepository
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * The store's part of an edit: keep the text native returned, hand it to the engine, save it. The
 * bank is read through its repository (#1009, PR 6), so each case lets the read and the saves land
 * ([settle]) before it looks at the file.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class MacroStoreTest {
    private val pushes = mutableListOf<String>()

    private fun TestScope.store(prefs: MemoryPrefs): MacroStore {
        val codec = MacroBankCodec(normalizeBank = { it.ifEmpty { DEFAULT } })
        val repository = PrefsSettingsRepository(prefs, codec, backgroundScope, StandardTestDispatcher(testScheduler))
        return MacroStore(repository, backgroundScope, push = { pushes += it }).also { settle() }
    }

    private fun TestScope.settle() = advanceUntilIdle()

    @Test
    fun `a new install loads what native calls the default and pushes it once`() =
        runTest {
            val store = store(MemoryPrefs())
            assertEquals(DEFAULT, store.state.value.text)
            assertEquals(
                listOf("Glow"),
                store.state.value.knobs
                    .map { it.name },
            )
            assertEquals(listOf(DEFAULT), pushes)
        }

    @Test
    fun `an edit is drawn, pushed and saved, and a new store loads it`() =
        runTest {
            val prefs = MemoryPrefs()
            store(prefs).edit { EDITED }
            settle()
            assertEquals(EDITED, prefs.values["macro_bank"])
            assertEquals(listOf(DEFAULT, EDITED), pushes)
            val again = store(prefs)
            assertEquals(EDITED, again.state.value.text)
            assertEquals(listOf(MacroKnob(1, "Glow", "", 0f), MacroKnob(5, "Mine", "", 0.5f)), again.state.value.knobs)
        }

    @Test
    fun `a drag pushes every step and writes once, at commit`() =
        runTest {
            val prefs = MemoryPrefs()
            val store = store(prefs)
            store.edit(persist = false) { EDITED }
            settle()
            assertEquals(listOf(DEFAULT, EDITED), pushes)
            assertNull(prefs.values["macro_bank"])
            store.commit()
            settle()
            assertEquals(EDITED, prefs.values["macro_bank"])
        }

    @Test
    fun `an edit that throws changes nothing`() =
        runTest {
            val prefs = MemoryPrefs()
            val store = store(prefs)
            store.edit { error("native is gone") }
            settle()
            assertEquals(DEFAULT, store.state.value.text)
            assertEquals(listOf(DEFAULT), pushes)
            assertNull(prefs.values["macro_bank"])
        }

    @Test
    fun `a draft is live but saved only when the user saves it`() =
        runTest {
            val prefs = MemoryPrefs()
            val store = store(prefs)
            assertEquals(5, store.drafted { EDITED })
            assertEquals(DEFAULT, store.state.value.text)
            assertEquals(EDITED, store.state.value.working)
            assertEquals(listOf(DEFAULT, EDITED), pushes)
            store.edit { ROLLED }
            settle()
            assertEquals(
                ROLLED,
                store.state.value.draft
                    ?.text,
            )
            assertNull(prefs.values["macro_bank"])
            // Native's rename is not loaded on the JVM: the draft is saved under the name it has.
            store.saveDraft("Mine")
            settle()
            assertNull(store.state.value.draft)
            assertEquals(ROLLED, prefs.values["macro_bank"])
            assertEquals(
                listOf("Glow", "Mine"),
                store.state.value.knobs
                    .map { it.name },
            )
        }

    @Test
    fun `a discarded draft leaves the bank as it was`() =
        runTest {
            val prefs = MemoryPrefs()
            val store = store(prefs)
            store.drafted { EDITED }
            store.discardDraft()
            settle()
            assertNull(store.state.value.draft)
            assertEquals(DEFAULT, store.state.value.working)
            assertEquals(listOf(DEFAULT, EDITED, DEFAULT), pushes)
            assertNull(prefs.values["macro_bank"])
        }

    @Test
    fun `the old knobs' keys are dropped on load`() =
        runTest {
            val prefs = MemoryPrefs()
            MacroStore.RETIRED_KEYS.forEach { prefs.values[it] = "old" }
            store(prefs)
            assertFalse(MacroStore.RETIRED_KEYS.any(prefs::contains))
        }

    private companion object {
        const val DEFAULT = "macrobank|1|5\nknob|1|0|Glow|\nlink|BLOOM|1|0\n"
        const val EDITED = "macrobank|1|6\nknob|1|0|Glow|\nlink|BLOOM|1|0\nknob|5|0.5|Mine|\n"
        const val ROLLED = "macrobank|1|6\nknob|1|0|Glow|\nlink|BLOOM|1|0\nknob|5|0|Mine|\nlink|ZOOM|0.3|0\n"
    }
}
