package dev.geode.engine.bridge

// The JNI declarations of geode_viz_jni.cpp for touch, gestures and shake.
object TouchNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /** A pinch's scale factor applied to Zoom, clamped to its schema range (core/viz/TouchTransform). */
    external fun touchZoom(
        current: Float,
        scaleFactor: Float,
    ): Float

    /** A twist in degrees added to Rotation, clamped to its schema range (core/viz/TouchTransform). */
    external fun touchRotation(
        current: Float,
        degrees: Float,
    ): Float

    /** Rewrites [points] (x, y) px pairs of [xy] in a [width] x [height] view as y-up NDC, in place (core/viz/TouchGesture). */
    external fun touchToNdc(
        xy: FloatArray,
        points: Int,
        width: Float,
        height: Float,
    )

    /**
     * Two fingers at [start], [last] and [now], each (ax, ay, bx, by) px, into [out] (6 floats): pan x, pan y and
     * zoom since start, twist in radians since start, and the step since last as a scale factor and degrees.
     */
    external fun touchPair(
        start: FloatArray,
        last: FloatArray,
        now: FloatArray,
        out: FloatArray,
    )

    /** Touch play's stir: [step]'s two NDC points into [xy] (4 floats) and the ms to hold them; 0 once it is over. */
    external fun touchPulse(
        step: Int,
        xy: FloatArray,
    ): Int

    /**
     * The actions Settings offers [gesture] (a CanvasGesture ordinal), as GestureAction ordinals into [out], the
     * [current] one kept, first, when it does not suit; how many there are, at most [GESTURE_MAX_CHOICES] (core/viz/GestureMap).
     */
    external fun gestureChoices(
        gesture: Int,
        current: Int,
        out: IntArray,
    ): Int

    /** An Android [keyCode]'s gesture on the stage (a CanvasGesture ordinal), -1 for none; a sideways step into [step] (1 int). */
    external fun gestureForKey(
        keyCode: Int,
        optionsShown: Boolean,
        step: IntArray,
    ): Int

    /** A gesture reader for one stage (core/viz/GestureRead); every call below takes it. */
    external fun gestureCreate(): Long

    external fun gestureDestroy(handle: Long)

    /** The platform's touch and double-tap slop (px), density (px per dp) and long-press and double-tap times (ms). */
    external fun gestureConfigure(
        handle: Long,
        touchSlop: Float,
        doubleTapSlop: Float,
        density: Float,
        longPressMs: Long,
        doubleTapMinMs: Long,
        doubleTapTimeoutMs: Long,
    )

    /** The window, its gesture strips (px from each edge) and the stage's top-left in it, once [placed]. */
    external fun gestureArea(
        handle: Long,
        windowW: Float,
        windowH: Float,
        left: Float,
        top: Float,
        right: Float,
        bottom: Float,
        originX: Float,
        originY: Float,
        placed: Boolean,
    )

    /** A first finger at ([x], [y]) stage px; [actions] each gesture's GestureAction ordinal; [armed] the hold may run. */
    external fun gestureDown(
        handle: Long,
        x: Float,
        y: Float,
        timeMs: Long,
        actions: IntArray,
        armed: Boolean,
    )

    /** How long to wait for the next event before [gestureTick]; -1 for no timer. */
    external fun gestureWaitMs(handle: Long): Long

    /** The hold timer ran out; GESTURE_* bits. */
    external fun gestureTick(handle: Long): Int

    /**
     * A pointer event at [timeMs] (-1 keeps the last): [live] pressed fingers' (x, y) px in [xy], whether the first
     * finger is still down; a pinch step's factor and degrees into [pinch] (2 floats). GESTURE_* bits.
     */
    external fun gestureEvent(
        handle: Long,
        timeMs: Long,
        xy: FloatArray,
        live: Int,
        primaryDown: Boolean,
        pinch: FloatArray,
    ): Int

    /** True when touch play holds fingers that must now lift; clears it. */
    external fun gestureRelease(handle: Long): Boolean

    /** The gesture to fire (a CanvasGesture ordinal, -1 none); [out] (3 ints): its step, 1 for a tap, the double-tap wait in ms. */
    external fun gestureEnd(
        handle: Long,
        splitY: Float,
        out: IntArray,
    ): Int

    /** A down while waiting for a second tap; a GESTURE_SECOND_* answer. */
    external fun gestureSecondDown(
        handle: Long,
        x: Float,
        y: Float,
        timeMs: Long,
    ): Int

    /** The followed second down's change ([present] false when the event has none); a GESTURE_SECOND_* answer. */
    external fun gestureSecondMove(
        handle: Long,
        present: Boolean,
        x: Float,
        y: Float,
        pressed: Boolean,
    ): Int

    const val GESTURE_MAX_CHOICES = 8
    const val GESTURE_RING = 1 shl 0
    const val GESTURE_HOLD = 1 shl 1
    const val GESTURE_SWIRL = 1 shl 2
    const val GESTURE_OVER = 1 shl 3
    const val GESTURE_PUBLISH = 1 shl 4
    const val GESTURE_RELEASE = 1 shl 5
    const val GESTURE_PINCH = 1 shl 6
    const val GESTURE_SECOND_BOUNCE = 0
    const val GESTURE_SECOND_FOLLOW = 1
    const val GESTURE_SECOND_DOUBLE = 2
    const val GESTURE_SECOND_NEW_TOUCH = 3

    /** The hold ring's fill after the long-press timeout (GEODE_GESTURE_HOLD_FILL_MS). */
    const val GESTURE_HOLD_FILL_MS = 800L

    /** Shake to screenshot's detector (`core/viz/Shake`), one per listener. */
    external fun shakeCreate(): Long

    external fun shakeDestroy(handle: Long)

    /** One TYPE_ACCELEROMETER event; true on the sample that completes a shake. */
    external fun shakeSample(
        handle: Long,
        timeNs: Long,
        x: Float,
        y: Float,
        z: Float,
    ): Boolean
}
