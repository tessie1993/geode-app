package dev.geode.render

import dev.geode.render.scene.SceneParams
import java.lang.reflect.Field
import java.lang.reflect.Modifier

/**
 * Mixes two full parameter sets.
 *
 * This is what the panel's A/B blend rides on: two snapshots and one slider between them. Floats
 * interpolate; anything that is a CHOICE rather than a quantity (which palette, which fractal,
 * mirror on or off) snaps at the halfway point, because there is no meaningful value between
 * "Ring" and "Star" and pretending otherwise would show a look neither snapshot contains.
 *
 * The excluded floats are [VisualizerRenderer.NOT_FADED] — the same set the settings fade skips,
 * for the same reason: they are sentinels or time constants, not quantities to glide.
 */
object ParamBlend {
    private val FLOATS: Array<Field> =
        SceneParams::class.java.declaredFields
            .filter { it.type == Float::class.javaPrimitiveType && !Modifier.isStatic(it.modifiers) }
            .filterNot { it.name in VisualizerRenderer.NOT_FADED }
            .onEach { it.isAccessible = true }
            .toTypedArray()

    fun mix(
        a: SceneParams,
        b: SceneParams,
        t: Float,
    ): SceneParams {
        val k = t.coerceIn(0f, 1f)
        val out = (if (k < 0.5f) a else b).copy()
        for (field in FLOATS) {
            val from = field.getFloat(a)
            field.setFloat(out, from + (field.getFloat(b) - from) * k)
        }
        return out
    }
}
