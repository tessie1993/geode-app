package dev.geode.data

import android.content.SharedPreferences
import dev.geode.data.prefs.PrefsSettingsRepository
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** The switch on its repository (#1009, PR 6): nothing reaches the engine before the saved value is read. */
@OptIn(ExperimentalCoroutinesApi::class)
class NoteResponseStoreTest {
    private val pushes = mutableListOf<Boolean>()

    private fun TestScope.store(prefs: MapPrefs): NoteResponseStore {
        val repository = PrefsSettingsRepository(prefs, NoteResponseCodec, backgroundScope, StandardTestDispatcher(testScheduler))
        return NoteResponseStore(repository, backgroundScope) { pushes += it }
    }

    @Test
    fun `a new install starts on and tells the engine so once`() =
        runTest {
            val store = store(MapPrefs())
            assertEquals(emptyList<Boolean>(), pushes)
            advanceUntilIdle()
            assertTrue(store.enabled.value)
            assertEquals(listOf(true), pushes)
        }

    @Test
    fun `a saved choice wins over the default and the default is never pushed`() =
        runTest {
            val prefs = MapPrefs()
            prefs.values[NoteResponseCodec.KEY_ENABLED] = false
            val store = store(prefs)
            advanceUntilIdle()
            assertFalse(store.enabled.value)
            assertEquals(listOf(false), pushes)
        }

    @Test
    fun `the switch is saved and survives a new store`() =
        runTest {
            val prefs = MapPrefs()
            store(prefs).setEnabled(false)
            advanceUntilIdle()
            assertEquals(false, prefs.values[NoteResponseCodec.KEY_ENABLED])
            val again = store(prefs)
            advanceUntilIdle()
            assertFalse(again.enabled.value)
        }

    @Test
    fun `each change reaches the engine and repeats push nothing`() =
        runTest {
            val store = store(MapPrefs())
            advanceUntilIdle()
            for (on in listOf(true, false, false, true)) {
                store.setEnabled(on)
                advanceUntilIdle()
            }
            assertEquals(listOf(true, false, true), pushes)
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

            override fun remove(key: String) = apply { pending[key] = null }

            override fun clear() = apply { values.keys.forEach { pending[it] = null } }

            override fun commit(): Boolean {
                apply()
                return true
            }

            override fun apply() {
                pending.forEach { (k, v) -> if (v == null) values.remove(k) else values[k] = v }
                pending.clear()
            }
        }
    }
}
