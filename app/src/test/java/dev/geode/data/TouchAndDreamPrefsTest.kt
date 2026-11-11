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
 * The settings the critique found dead and the owner kept: Settings › Visuals › Touch (smear, its
 * strength, pinch and twist), the pinch's gesture key, and the screen saver's "reacts to any app".
 * Each is read back as saved; what they do is native (`core/tests/touch_smear_test.cpp`,
 * `touch_transform_test.cpp`) or the dream's sound source.
 */
@RunWith(RobolectricTestRunner::class)
class TouchAndDreamPrefsTest {
    private val prefs =
        ApplicationProvider
            .getApplicationContext<Context>()
            .getSharedPreferences("touch-dream-prefs-test", Context.MODE_PRIVATE)

    @Test
    fun touchDefaultsAreSmearOffAndPinchOn() {
        val gui = ThemeStore(prefs).loadGui()
        assertFalse(gui.touchSmear)
        assertEquals(1f, gui.touchSmearStrength)
        assertTrue(gui.touchTransform)
    }

    @Test
    fun touchSettingsRoundTripUnderTheirKeys() {
        val store = ThemeStore(prefs)
        store.saveGui(store.loadGui().copy(touchSmear = true, touchSmearStrength = 1.6f, touchTransform = false))
        val back = ThemeStore(prefs).loadGui()
        assertTrue(back.touchSmear)
        assertEquals(1.6f, back.touchSmearStrength)
        assertFalse(back.touchTransform)
        assertTrue(prefs.getBoolean(ThemeStore.KEY_TOUCH_SMEAR, false))
        assertFalse(prefs.getBoolean(ThemeStore.KEY_TOUCH_TRANSFORM, true))
    }

    @Test
    fun thePinchGestureKeyIsReadAndSaved() {
        assertEquals(GestureAction.OFF, BehaviourCodec.read(prefs).actionFor(CanvasGesture.PINCH_TWIST))
        val b = BehaviourCodec.read(prefs)
        saveBehaviour(b.copy(gestures = b.gestures + (CanvasGesture.PINCH_TWIST to GestureAction.TOUCH_PLAY)))
        assertEquals(GestureAction.TOUCH_PLAY, BehaviourCodec.read(prefs).actionFor(CanvasGesture.PINCH_TWIST))
        assertEquals("TOUCH_PLAY", prefs.getString("behaviour_gesture_PINCH_TWIST", null))
    }

    @Test
    fun theScreenSaverHearsOtherAppsOnlyWhenAsked() {
        assertFalse(BehaviourCodec.read(prefs).dreamHearOtherApps)
        saveBehaviour(BehaviourCodec.read(prefs).copy(dreamHearOtherApps = true))
        assertTrue(BehaviourCodec.read(prefs).dreamHearOtherApps)
        assertTrue(prefs.getBoolean("behaviour_dream_hear_other_apps", false))
    }

    private fun saveBehaviour(value: BehaviourPrefs) = prefs.edit().also { BehaviourCodec.write(it, value) }.commit()
}
