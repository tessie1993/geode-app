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
        assertTrue(VisualsSettingsCodec.read(prefs).touchTransform)
    }

    @Test
    fun touchSettingsRoundTripUnderTheirKeys() {
        saveVisuals(VisualsSettingsCodec.read(prefs).copy(touchTransform = false))
        assertFalse(VisualsSettingsCodec.read(prefs).touchTransform)
        assertFalse(prefs.getBoolean(VisualsSettingsCodec.KEY_TOUCH_TRANSFORM, true))
    }

    @Test
    fun anOldInstallsSmearKeysGoOnTheNextSave() {
        prefs
            .edit()
            .putBoolean(VisualsSettingsCodec.KEY_TOUCH_SMEAR, true)
            .putFloat(VisualsSettingsCodec.KEY_TOUCH_SMEAR_STRENGTH, 1.6f)
            .commit()
        saveVisuals(VisualsSettingsCodec.read(prefs))
        assertFalse(prefs.contains(VisualsSettingsCodec.KEY_TOUCH_SMEAR))
        assertFalse(prefs.contains(VisualsSettingsCodec.KEY_TOUCH_SMEAR_STRENGTH))
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

    private fun saveVisuals(value: VisualsSettings) = prefs.edit().also { VisualsSettingsCodec.write(it, value) }.commit()

    private fun saveBehaviour(value: BehaviourPrefs) = prefs.edit().also { BehaviourCodec.write(it, value) }.commit()
}
