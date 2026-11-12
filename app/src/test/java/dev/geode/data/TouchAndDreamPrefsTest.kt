package dev.geode.data

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/**
 * The settings the critique found dead and the owner kept: Settings › Visuals › Touch (pinch and
 * twist; the smear went with Water, #1036), the pinch's gesture key, and the screen saver's "reacts
 * to any app". Each is read back as saved; what they do is native (`touch_transform_test.cpp`) or
 * the dream's sound source.
 */
@RunWith(RobolectricTestRunner::class)
class TouchAndDreamPrefsTest {
    private val prefs =
        ApplicationProvider
            .getApplicationContext<Context>()
            .getSharedPreferences("touch-dream-prefs-test", Context.MODE_PRIVATE)

    @Test
    fun touchDefaultIsPinchOn() {
        assertTrue(ThemeStore(prefs).loadGui().touchTransform)
    }

    @Test
    fun touchSettingsRoundTripUnderTheirKeys() {
        val store = ThemeStore(prefs)
        store.saveGui(store.loadGui().copy(touchTransform = false))
        assertFalse(ThemeStore(prefs).loadGui().touchTransform)
        assertFalse(prefs.getBoolean(ThemeStore.KEY_TOUCH_TRANSFORM, true))
    }

    @Test
    fun anOldInstallsSmearKeysGoOnTheNextSave() {
        prefs
            .edit()
            .putBoolean(ThemeStore.KEY_TOUCH_SMEAR, true)
            .putFloat(ThemeStore.KEY_TOUCH_SMEAR_STRENGTH, 1.6f)
            .commit()
        val store = ThemeStore(prefs)
        store.saveGui(store.loadGui())
        assertFalse(prefs.contains(ThemeStore.KEY_TOUCH_SMEAR))
        assertFalse(prefs.contains(ThemeStore.KEY_TOUCH_SMEAR_STRENGTH))
    }

    @Test
    fun thePinchGestureKeyIsReadAndSaved() {
        val store = BehaviourStore(prefs)
        assertEquals(GestureAction.OFF, store.load().actionFor(CanvasGesture.PINCH_TWIST))
        val b = store.load()
        store.save(b.copy(gestures = b.gestures + (CanvasGesture.PINCH_TWIST to GestureAction.TOUCH_PLAY)))
        assertEquals(GestureAction.TOUCH_PLAY, BehaviourStore(prefs).load().actionFor(CanvasGesture.PINCH_TWIST))
        assertEquals("TOUCH_PLAY", prefs.getString("behaviour_gesture_PINCH_TWIST", null))
    }

    @Test
    fun theScreenSaverHearsOtherAppsOnlyWhenAsked() {
        val store = BehaviourStore(prefs)
        assertFalse(store.load().dreamHearOtherApps)
        store.save(store.load().copy(dreamHearOtherApps = true))
        assertTrue(BehaviourStore(prefs).load().dreamHearOtherApps)
        assertTrue(prefs.getBoolean("behaviour_dream_hear_other_apps", false))
    }
}
