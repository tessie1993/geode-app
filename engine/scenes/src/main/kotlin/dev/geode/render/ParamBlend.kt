package dev.geode.render

import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.bridge.SceneParamsCodec
import dev.geode.render.scene.SceneParams

/**
 * Mixes two full parameter sets.
 *
 * This is what the panel's A/B blend rides on: two snapshots and one slider between them. Floats
 * interpolate; anything that is a CHOICE rather than a quantity (which palette, which fractal,
 * mirror on or off) snaps at the halfway point, because there is no meaningful value between
 * "Ring" and "Star" and pretending otherwise would show a look neither snapshot contains. The
 * [VisualizerRenderer.NOT_FADED] sentinels snap too. The mix itself is native (core/viz/Params
 * `blendParams`, over the same floats the renderer's settings fade glides).
 */
object ParamBlend {
    fun mix(
        a: SceneParams,
        b: SceneParams,
        t: Float,
    ): SceneParams {
        val out = FloatArray(SceneParamsCodec.FIELDS)
        GeodeNative.paramsBlend(SceneParamsCodec.pack(a), SceneParamsCodec.pack(b), t, out)
        return SceneParamsCodec.unpack(out, base = if (t.coerceIn(0f, 1f) < 0.5f) a else b)
    }
}
