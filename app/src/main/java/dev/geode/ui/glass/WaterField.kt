package dev.geode.ui.glass

import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.getSystemService
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import kotlinx.coroutines.isActive
import kotlin.math.min

/** The shared [WaterField] for the current screen, or null where no screen root has provided one
 * (glassTouch/floatOnWater degrade gracefully to their non-coupled behaviour in that case). */
internal val LocalWaterField = staticCompositionLocalOf<WaterField?> { null }

/**
 * One shared, low-resolution water simulation for a screen (see
 * docs/design/liquid-glass/README.md "Water field"): a height field driving ripples/float/tilt,
 * and a small stable-fluids-lite velocity+dye grid driving the ink. Not thread-safe; stepped once
 * per frame on the UI thread by [LiquidBackground] / [rememberWaterField].
 */
internal class WaterField {
    private var hw = HEIGHT_W
    private var hh = HEIGHT_H
    private var height = FloatArray(hw * hh)
    private var heightPrev = FloatArray(hw * hh)

    private val vw = VEL_W
    private val vh = VEL_H
    private var velX = FloatArray(vw * vh)
    private var velY = FloatArray(vw * vh)
    private var dyeR = FloatArray(vw * vh)
    private var dyeG = FloatArray(vw * vh)
    private var dyeB = FloatArray(vw * vh)
    private var pressure = FloatArray(vw * vh)
    private var divergence = FloatArray(vw * vh)

    var canvasWidth: Float = 1f
        private set
    var canvasHeight: Float = 1f
        private set

    var tiltX: Float = 0f
        private set
    var tiltY: Float = 0f
        private set

    val heightGridSize: Pair<Int, Int> get() = hw to hh
    val dyeGridSize: Pair<Int, Int> get() = vw to vh

    fun updateCanvasSize(
        w: Float,
        h: Float,
    ) {
        if (w <= 0f || h <= 0f) return
        canvasWidth = w
        canvasHeight = h
        val targetHh = (hw * (h / w)).toInt().coerceIn(HEIGHT_H / 2, HEIGHT_H * 2)
        if (targetHh != hh) {
            hh = targetHh
            height = FloatArray(hw * hh)
            heightPrev = FloatArray(hw * hh)
        }
    }

    fun setTilt(
        x: Float,
        y: Float,
    ) {
        tiltX = x
        tiltY = y
    }

    /** A tap impulse (press, release-at-half-strength) into the height field. */
    fun tap(
        xPx: Float,
        yPx: Float,
        strength: Float,
    ) {
        val gx = ((xPx / canvasWidth) * hw).toInt()
        val gy = ((yPx / canvasHeight) * hh).toInt()
        addHeightImpulse(gx, gy, strength)
    }

    /** A velocity + dye splat (hold, drag) into the fluid grid. */
    fun splat(
        xPx: Float,
        yPx: Float,
        dxPx: Float,
        dyPx: Float,
        color: Color,
    ) {
        val gx = ((xPx / canvasWidth) * vw).toInt()
        val gy = ((yPx / canvasHeight) * vh).toInt()
        addVelocityDye(gx, gy, dxPx, dyPx, color)
    }

    fun heightAt(
        xPx: Float,
        yPx: Float,
    ): Float = sampleBilinear(height, hw, hh, (xPx / canvasWidth) * hw, (yPx / canvasHeight) * hh)

    fun gradientAt(
        xPx: Float,
        yPx: Float,
    ): Offset {
        val fx = (xPx / canvasWidth) * hw
        val fy = (yPx / canvasHeight) * hh
        val dx = sampleBilinear(height, hw, hh, fx + 1f, fy) - sampleBilinear(height, hw, hh, fx - 1f, fy)
        val dy = sampleBilinear(height, hw, hh, fx, fy + 1f) - sampleBilinear(height, hw, hh, fx, fy - 1f)
        return Offset(dx, dy)
    }

    fun flowAt(
        xPx: Float,
        yPx: Float,
    ): Offset {
        val fx = (xPx / canvasWidth) * vw
        val fy = (yPx / canvasHeight) * vh
        return Offset(sampleBilinear(velX, vw, vh, fx, fy), sampleBilinear(velY, vw, vh, fx, fy))
    }

    fun step(
        dtSeconds: Float,
        liquidMotion: Float,
    ) {
        stepHeightField(liquidMotion)
        stepFluid(dtSeconds, liquidMotion)
    }

    internal fun heightGrid(): FloatArray = height

    internal fun dyeGrids(): Triple<FloatArray, FloatArray, FloatArray> = Triple(dyeR, dyeG, dyeB)

    private fun addHeightImpulse(
        gx: Int,
        gy: Int,
        strength: Float,
    ) {
        for (oy in -1..1) {
            for (ox in -1..1) {
                val idx = clampIndex(gx + ox, gy + oy, hw, hh) ?: continue
                val falloff = if (ox == 0 && oy == 0) 1f else 0.4f
                height[idx] += strength * falloff
            }
        }
    }

    private fun addVelocityDye(
        gx: Int,
        gy: Int,
        dxPx: Float,
        dyPx: Float,
        color: Color,
    ) {
        val idx = clampIndex(gx, gy, vw, vh) ?: return
        velX[idx] += dxPx * SPLAT_VELOCITY_SCALE
        velY[idx] += dyPx * SPLAT_VELOCITY_SCALE
        dyeR[idx] = min(1f, dyeR[idx] + color.red * SPLAT_DYE_STRENGTH)
        dyeG[idx] = min(1f, dyeG[idx] + color.green * SPLAT_DYE_STRENGTH)
        dyeB[idx] = min(1f, dyeB[idx] + color.blue * SPLAT_DYE_STRENGTH)
    }

    /** 2D wave equation `h' = 2h - h_prev + c^2 . laplacian(h)`, damped, with a slow tilt-driven bias. */
    private fun stepHeightField(liquidMotion: Float) {
        if (liquidMotion <= 0f) return
        val next = heightPrev
        for (y in 0 until hh) {
            for (x in 0 until hw) {
                val idx = y * hw + x
                val left = height[clampIndex(x - 1, y, hw, hh) ?: idx]
                val right = height[clampIndex(x + 1, y, hw, hh) ?: idx]
                val up = height[clampIndex(x, y - 1, hw, hh) ?: idx]
                val down = height[clampIndex(x, y + 1, hw, hh) ?: idx]
                val laplacian = left + right + up + down - 4f * height[idx]
                var value = 2f * height[idx] - heightPrev[idx] + WAVE_SPEED_SQ * laplacian
                value *= WAVE_DAMPING
                value += (tiltX * (x - hw / 2f) + tiltY * (y - hh / 2f)) * TILT_HEIGHT_BIAS
                next[idx] = value
            }
        }
        heightPrev = height
        height = next
    }

    private fun stepFluid(
        dtSeconds: Float,
        liquidMotion: Float,
    ) {
        if (liquidMotion <= 0f) return
        // Self-advect velocity: sample last frame's velocity at each cell's back-traced position.
        val prevVelX = velX.copyOf()
        val prevVelY = velY.copyOf()
        advect(velX, prevVelX, prevVelX, prevVelY, dtSeconds)
        advect(velY, prevVelY, prevVelX, prevVelY, dtSeconds)
        project()
        for (i in velX.indices) {
            velX[i] = (velX[i] + tiltX * TILT_FLOW_BIAS) * FLUID_VELOCITY_DISSIPATION
            velY[i] = (velY[i] + tiltY * TILT_FLOW_BIAS) * FLUID_VELOCITY_DISSIPATION
        }
        // Advect dye by the freshly-updated velocity field.
        advect(dyeR, dyeR.copyOf(), velX, velY, dtSeconds)
        advect(dyeG, dyeG.copyOf(), velX, velY, dtSeconds)
        advect(dyeB, dyeB.copyOf(), velX, velY, dtSeconds)
        for (i in dyeR.indices) {
            dyeR[i] *= FLUID_DYE_DISSIPATION
            dyeG[i] *= FLUID_DYE_DISSIPATION
            dyeB[i] *= FLUID_DYE_DISSIPATION
        }
    }

    /** Semi-Lagrangian back-trace: `field[cell] = source(cell - velocity * dt)`, bilinearly sampled. */
    private fun advect(
        field: FloatArray,
        source: FloatArray,
        velocityX: FloatArray,
        velocityY: FloatArray,
        dt: Float,
    ) {
        for (y in 0 until vh) {
            for (x in 0 until vw) {
                val idx = y * vw + x
                val bx = x - velocityX[idx] * dt
                val by = y - velocityY[idx] * dt
                field[idx] = sampleBilinear(source, vw, vh, bx, by)
            }
        }
    }

    /** One Jacobi pass of pressure projection: an approximate incompressibility solve, cheap by design. */
    private fun project() {
        for (y in 0 until vh) {
            for (x in 0 until vw) {
                val idx = y * vw + x
                val l = velX[clampIndex(x - 1, y, vw, vh) ?: idx]
                val r = velX[clampIndex(x + 1, y, vw, vh) ?: idx]
                val u = velY[clampIndex(x, y - 1, vw, vh) ?: idx]
                val d = velY[clampIndex(x, y + 1, vw, vh) ?: idx]
                divergence[idx] = 0.5f * ((r - l) + (d - u))
                pressure[idx] = 0f
            }
        }
        for (y in 0 until vh) {
            for (x in 0 until vw) {
                val idx = y * vw + x
                val l = pressure[clampIndex(x - 1, y, vw, vh) ?: idx]
                val r = pressure[clampIndex(x + 1, y, vw, vh) ?: idx]
                val u = pressure[clampIndex(x, y - 1, vw, vh) ?: idx]
                val d = pressure[clampIndex(x, y + 1, vw, vh) ?: idx]
                pressure[idx] = (l + r + u + d - divergence[idx]) * 0.25f
            }
        }
        for (y in 0 until vh) {
            for (x in 0 until vw) {
                val idx = y * vw + x
                val l = pressure[clampIndex(x - 1, y, vw, vh) ?: idx]
                val r = pressure[clampIndex(x + 1, y, vw, vh) ?: idx]
                val u = pressure[clampIndex(x, y - 1, vw, vh) ?: idx]
                val d = pressure[clampIndex(x, y + 1, vw, vh) ?: idx]
                velX[idx] -= 0.5f * (r - l)
                velY[idx] -= 0.5f * (d - u)
            }
        }
    }

    private companion object {
        const val HEIGHT_W = 96
        const val HEIGHT_H = 192
        const val VEL_W = 64
        const val VEL_H = 128
        const val WAVE_SPEED_SQ = 0.45f * 0.45f
        const val WAVE_DAMPING = 0.985f
        const val TILT_HEIGHT_BIAS = 0.00002f
        const val TILT_FLOW_BIAS = 0.02f
        const val FLUID_VELOCITY_DISSIPATION = 0.995f
        const val FLUID_DYE_DISSIPATION = 0.992f
        const val SPLAT_VELOCITY_SCALE = 0.08f
        const val SPLAT_DYE_STRENGTH = 0.6f
    }
}

private fun clampIndex(
    x: Int,
    y: Int,
    w: Int,
    h: Int,
): Int? {
    if (x < 0 || x >= w) return null
    if (y < 0 || y >= h) return null
    return y * w + x
}

private fun sampleBilinear(
    field: FloatArray,
    w: Int,
    h: Int,
    fx: Float,
    fy: Float,
): Float {
    val x = fx.coerceIn(0f, (w - 1).toFloat())
    val y = fy.coerceIn(0f, (h - 1).toFloat())
    val x0 = x.toInt()
    val y0 = y.toInt()
    val x1 = min(x0 + 1, w - 1)
    val y1 = min(y0 + 1, h - 1)
    val tx = x - x0
    val ty = y - y0
    val v00 = field[y0 * w + x0]
    val v10 = field[y0 * w + x1]
    val v01 = field[y1 * w + x0]
    val v11 = field[y1 * w + x1]
    val top = v00 + (v10 - v00) * tx
    val bottom = v01 + (v11 - v01) * tx
    return top + (bottom - top) * ty
}

/** Creates and steps a [WaterField] for the caller's subtree: one per screen root, shared via
 * [LocalWaterField] by the caller wrapping its content in `CompositionLocalProvider`. Registers
 * the gravity sensor while the lifecycle is resumed and pauses stepping under reduced motion or
 * `liquidMotion == 0`. */
@Composable
internal fun rememberWaterField(
    liquidMotion: Float,
    reducedMotion: Boolean,
): WaterField {
    val field = remember { WaterField() }
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current

    DisposableEffect(context) {
        val manager = context.getSystemService<SensorManager>()
        val sensor =
            manager?.getDefaultSensor(Sensor.TYPE_GRAVITY)
                ?: manager?.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
        val listener = tiltListener(field)
        if (manager != null && sensor != null) {
            manager.registerListener(listener, sensor, SensorManager.SENSOR_DELAY_UI)
        }
        onDispose { manager?.unregisterListener(listener) }
    }

    LaunchedEffect(liquidMotion, reducedMotion) {
        if (reducedMotion || liquidMotion <= 0f) return@LaunchedEffect
        var lastNanos = 0L
        while (isActive) {
            withFrameNanos { nanos ->
                val resumed = lifecycleOwner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)
                if (resumed) {
                    val dt = if (lastNanos == 0L) FRAME_DT_FALLBACK else ((nanos - lastNanos) / 1e9f).coerceIn(0f, MAX_DT)
                    field.step(dt, liquidMotion)
                }
                lastNanos = nanos
            }
        }
    }
    return field
}

private fun tiltListener(field: WaterField): SensorEventListener =
    object : SensorEventListener {
        override fun onSensorChanged(event: SensorEvent) {
            val gx = (event.values.getOrElse(0) { 0f } / SensorManager.GRAVITY_EARTH).coerceIn(-1f, 1f)
            val gy = (event.values.getOrElse(1) { 0f } / SensorManager.GRAVITY_EARTH).coerceIn(-1f, 1f)
            field.setTilt(gx, -gy)
        }

        override fun onAccuracyChanged(
            sensor: Sensor?,
            accuracy: Int,
        ) = Unit
    }

private const val FRAME_DT_FALLBACK = 1f / 60f
private const val MAX_DT = 1f / 30f
