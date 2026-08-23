package dev.geode.render

import kotlin.math.exp
import kotlin.math.hypot
import kotlin.math.sqrt

/**
 * Where the fingers are, in one place every scene family can read.
 *
 * WHY: touch used to reach exactly two destinations — the surface in
 * [dev.geode.render.fluid.WaterScene], and otherwise a generic 2D ripple
 * overlay laid over whatever was underneath. Every other family — the fragment
 * styles, cymatics, the four field sims, the beam — was untouchable, so on 50
 * of the 58 styles a finger did nothing the style itself knew about. That is a
 * routing table, not an input: adding a family meant adding a branch, and no
 * style could be authored against "where is the user pointing" because there
 * was nowhere to ask.
 *
 * This is that place. The UI thread publishes a snapshot of the live pointers;
 * the GL thread steps it once per frame and every scene reads the same three
 * derived things:
 *
 * - [anchorX]/[anchorY] — the primary point. A style decides what that MEANS
 *   (vanishing point, spawn point, inversion pole, excitation site), but every
 *   style is expected to mean something by it.
 * - [gesture] — how many fingers are down, as an intent rather than a count.
 * - [axisX]/[axisY] and [spin] — the two- and three-finger quantities, so a
 *   style can escalate instead of treating the fifth finger like the first.
 *
 * ## Threading
 *
 * [submit] runs on the UI thread at touch rate (120-240 Hz on modern panels);
 * [step] runs on the GL thread at frame rate. The handover is latest-wins under
 * one short lock rather than a queue, because a visual anchor wants the CURRENT
 * position, not every position it passed through — a dropped intermediate
 * sample is invisible, whereas a backlog would drag the anchor behind the
 * finger. (The fluid smear still uses [VisualizerRenderer.queueTouchStroke]'s
 * queue, because a smear IS the path and dropping part of it loses ink.)
 *
 * Both sides write into preallocated arrays: this is a per-frame path, and the
 * render/audio hot-path rule in `CLAUDE.md` applies.
 *
 * ## Coordinates
 *
 * Everything published here is in **y-up normalized device coordinates**:
 * x and y in -1..1 with the origin at the centre of the surface and +y toward
 * the top, matching what the fragment styles already build out of `vUv`.
 * Aspect is NOT applied — a shader that needs square units scales by
 * `uResolution` itself, the same way it already does for its own geometry.
 */
class TouchField {
    companion object {
        /** Slots. Five is the finger count the multi-touch visualizer tradition settled on. */
        const val MAX_POINTS: Int = 5

        /** Floats per published point: x, y, strength, age. */
        const val POINT_STRIDE: Int = 4

        /** No fingers down. */
        const val GESTURE_NONE: Int = 0

        /** One finger: a point of interest. */
        const val GESTURE_ANCHOR: Int = 1

        /** Two fingers: a direction and a distance between them. */
        const val GESTURE_AXIS: Int = 2

        /** Three or more: a swirl about their centroid. */
        const val GESTURE_VORTEX: Int = 3

        /**
         * Seconds for a released point to fade to 1/e of its strength.
         *
         * A lifted finger that vanished on the frame it left would pop; a wake
         * reads as the gesture having happened. Long enough to see, short
         * enough that a style is not still being steered by a touch the user
         * has forgotten about.
         */
        const val RELEASE_TAU_SECONDS: Float = 0.55f

        /**
         * How fast the anchor chases the raw primary pointer, in units of
         * "fraction of the remaining distance per second".
         *
         * Raw touch coordinates are quantized to the panel's digitizer grid and
         * arrive at a rate that is not the frame rate, so a hard follow makes a
         * slow drag look like a staircase. This is a time-constant chase so the
         * behaviour is identical at 60, 90 and 120 Hz.
         */
        const val ANCHOR_TAU_SECONDS: Float = 0.06f

        /** Below this a decayed point is spent and its slot is free. */
        private const val SPENT_STRENGTH = 0.004f

        /** Velocity is clamped here; a digitizer glitch should not launch a style. */
        private const val MAX_SPEED = 8f

        private const val SPIN_TAU_SECONDS = 0.25f
    }

    /** The published point rows: [MAX_POINTS] * [POINT_STRIDE], x, y, strength, age. */
    val points: FloatArray = FloatArray(MAX_POINTS * POINT_STRIDE)

    /** How many rows of [points] carry a live or still-decaying point. */
    var count: Int = 0
        private set

    var anchorX: Float = 0f
        private set

    var anchorY: Float = 0f
        private set

    /** 0 when nothing is being touched, rising to 1 while a finger is down. */
    var anchorStrength: Float = 0f
        private set

    /** Seconds since the anchor was last touched by a live finger. */
    var anchorAge: Float = 0f
        private set

    var gesture: Int = GESTURE_NONE
        private set

    /** Two-finger axis: the vector from the first live point to the second. */
    var axisX: Float = 0f
        private set

    var axisY: Float = 0f
        private set

    /** Signed swirl rate of three or more fingers about their centroid, radians/second-ish. */
    var spin: Float = 0f
        private set

    // --- the UI -> GL handover -------------------------------------------------

    private val lock = Any()

    private val inbox = FloatArray(MAX_POINTS * 2)
    private var inboxCount = 0
    private var inboxDirty = false

    // --- GL-thread state -------------------------------------------------------

    private val liveX = FloatArray(MAX_POINTS)
    private val liveY = FloatArray(MAX_POINTS)
    private val strength = FloatArray(MAX_POINTS)
    private val age = FloatArray(MAX_POINTS)
    private val prevX = FloatArray(MAX_POINTS)
    private val prevY = FloatArray(MAX_POINTS)
    private var liveCount = 0
    private var anchorSeeded = false

    /** Where [step] copies the inbox to, so the lock is held for a memcpy and nothing else. */
    private val scratch = FloatArray(MAX_POINTS * 2)

    /**
     * Publish the pointers that are down right now, from the UI thread.
     *
     * [xy] is `x0, y0, x1, y1, ...` in y-up NDC; only the first
     * [MAX_POINTS] pairs are read. Passing `n = 0` means every finger has
     * lifted, which is a state worth publishing rather than a no-op — it is
     * what starts the release decay.
     */
    fun submit(
        xy: FloatArray,
        n: Int,
    ) {
        val clamped = n.coerceIn(0, MAX_POINTS)
        synchronized(lock) {
            for (i in 0 until clamped * 2) {
                inbox[i] = xy[i]
            }
            inboxCount = clamped
            inboxDirty = true
        }
    }

    /** Drop every point and every derived value. For a scene switch or a surface loss. */
    fun reset() {
        synchronized(lock) {
            inboxCount = 0
            inboxDirty = true
        }
        liveCount = 0
        anchorSeeded = false
        anchorStrength = 0f
        anchorAge = 0f
        spin = 0f
        axisX = 0f
        axisY = 0f
        gesture = GESTURE_NONE
        count = 0
        strength.fill(0f)
        age.fill(0f)
        points.fill(0f)
    }

    /**
     * Advance one frame. GL thread only.
     *
     * Reads the latest published pointers if there are any, ages and decays
     * everything, then recomputes the anchor, the gesture and the two- and
     * three-finger quantities.
     */
    fun step(dt: Float) {
        val step = dt.coerceIn(0f, 0.1f)
        val fresh =
            synchronized(lock) {
                if (!inboxDirty) {
                    -1
                } else {
                    inboxDirty = false
                    for (i in 0 until inboxCount * 2) {
                        scratch[i] = inbox[i]
                    }
                    inboxCount
                }
            }
        if (fresh >= 0) adoptLive(fresh, step)
        decay(step)
        publish()
        updateAnchor(step)
        updateGesture(step)
    }

    private fun adoptLive(
        n: Int,
        dt: Float,
    ) {
        // Slot i is pointer i as the UI ordered them. Compose hands pointers back in a
        // stable order for the life of a gesture, so a slot keeps its finger without this
        // needing an id map; a finger lifting mid-gesture reshuffles the tail, which reads
        // as those points jumping once - acceptable, and far cheaper than tracking ids for
        // a quantity whose whole job is to be approximate.
        for (i in 0 until n) {
            val nx = scratch[i * 2]
            val ny = scratch[i * 2 + 1]
            if (!nx.isFinite() || !ny.isFinite()) continue
            if (i < liveCount && dt > 0f) {
                prevX[i] = liveX[i]
                prevY[i] = liveY[i]
            } else {
                prevX[i] = nx
                prevY[i] = ny
            }
            liveX[i] = nx.coerceIn(-1f, 1f)
            liveY[i] = ny.coerceIn(-1f, 1f)
            strength[i] = 1f
            age[i] = 0f
        }
        liveCount = n
    }

    private fun decay(dt: Float) {
        val k = exp(-dt / RELEASE_TAU_SECONDS)
        // Exactly the released slots: 0 until liveCount are still held and do not decay.
        for (i in liveCount until MAX_POINTS) {
            if (strength[i] <= 0f) continue
            strength[i] *= k
            age[i] += dt
            if (strength[i] < SPENT_STRENGTH) strength[i] = 0f
        }
        for (i in 0 until liveCount) {
            age[i] += dt
        }
    }

    private fun publish() {
        var live = 0
        for (i in 0 until MAX_POINTS) {
            val base = i * POINT_STRIDE
            if (strength[i] <= 0f) {
                points[base] = 0f
                points[base + 1] = 0f
                points[base + 2] = 0f
                points[base + 3] = 0f
                continue
            }
            points[base] = liveX[i]
            points[base + 1] = liveY[i]
            points[base + 2] = strength[i]
            points[base + 3] = age[i]
            live = i + 1
        }
        count = live
    }

    private fun updateAnchor(dt: Float) {
        if (liveCount > 0) {
            val tx = liveX[0]
            val ty = liveY[0]
            if (!anchorSeeded) {
                // First contact teleports rather than sliding in from wherever the last
                // gesture ended - a new touch is a new intent, not a continuation.
                anchorX = tx
                anchorY = ty
                anchorSeeded = true
            } else {
                val k = 1f - exp(-dt / ANCHOR_TAU_SECONDS)
                anchorX += (tx - anchorX) * k
                anchorY += (ty - anchorY) * k
            }
            anchorStrength = 1f
            anchorAge = 0f
        } else {
            anchorStrength *= exp(-dt / RELEASE_TAU_SECONDS)
            anchorAge += dt
            if (anchorStrength < SPENT_STRENGTH) {
                anchorStrength = 0f
                anchorSeeded = false
            }
        }
    }

    private fun updateGesture(dt: Float) {
        gesture =
            when {
                liveCount <= 0 -> GESTURE_NONE
                liveCount == 1 -> GESTURE_ANCHOR
                liveCount == 2 -> GESTURE_AXIS
                else -> GESTURE_VORTEX
            }

        if (liveCount >= 2) {
            axisX = liveX[1] - liveX[0]
            axisY = liveY[1] - liveY[0]
        } else {
            val k = exp(-dt / RELEASE_TAU_SECONDS)
            axisX *= k
            axisY *= k
        }

        val target = if (liveCount >= 3 && dt > 0f) swirl(dt) else 0f
        val k = 1f - exp(-dt / SPIN_TAU_SECONDS)
        spin += (target - spin) * k
    }

    /**
     * Mean signed angular rate of the live points about their centroid.
     *
     * The cross product of "where the finger is relative to the centre" with
     * "how it moved" is positive for counter-clockwise motion, and dividing by
     * the radius turns a linear speed into an angular one, so a small circle
     * traced quickly and a large one traced slowly read the same.
     */
    private fun swirl(dt: Float): Float {
        var cx = 0f
        var cy = 0f
        for (i in 0 until liveCount) {
            cx += liveX[i]
            cy += liveY[i]
        }
        cx /= liveCount
        cy /= liveCount

        var total = 0f
        var counted = 0
        for (i in 0 until liveCount) {
            val rx = liveX[i] - cx
            val ry = liveY[i] - cy
            val radius = hypot(rx, ry)
            if (radius < 1e-3f) continue
            val vx = ((liveX[i] - prevX[i]) / dt).coerceIn(-MAX_SPEED, MAX_SPEED)
            val vy = ((liveY[i] - prevY[i]) / dt).coerceIn(-MAX_SPEED, MAX_SPEED)
            total += (rx * vy - ry * vx) / (radius * radius)
            counted++
        }
        return if (counted == 0) 0f else (total / counted).coerceIn(-MAX_SPEED, MAX_SPEED)
    }

    /** Distance between the two axis points, 0 when fewer than two are down. */
    fun spread(): Float {
        if (gesture != GESTURE_AXIS && gesture != GESTURE_VORTEX) return 0f
        return sqrt(axisX * axisX + axisY * axisY)
    }
}
