package dev.geode.ui

import android.view.KeyEvent
import dev.geode.data.CanvasGesture

/**
 * A TV remote's (or a keyboard's or a game pad's) twin of each gesture on the visual, so every
 * action Settings › Gestures & controls gives a gesture is one key away on a device with no
 * touchscreen. This only says which [CanvasGesture] a key stands for; the stage runs it through
 * the same map as the finger ([dev.geode.data.BehaviourPrefs.actionFor], [VisualActions.run]), so
 * a remapped gesture moves its key with it and nothing here learns what an action does.
 *
 * With the options hidden (full screen) the D-pad is the visual's: left and right are the sideways
 * swipe (right is next, as › is), up and down the vertical swipes, and the centre the double tap.
 * With the options up the arrows are left to focus, so the remote can reach the rail and the
 * options; the centre still is the double tap. Channel up and down step like a swipe either way,
 * and the remote's colour keys, Menu, Info and Record carry the gestures with no direction.
 * Each is the gesture and the step a sideways one carries (+1 next, -1 previous), as the finger's
 * swipe reports it. Null for a key that is no gesture, which the caller leaves to whoever else
 * wants it (Back, the media keys, which the playback session takes).
 */
internal fun stageKeyGesture(
    keyCode: Int,
    optionsShown: Boolean,
): Pair<CanvasGesture, Int>? =
    when (keyCode) {
        KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER, KeyEvent.KEYCODE_BUTTON_A ->
            CanvasGesture.DOUBLE_TAP to 0
        KeyEvent.KEYCODE_CHANNEL_UP -> CanvasGesture.SWIPE_SIDEWAYS to 1
        KeyEvent.KEYCODE_CHANNEL_DOWN -> CanvasGesture.SWIPE_SIDEWAYS to -1
        KeyEvent.KEYCODE_MEDIA_RECORD, KeyEvent.KEYCODE_PROG_RED -> CanvasGesture.LONG_PRESS to 0
        KeyEvent.KEYCODE_PROG_GREEN, KeyEvent.KEYCODE_BUTTON_Y -> CanvasGesture.SWIRL to 0
        KeyEvent.KEYCODE_PROG_YELLOW, KeyEvent.KEYCODE_BUTTON_X -> CanvasGesture.TWO_FINGER_DRAG to 0
        KeyEvent.KEYCODE_PROG_BLUE, KeyEvent.KEYCODE_MENU -> CanvasGesture.TAP_TOP to 0
        KeyEvent.KEYCODE_INFO -> CanvasGesture.TAP_BOTTOM to 0
        else -> if (optionsShown) null else dpadGesture(keyCode)
    }

private fun dpadGesture(keyCode: Int): Pair<CanvasGesture, Int>? =
    when (keyCode) {
        KeyEvent.KEYCODE_DPAD_LEFT -> CanvasGesture.SWIPE_SIDEWAYS to -1
        KeyEvent.KEYCODE_DPAD_RIGHT -> CanvasGesture.SWIPE_SIDEWAYS to 1
        KeyEvent.KEYCODE_DPAD_UP -> CanvasGesture.SWIPE_UP to 0
        KeyEvent.KEYCODE_DPAD_DOWN -> CanvasGesture.SWIPE_DOWN to 0
        else -> null
    }
