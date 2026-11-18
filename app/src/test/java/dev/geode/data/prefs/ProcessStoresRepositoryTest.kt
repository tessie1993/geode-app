package dev.geode.data.prefs

import dev.geode.data.AutoDirectorCodec
import dev.geode.data.AutoDirectorSettings
import dev.geode.data.AutoDirectorStore
import dev.geode.data.CustomizeGroups
import dev.geode.data.CustomizeGroupsCodec
import dev.geode.data.CustomizeGroupsStore
import dev.geode.data.KeepSetStore
import dev.geode.data.StyleShelfCodec
import dev.geode.data.StyleShelfStore
import dev.geode.model.StyleShelf
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * The process-wide stores on their repositories (#1009, PR 6): same keys and values as before. The
 * stores run in [TestScope.backgroundScope], which `advanceUntilIdle` does not run, so the cases use
 * [runCurrent] (nothing here waits on time).
 */
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
            runCurrent()
            store.toggleFavourite("plasma")
            store.pushRecent("tunnel")
            store.pushRecent("plasma")
            runCurrent()
            val expected = StyleShelf(favourites = setOf("plasma"), recents = listOf("plasma", "tunnel"))
            assertEquals(expected, store.shelf.value)
            assertEquals(setOf("plasma"), file.values["style_favourites"])
            assertEquals("plasma,tunnel", file.values["style_recents"])
            val again = shelf()
            runCurrent()
            assertEquals(expected, again.shelf.value)
        }

    private fun TestScope.groups() =
        CustomizeGroupsStore(
            PrefsSettingsRepository(file.prefs, CustomizeGroupsCodec, backgroundScope, StandardTestDispatcher(testScheduler)),
            backgroundScope,
        )

    @Test
    fun customizeGroupsStayOpenAndCollapseAllClosesOneTabOnly() =
        runTest {
            val store = groups()
            runCurrent()
            store.toggle("LOOK/basics")
            store.toggle("LOOK/colour_detail")
            store.toggle("SHAPE/basics")
            store.selectTab("LOOK")
            store.collapseAll("LOOK")
            runCurrent()
            assertEquals(CustomizeGroups(open = setOf("SHAPE/basics"), tab = "LOOK"), store.groups.value)
            assertEquals(setOf("SHAPE/basics"), file.values["customize_open_groups"])
            val again = groups()
            runCurrent()
            assertEquals(store.groups.value, again.groups.value)
            again.collapseAll(null)
            runCurrent()
            assertEquals(emptySet<String>(), again.groups.value.open)
        }

    @Test
    fun theDirectorsSwitchAndReachRoundTripUnderTodaysKeys() {
        assertEquals(AutoDirectorSettings(enabled = true, reach = AutoDirectorStore.DEFAULT_REACH), AutoDirectorCodec.read(file.prefs))
        val chosen = AutoDirectorSettings(enabled = false, reach = 2, tuning = "lean_speed=0.5\n")
        file.prefs
            .edit()
            .also { AutoDirectorCodec.write(it, chosen) }
            .apply()
        assertEquals(chosen, AutoDirectorCodec.read(file.prefs))
        assertEquals(
            mapOf("auto_director_enabled" to false, "auto_director_reach" to 2, "auto_director_tuning" to "lean_speed=0.5\n"),
            file.values.toMap(),
        )
    }

    @Test
    fun theKeepSetIsReadFromTheModulationFile() {
        val modulation = RecordingPrefs()
        val empty = HeldKeep { it }
        assertEquals("", KeepSetStore(modulation.prefs, empty::load, empty::toggle, empty::held).document.value)
        // The string set users have saved: handed to native as one key a line, and what native then holds is shown.
        modulation.values[KeepSetStore.KEY] = setOf("speed", "#CAMERA")
        val native = HeldKeep { "#CAMERA\nspeed\n" }
        val normalized = KeepSetStore(modulation.prefs, native::load, native::toggle, native::held)
        assertEquals("#CAMERA\nspeed\n", normalized.document.value)
        assertEquals(setOf("speed", "#CAMERA"), normalized.keys.value)
    }

    @Test
    fun aTappedKeepSetIsSavedAsAStringSet() {
        val modulation = RecordingPrefs()
        val native = HeldKeep { it }
        val store = KeepSetStore(modulation.prefs, native::load, native::toggle, native::held)
        // What native holds after the tap.
        native.next = "#LOOK\nspeed\n"
        store.tap("speed")
        assertEquals("speed", native.tapped)
        assertEquals(setOf("#LOOK", "speed"), modulation.values[KeepSetStore.KEY])
        assertEquals(setOf("#LOOK", "speed"), store.keys.value)
        assertEquals("#LOOK\nspeed\n", store.document.value)
    }

    /** Native's keep set for a JVM test: [normalize] is what it holds after a load, [next] after a tap. */
    private class HeldKeep(
        private val normalize: (String) -> String,
    ) {
        var document = ""
        var next = ""
        var tapped: String? = null

        fun load(text: String) {
            document = normalize(text)
        }

        fun toggle(key: String) {
            tapped = key
            document = next
        }

        fun held(): String = document
    }
}
