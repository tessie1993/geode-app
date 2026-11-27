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
 * The settings the critique found dead and the owner kept: Settings › Gestures & controls › Touch
 * (pinch and twist, the smear and its strength) and the pinch's gesture key. Each is read back as saved;
 * what they do is native (`touch_transform_test.cpp`, `touch_smear_test.cpp`).
 */
@RunWith(RobolectricTestRunner::class)
class TouchPrefsTest {
    private val prefs =
        ApplicationProvider
            .getApplicationContext<Context>()
            .getSharedPreferences("touch-prefs-test", Context.MODE_PRIVATE)

    @Test
    fun touchDefaultIsPinchOn() {
        assertTrue(VisualsSettingsCodec.read(prefs).touchTransform)
    }

    @Test
    fun touchSettingsRoundTripUnderTheirKeys() {
        saveVisuals(VisualsSettingsCodec.read(prefs).copy(touchTransform = false))
        assertFalse(VisualsSettingsCodec.read(prefs).touchTransform)
        assertFalse(prefs.getBoolean(VisualsSettingsCodec.KEY_TOUCH_TRANSFORM, true))
    }

    @Test
    fun smearIsOffAtFullStrengthByDefault() {
        val v = VisualsSettingsCodec.read(prefs)
        assertFalse(v.touchSmear)
        assertEquals(1f, v.touchSmearStrength, 0f)
    }

    @Test
    fun anOldInstallsSmearKeysAreReadAgainAndKept() {
        prefs
            .edit()
            .putBoolean(VisualsSettingsCodec.KEY_TOUCH_SMEAR, true)
            .putFloat(VisualsSettingsCodec.KEY_TOUCH_SMEAR_STRENGTH, 1.6f)
            .commit()
        val v = VisualsSettingsCodec.read(prefs)
        assertTrue(v.touchSmear)
        assertEquals(1.6f, v.touchSmearStrength, 0f)
        saveVisuals(v.copy(touchSmearStrength = 0.5f))
        assertTrue(prefs.getBoolean(VisualsSettingsCodec.KEY_TOUCH_SMEAR, false))
        assertEquals(0.5f, prefs.getFloat(VisualsSettingsCodec.KEY_TOUCH_SMEAR_STRENGTH, 0f), 0f)
    }

    @Test
    fun thePinchGestureKeyIsReadAndSaved() {
        assertEquals(GestureAction.OFF, BehaviourCodec.read(prefs).actionFor(CanvasGesture.PINCH_TWIST))
        val b = BehaviourCodec.read(prefs)
        saveBehaviour(b.copy(gestures = b.gestures + (CanvasGesture.PINCH_TWIST to GestureAction.TOUCH_PLAY)))
        assertEquals(GestureAction.TOUCH_PLAY, BehaviourCodec.read(prefs).actionFor(CanvasGesture.PINCH_TWIST))
        assertEquals("TOUCH_PLAY", prefs.getString("behaviour_gesture_PINCH_TWIST", null))
    }

    private fun saveVisuals(value: VisualsSettings) = prefs.edit().also { VisualsSettingsCodec.write(it, value) }.commit()

    private fun saveBehaviour(value: BehaviourPrefs) = prefs.edit().also { BehaviourCodec.write(it, value) }.commit()
}
