package dev.geode.render.bridge

import dev.geode.render.AdsrConfig
import dev.geode.render.LfoConfig
import dev.geode.render.ReactionRoute

/** The float layouts `geode_viz_set_lfo`, `_adsr` and `_reactions` read; enums travel as ordinals. */
object ModConfigCodec {
    const val LFO_FLOATS = 8
    const val ADSR_HEAD_FLOATS = 10

    /** Mirrors GEODE_REACT_ROUTE_FLOATS. */
    const val REACT_ROUTE_FLOATS = 8

    fun packLfo(c: LfoConfig): FloatArray =
        floatArrayOf(
            flag(c.enabled),
            c.source.ordinal.toFloat(),
            c.target.ordinal.toFloat(),
            c.wave.ordinal.toFloat(),
            c.rateSeconds,
            c.depth,
            c.polarity.ordinal.toFloat(),
            c.curve.ordinal.toFloat(),
        )

    fun packAdsr(c: AdsrConfig): FloatArray {
        val out = FloatArray(ADSR_HEAD_FLOATS + c.targets.size)
        out[0] = flag(c.enabled)
        out[1] = c.attack
        out[2] = c.decay
        out[3] = c.sustain
        out[4] = c.release
        out[5] = c.amount
        out[6] = c.band.ordinal.toFloat()
        out[7] = c.gateThreshold
        out[8] = flag(c.sustainTrack)
        out[9] = flag(c.retrigger)
        c.targets.forEachIndexed { i, target -> out[ADSR_HEAD_FLOATS + i] = target.ordinal.toFloat() }
        return out
    }

    /** Every route slot, disabled past the end of [routes], so native never keeps a stale one. */
    fun packReactions(routes: List<ReactionRoute>): FloatArray {
        val out = FloatArray(ReactionRoute.MAX_ROUTES * REACT_ROUTE_FLOATS)
        routes.take(ReactionRoute.MAX_ROUTES).forEachIndexed { i, r ->
            val o = i * REACT_ROUTE_FLOATS
            out[o] = flag(r.enabled)
            out[o + 1] = r.source.ordinal.toFloat()
            out[o + 2] = r.target.ordinal.toFloat()
            out[o + 3] = r.mode.ordinal.toFloat()
            out[o + 4] = r.amount
            out[o + 5] = r.curve.ordinal.toFloat()
            out[o + 6] = r.attackSeconds
            out[o + 7] = r.releaseSeconds
        }
        return out
    }

    private fun flag(on: Boolean): Float = if (on) 1f else 0f
}
