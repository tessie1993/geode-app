package dev.geode.data

import android.content.SharedPreferences
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ThemeStoreTest {
    @Test
    fun aSaveMidSetupDoesNotMarkSetupDone() {
        val store = ThemeStore(MapPrefs())
        store.saveGui(store.loadGui().copy(safetyAcknowledged = true))
        assertFalse(store.loadGui().setupDone)
    }

    @Test
    fun anAnsweredLegacyIntentStillCountsAsSetupDone() {
        val prefs = MapPrefs().apply { values[ThemeStore.KEY_INTENT] = "LISTENING" }
        assertTrue(ThemeStore(prefs).loadGui().setupDone)
    }

    @Test
    fun theLegacyReducedMotionAnswerSeedsTheSettingUntilItIsSaved() {
        val prefs =
            MapPrefs().apply {
                values[ThemeStore.KEY_SAFETY_CHOICE] = ThemeStore.LEGACY_CHOICE_REDUCED_MOTION
            }
        val store = ThemeStore(prefs)
        assertTrue(store.loadGui().reducedMotion)
        store.saveGui(store.loadGui().copy(reducedMotion = false))
        assertFalse(store.loadGui().reducedMotion)
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
