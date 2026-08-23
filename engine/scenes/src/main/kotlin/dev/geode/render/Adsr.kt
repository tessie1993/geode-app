package dev.geode.render

import dev.geode.analysis.AudioFeatures
import dev.geode.render.scene.SceneParams

enum class EnvBand(
    val label: String,
) {
    BASS("Bass"),
    MID("Mid"),
    TREBLE("Treble"),
    RMS("Level"),
    BRIGHTNESS("Brightness"),
    WIDTH("Stereo width"),
}

data class AdsrConfig(
    val enabled: Boolean = false,
    val targets: List<LfoTarget> = emptyList(),
    val attack: Float = 0.05f,
    val decay: Float = 0.25f,
    val sustain: Float = 0.5f,
    val release: Float = 0.35f,
    val amount: Float = 0.5f,
    val band: EnvBand = EnvBand.BASS,
    val gateThreshold: Float = 0.25f,
    val sustainTrack: Boolean = false,
    val retrigger: Boolean = true,
)

class AdsrEngine {
    @Volatile
    var configs: List<AdsrConfig> = List(COUNT) { AdsrConfig() }

    private val level = FloatArray(COUNT)
    private val stage = IntArray(COUNT)
    private val out = FloatArray(COUNT)

    private val peak = FloatArray(COUNT) { 1f }

    fun tick(
        dt: Float,
        features: AudioFeatures,
    ): FloatArray {
        for (i in 0 until COUNT) {
            val c = configs.getOrNull(i)
            if (c == null || !c.enabled || c.targets.none { it != LfoTarget.NONE }) {
                level[i] = 0f
                stage[i] = 0
                peak[i] = 1f
                out[i] = 0f
                continue
            }
            val energy =
                when (c.band) {
                    EnvBand.BASS -> features.bass
                    EnvBand.MID -> features.mid
                    EnvBand.TREBLE -> features.treble
                    EnvBand.RMS -> features.rms
                    EnvBand.BRIGHTNESS -> LiveSignal.brightness(features)
                    EnvBand.WIDTH -> LiveSignal.width(features)
                }.coerceIn(0f, 1.5f)
            val gateOpen = energy >= c.gateThreshold
            val gateHolds = energy >= c.gateThreshold * 0.85f
            // Triggered by the transient that was actually heard this frame, not by a tempo
            // grid's idea of where the beat should be: on live input, on a track the analyser
            // has not reached, and on anything without a steady pulse, the grid fires late or
            // not at all while the transient is always exactly on the hit.
            val hit = LiveSignal.hit(features)
            if (hit > 0f && (c.retrigger || stage[i] == 0 || stage[i] == 4)) {
                val wasAttacking = stage[i] == 1
                stage[i] = 1
                peak[i] = if (wasAttacking) maxOf(peak[i], hit) else maxOf(hit, level[i])
            }
            val ceiling = peak[i].coerceIn(0f, 1f)
            val sustainTarget =
                if (c.sustainTrack) {
                    (c.sustain * (energy / c.gateThreshold.coerceAtLeast(0.05f)).coerceIn(0f, 1f))
                } else {
                    c.sustain
                } * ceiling
            when (stage[i]) {
                1 -> {
                    level[i] += dt / c.attack.coerceAtLeast(0.005f) * ceiling
                    if (level[i] >= ceiling) {
                        level[i] = ceiling
                        stage[i] = 2
                    }
                }
                2 -> {
                    level[i] -= dt / c.decay.coerceAtLeast(0.005f) * (1f - sustainTarget).coerceAtLeast(0.05f)
                    if (level[i] <= sustainTarget) {
                        level[i] = sustainTarget
                        stage[i] = if (gateHolds) 3 else 4
                    }
                }
                3 -> {
                    level[i] +=
                        (sustainTarget - level[i]) * (dt * 8f).coerceAtMost(1f)
                    if (!gateHolds) stage[i] = 4
                }
                4 -> {
                    level[i] -= dt / c.release.coerceAtLeast(0.005f)
                    if (gateOpen && level[i] > 0.01f) {
                        stage[i] = 3
                    } else if (level[i] <= 0f) {
                        level[i] = 0f
                        stage[i] = 0
                    }
                }
            }
            out[i] = level[i].coerceIn(0f, 1f)
        }
        return out
    }

    companion object {
        const val COUNT = 2

        /** Rate offset a full envelope adds to a chained slot, in Hz. Matches LfoEngine's own. */
        private const val CHAIN_RATE_HZ = 4f

        fun lfoOffsets(
            configs: List<AdsrConfig>,
            envs: FloatArray,
        ): Pair<FloatArray, FloatArray> {
            val rate = FloatArray(LfoEngine.SLOTS)
            val depth = FloatArray(LfoEngine.SLOTS)
            lfoOffsets(configs, envs, rate, depth)
            return rate to depth
        }

        fun lfoOffsets(
            configs: List<AdsrConfig>,
            envs: FloatArray,
            rate: FloatArray,
            depth: FloatArray,
        ) {
            for (i in 0 until LfoEngine.SLOTS) {
                rate[i] = 0f
                depth[i] = 0f
            }
            for (i in envs.indices) {
                val c = configs.getOrNull(i) ?: continue
                if (!c.enabled || envs[i] <= 0f) continue
                val v = envs[i] * c.amount
                for (t in c.targets) {
                    val chain = t.chain ?: continue
                    when (chain.field) {
                        ModChainField.RATE -> rate[chain.slot] += v * CHAIN_RATE_HZ
                        ModChainField.DEPTH -> depth[chain.slot] += v
                    }
                }
            }
        }

        fun apply(
            p: SceneParams,
            configs: List<AdsrConfig>,
            envs: FloatArray,
        ): SceneParams {
            var r = p
            for (i in envs.indices) {
                val c = configs.getOrNull(i) ?: continue
                if (!c.enabled || envs[i] <= 0f) continue
                for (t in c.targets) {
                    if (t == LfoTarget.NONE || t.chain != null) continue
                    r = LfoEngine.applyTarget(r, t, envs[i] * c.amount)
                }
            }
            return r
        }
    }
}
