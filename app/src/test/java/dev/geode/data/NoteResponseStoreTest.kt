package dev.geode.data

import android.content.SharedPreferences
import dev.geode.pro.Entitlement
import dev.geode.pro.FeatureGate
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NoteResponseStoreTest {
    private val pro = FeatureGate(Entitlement.Pro)
    private val trial = FeatureGate(Entitlement.Trial(endsAtMs = 1L))

    @Test
    fun `starts off and tells the engine so once`() {
        val pushes = mutableListOf<Boolean>()
        val store = NoteResponseStore(MapPrefs()) { pushes += it }
        assertFalse(store.enabled.value)
        assertFalse(store.active)
        assertEquals(listOf(false), pushes)
    }

    @Test
    fun `the switch is saved and survives a new store`() {
        val prefs = MapPrefs()
        NoteResponseStore(prefs) { }.setEnabled(true)
        assertEquals(true, prefs.values[NoteResponseStore.KEY_ENABLED])
        val again = NoteResponseStore(prefs) { }
        assertTrue(again.enabled.value)
    }

    @Test
    fun `free keeps the engine off whatever the switch says`() {
        val pushes = mutableListOf<Boolean>()
        val store = NoteResponseStore(MapPrefs()) { pushes += it }
        store.setEnabled(true)
        assertTrue(store.enabled.value)
        assertFalse(store.active)
        assertEquals(listOf(false), pushes)
    }

    @Test
    fun `an entitlement turns it on, a lapsed trial off, and repeats push nothing`() {
        val pushes = mutableListOf<Boolean>()
        val store = NoteResponseStore(MapPrefs()) { pushes += it }
        store.setEnabled(true)
        store.onGate(trial)
        store.onGate(trial)
        assertTrue(store.active)
        store.onGate(FeatureGate.FREE)
        assertFalse(store.active)
        store.onGate(pro)
        store.setEnabled(false)
        assertEquals(listOf(false, true, false, true, false), pushes)
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
