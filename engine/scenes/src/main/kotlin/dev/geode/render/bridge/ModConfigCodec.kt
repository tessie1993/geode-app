package dev.geode.render.bridge

import dev.geode.render.AdsrConfig
import dev.geode.render.LfoConfig

/** The float layouts `geode_viz_set_lfo` and `geode_viz_set_adsr` read; enums travel as ordinals. */
object ModConfigCodec {
    const val LFO_FLOATS = 8
    const val ADSR_HEAD_FLOATS = 10

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

    private fun flag(on: Boolean): Float = if (on) 1f else 0f
}
