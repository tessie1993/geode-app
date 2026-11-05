package dev.geode.ui.tour

import android.content.SharedPreferences
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test

/**
 * #330's promise: a gesture tip is said once, ever, and only that tip. The drawn tip and a tap
 * that nudges it are tested on the device (`GestureTipHostTest`); this pins the counting and the
 * seen set on the JVM, where a regression is cheapest to catch.
 */
class GestureTipsTest {
    private val prefs = MapPrefs()

    @Before
    fun attach() {
        GestureTips.detach()
        GestureTips.attachTo(prefs)
    }

    @After
    fun forget() = GestureTips.detach()

    @Test
    fun theFirstLongWayRoundShowsTheTip() {
        GestureTips.nudge(GestureTip.SWIPE_STYLES)
        assertEquals(GestureTip.SWIPE_STYLES, GestureTips.showing)
    }

    @Test
    fun aSeenTipIsNeverShownAgain() {
        GestureTips.nudge(GestureTip.SWIPE_STYLES)
        GestureTips.dismiss()
        repeat(3) { GestureTips.nudge(GestureTip.SWIPE_STYLES) }
        assertNull(GestureTips.showing)
    }

    /** The seen set is on disk: a new process (detach, attach again) still remembers it. */
    @Test
    fun aSeenTipStaysSeenAfterARestart() {
        GestureTips.nudge(GestureTip.SWIPE_STYLES)
        GestureTips.dismiss()
        GestureTips.detach()
        GestureTips.attachTo(prefs)
        GestureTips.nudge(GestureTip.SWIPE_STYLES)
        assertNull(GestureTips.showing)
    }

    /** Seeing one tip spends only that tip: each gesture gets its own one mention. */
    @Test
    fun seeingOneTipLeavesTheOthersToBeSaid() {
        GestureTips.nudge(GestureTip.SWIPE_STYLES)
        GestureTips.dismiss()
        GestureTips.nudge(GestureTip.DOUBLE_TAP_SHEET)
        assertEquals(GestureTip.DOUBLE_TAP_SHEET, GestureTips.showing)
        GestureTips.dismiss()
        GestureTips.nudge(GestureTip.HOLD_TO_RECORD)
        assertEquals(GestureTip.HOLD_TO_RECORD, GestureTips.showing)
        GestureTips.dismiss()
        GestureTips.nudge(GestureTip.SWIPE_UP_PLAYER)
        assertEquals(GestureTip.SWIPE_UP_PLAYER, GestureTips.showing)
    }

    /** One at a time: a second tip waits rather than replacing the one being read. */
    @Test
    fun aTipOnScreenIsNotReplaced() {
        GestureTips.nudge(GestureTip.SWIPE_STYLES)
        GestureTips.nudge(GestureTip.HOLD_TO_RECORD)
        assertEquals(GestureTip.SWIPE_STYLES, GestureTips.showing)
    }

    /** Until the file is read, a nudge says nothing, so a tip seen before is never repeated. */
    @Test
    fun nothingShowsBeforeTheFileIsRead() {
        GestureTips.detach()
        GestureTips.nudge(GestureTip.SWIPE_STYLES)
        assertNull(GestureTips.showing)
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

        override fun registerOnSharedPreferenceChangeListener(
            listener: SharedPreferences.OnSharedPreferenceChangeListener,
        ) = Unit

        override fun unregisterOnSharedPreferenceChangeListener(
            listener: SharedPreferences.OnSharedPreferenceChangeListener,
        ) = Unit

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
