package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefsCodec
import dev.geode.data.prefs.PrefsSettingsRepository
import dev.geode.model.TourChapter
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** `GuiPrefs` split by subject (#1009, PR 5): the old answers still read, and each repository writes only its own keys. */
@OptIn(ExperimentalCoroutinesApi::class)
class GuiSettingsCodecsTest {
    private fun <T> PrefsCodec<T>.save(
        prefs: SharedPreferences,
        value: T,
    ) = prefs.edit().also { write(it, value) }.apply()

    @Test
    fun aSaveMidSetupDoesNotMarkSetupDone() {
        val prefs = MapPrefs()
        AppStateCodec.save(prefs, AppStateCodec.read(prefs).copy(safetyAcknowledged = true))
        assertFalse(AppStateCodec.read(prefs).setupDone)
    }

    @Test
    fun anAnsweredLegacyIntentStillCountsAsSetupDone() {
        val prefs = MapPrefs().apply { values[AppStateCodec.KEY_INTENT] = "LISTENING" }
        assertTrue(AppStateCodec.read(prefs).setupDone)
    }

    @Test
    fun theOldTourEndedFlagCountsEveryChapterAndIsFoldedInOnTheNextWrite() {
        val prefs = MapPrefs().apply { values[AppStateCodec.KEY_TUTORIAL_SEEN] = true }
        val state = AppStateCodec.read(prefs)
        assertEquals(TourChapter.entries.toSet(), state.tourProgress.done)
        AppStateCodec.save(prefs, state)
        assertFalse(prefs.contains(AppStateCodec.KEY_TUTORIAL_SEEN))
        assertEquals(TourChapter.entries.toSet(), AppStateCodec.read(prefs).tourProgress.done)
    }

    @Test
    fun theRetiredReducedMotionSwitchIsDroppedOnTheNextSave() {
        // The app's own switch is gone (#336): reduced motion follows Android's "Remove animations"
        // alone, so an old answer must not linger in the prefs file (or a backup made from it).
        val prefs = MapPrefs().apply { values[GeneralSettingsCodec.KEY_REDUCED_MOTION] = true }
        GeneralSettingsCodec.save(prefs, GeneralSettingsCodec.read(prefs))
        assertFalse(prefs.contains(GeneralSettingsCodec.KEY_REDUCED_MOTION))
    }

    @Test
    fun pipIsOnForEveryoneUntilTheSwitchTurnsItOffAndTheOffStays() {
        // The old key was written false by every save while off was the default, so it means nothing (#994).
        val prefs = MapPrefs().apply { values[GeneralSettingsCodec.KEY_AUTO_ENTER_PIP_OFF_DEFAULT] = false }
        assertTrue(GeneralSettingsCodec.read(prefs).autoEnterPip)
        GeneralSettingsCodec.save(prefs, GeneralSettingsCodec.read(prefs).copy(autoEnterPip = false))
        assertFalse(prefs.contains(GeneralSettingsCodec.KEY_AUTO_ENTER_PIP_OFF_DEFAULT))
        assertFalse(GeneralSettingsCodec.read(prefs).autoEnterPip)
        GeneralSettingsCodec.save(prefs, GeneralSettingsCodec.read(prefs).copy(uiSounds = true))
        assertFalse(GeneralSettingsCodec.read(prefs).autoEnterPip)
    }

    @Test
    fun theOldWhiteFontReadsAsWhiteUntilAColourIsChosenThenStaysGone() {
        val prefs = MapPrefs().apply { values[GeneralSettingsCodec.KEY_WHITE_FONT] = true }
        assertEquals(FontColorChoice.WHITE_ARGB, GeneralSettingsCodec.read(prefs).fontColorArgb)
        GeneralSettingsCodec.save(prefs, GeneralSettings(fontColorArgb = null))
        assertFalse(prefs.contains(GeneralSettingsCodec.KEY_WHITE_FONT))
        assertNull(GeneralSettingsCodec.read(prefs).fontColorArgb)
    }

    @Test
    fun theSubjectsShareOneFileWithoutWritingOverEachOther() =
        runTest {
            val prefs = MapPrefs()
            val io = StandardTestDispatcher(testScheduler)
            val general = PrefsSettingsRepository(prefs, GeneralSettingsCodec, this, io)
            val appState = PrefsSettingsRepository(prefs, AppStateCodec, this, io)
            val visuals = PrefsSettingsRepository(prefs, VisualsSettingsCodec, this, io)
            appState.update { it.copy(safetyAcknowledged = true) }
            general.update { it.copy(uiSounds = true) }
            visuals.update { it.copy(secondScreen = false) }
            assertTrue(AppStateCodec.read(prefs).safetyAcknowledged)
            assertTrue(GeneralSettingsCodec.read(prefs).uiSounds)
            assertFalse(VisualsSettingsCodec.read(prefs).secondScreen)
            assertFalse(prefs.contains(SoundInputSettingsCodec.KEY_MIC_REACTIVE))
            assertFalse(prefs.contains(LibrarySettingsCodec.KEY_PRESET_MIRROR_URI))
        }

    /** In-memory [SharedPreferences]: the platform one is a stub in local unit tests. */
    private class MapPrefs : SharedPreferences {
        val values = mutableMapOf<String, Any?>()

        override fun getAll(): MutableMap<String, *> = values

        override fun getString(
            key: String,
            defValue: String?,
        ): String? = values[key] as String? ?: defValue

        @Suppress("UNCHECKED_CAST")
        override fun getStringSet(
            key: String,
            defValues: MutableSet<String>?,
        ): MutableSet<String>? = values[key] as MutableSet<String>? ?: defValues

        override fun getInt(
            key: String,
            defValue: Int,
        ): Int = values[key] as Int? ?: defValue

        override fun getLong(
            key: String,
            defValue: Long,
        ): Long = values[key] as Long? ?: defValue

        override fun getFloat(
            key: String,
            defValue: Float,
        ): Float = values[key] as Float? ?: defValue

        override fun getBoolean(
            key: String,
            defValue: Boolean,
        ): Boolean = values[key] as Boolean? ?: defValue

        override fun contains(key: String): Boolean = key in values

        override fun edit(): SharedPreferences.Editor = Editor()

        override fun registerOnSharedPreferenceChangeListener(listener: SharedPreferences.OnSharedPreferenceChangeListener) = Unit

        override fun unregisterOnSharedPreferenceChangeListener(listener: SharedPreferences.OnSharedPreferenceChangeListener) = Unit

        private inner class Editor : SharedPreferences.Editor {
            private val pending = mutableMapOf<String, Any?>()
            private val removed = mutableSetOf<String>()

            override fun putString(
                key: String,
                value: String?,
            ) = apply { pending[key] = value }

            override fun putStringSet(
                key: String,
                values: MutableSet<String>?,
            ) = apply { pending[key] = values }

            override fun putInt(
                key: String,
                value: Int,
            ) = apply { pending[key] = value }

            override fun putLong(
                key: String,
                value: Long,
            ) = apply { pending[key] = value }

            override fun putFloat(
                key: String,
                value: Float,
            ) = apply { pending[key] = value }

            override fun putBoolean(
                key: String,
                value: Boolean,
            ) = apply { pending[key] = value }

            override fun remove(key: String) = apply { removed += key }

            override fun clear() = apply { removed += values.keys }

            override fun commit(): Boolean {
                apply()
                return true
            }

            override fun apply() {
                removed.forEach { values.remove(it) }
                pending.forEach { (k, v) -> if (v == null) values.remove(k) else values[k] = v }
            }
        }
    }
}
