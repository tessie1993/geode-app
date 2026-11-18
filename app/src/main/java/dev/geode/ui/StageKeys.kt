package dev.geode.ui

import dev.geode.data.CanvasGesture
import dev.geode.engine.bridge.TouchNative

/**
 * A TV remote's (or a keyboard's or a game pad's) twin of each gesture on the visual, so every
 * action Settings › Gestures & controls gives a gesture is one key away on a device with no
 * touchscreen. This only says which [CanvasGesture] a key stands for; the stage runs it through
 * the same map as the finger ([dev.geode.data.BehaviourPrefs.actionFor], [VisualActions.run]), so
 * a remapped gesture moves its key with it and nothing here learns what an action does.
 *
 * The table is native (`core/viz/GestureMap`): with the options hidden the D-pad is the visual's,
 * with them up the arrows are left to focus. Each answer is the gesture and the step a sideways one
 * carries (+1 next, -1 previous). Null for a key that is no gesture, which the caller leaves to
 * whoever else wants it (Back, the media keys, which the playback session takes).
 */
internal fun stageKeyGesture(
    keyCode: Int,
    optionsShown: Boolean,
): Pair<CanvasGesture, Int>? {
    val step = IntArray(1)
    val gesture = CanvasGesture.entries.getOrNull(TouchNative.gestureForKey(keyCode, optionsShown, step)) ?: return null
    return gesture to step[0]
}
