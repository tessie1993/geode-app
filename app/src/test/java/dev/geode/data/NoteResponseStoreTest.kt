package dev.geode.data

import android.content.SharedPreferences
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NoteResponseStoreTest {
    @Test
    fun `a new install starts on and tells the engine so once`() {
        val pushes = mutableListOf<Boolean>()
        val store = NoteResponseStore(MapPrefs()) { pushes += it }
        assertTrue(store.enabled.value)
        assertEquals(listOf(true), pushes)
    }

    @Test
    fun `a saved choice wins over the default`() {
        val prefs = MapPrefs()
        prefs.values[NoteResponseStore.KEY_ENABLED] = false
        val pushes = mutableListOf<Boolean>()
        val store = NoteResponseStore(prefs) { pushes += it }
        assertFalse(store.enabled.value)
        assertEquals(listOf(false), pushes)
    }

    @Test
    fun `the switch is saved and survives a new store`() {
        val prefs = MapPrefs()
        NoteResponseStore(prefs) { }.setEnabled(false)
        assertEquals(false, prefs.values[NoteResponseStore.KEY_ENABLED])
        val again = NoteResponseStore(prefs) { }
        assertFalse(again.enabled.value)
    }

    @Test
    fun `each change reaches the engine and repeats push nothing`() {
        val pushes = mutableListOf<Boolean>()
        val store = NoteResponseStore(MapPrefs()) { pushes += it }
        store.setEnabled(true)
        store.setEnabled(false)
        store.setEnabled(false)
        store.setEnabled(true)
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
