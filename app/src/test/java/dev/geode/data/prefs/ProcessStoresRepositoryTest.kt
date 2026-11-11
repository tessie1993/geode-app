package dev.geode.data.prefs

import dev.geode.data.AutoDirectorCodec
import dev.geode.data.AutoDirectorSettings
import dev.geode.data.AutoDirectorStore
import dev.geode.data.StyleShelfCodec
import dev.geode.data.StyleShelfStore
import dev.geode.model.StyleShelf
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Test

/** The process-wide stores on their repositories (#1009, PR 6): same keys and values as before. */
@OptIn(ExperimentalCoroutinesApi::class)
class ProcessStoresRepositoryTest {
    private val file = RecordingPrefs()

    private fun TestScope.shelf() =
        StyleShelfStore(
            PrefsSettingsRepository(file.prefs, StyleShelfCodec, backgroundScope, StandardTestDispatcher(testScheduler)),
            backgroundScope,
        )

    @Test
    fun styleShelfEditsInARowEachApplyToTheLatestShelfAndANewStoreReadsThem() =
        runTest {
            val store = shelf()
            advanceUntilIdle()
            store.toggleFavourite("plasma")
            store.pushRecent("tunnel")
            store.pushRecent("plasma")
            advanceUntilIdle()
            val expected = StyleShelf(favourites = setOf("plasma"), recents = listOf("plasma", "tunnel"))
            assertEquals(expected, store.shelf.value)
            assertEquals(setOf("plasma"), file.values["style_favourites"])
            assertEquals("plasma,tunnel", file.values["style_recents"])
            val again = shelf()
            advanceUntilIdle()
            assertEquals(expected, again.shelf.value)
        }

    @Test
    fun theDirectorsSwitchAndReachRoundTripUnderTodaysKeys() {
        assertEquals(AutoDirectorSettings(enabled = true, reach = AutoDirectorStore.DEFAULT_REACH), AutoDirectorCodec.read(file.prefs))
        val chosen = AutoDirectorSettings(enabled = false, reach = 2)
        file.prefs
            .edit()
            .also { AutoDirectorCodec.write(it, chosen) }
            .apply()
        assertEquals(chosen, AutoDirectorCodec.read(file.prefs))
        assertEquals(mapOf("auto_director_enabled" to false, "auto_director_reach" to 2), file.values.toMap())
    }

    @Test
    fun theKeepSetIsReadFromTheModulationFile() {
        val modulation = RecordingPrefs()
        assertEquals(emptySet<String>(), AutoDirectorCodec.readKept(modulation.prefs))
        modulation.values[AutoDirectorStore.KEY_KEPT] = setOf("speed", "#CAMERA")
        assertEquals(setOf("speed", "#CAMERA"), AutoDirectorCodec.readKept(modulation.prefs))
    }
}
