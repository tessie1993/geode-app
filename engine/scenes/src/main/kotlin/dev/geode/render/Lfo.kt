package dev.geode.render

import dev.geode.analysis.AudioFeatures
import dev.geode.render.scene.ParamScope
import dev.geode.render.scene.SceneParams
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.sin
import kotlin.math.sqrt

enum class LfoWave(
    val label: String,
) {
    SINE("Sin"),
    TRIANGLE("Tri"),
    SAW("Saw"),
    SQUARE("Sqr"),
    RANDOM("S&H"),
}

/**
 * What a modulation slot listens to.
 *
 * [LFO] is the free-running oscillator; everything else is a follower on the LIVE signal, which
 * is what makes "this parameter listens to the treble band" a thing you can set rather than
 * something baked into a scene. [BRIGHTNESS], [STEREO_WIDTH] and [STEREO_PAN] exist because the
 * spec asks the visuals to react to spectral brightness and to left/right movement, and nothing
 * else in the engine reads those.
 */
enum class ModSource(
    val label: String,
) {
    LFO("LFO"),
    BASS("Bass band"),
    MID("Mid band"),
    TREBLE("Treble band"),
    LEVEL("Level"),
    BRIGHTNESS("Brightness"),
    TRANSIENT("Transient"),
    STEREO_WIDTH("Stereo width"),
    STEREO_PAN("L/R movement"),
}

enum class ModPolarity(
    val label: String,
) {
    BIPOLAR("Bipolar"),
    POSITIVE("Positive"),
    NEGATIVE("Negative"),
}

enum class ModCurve(
    val label: String,
) {
    LINEAR("Linear"),
    EXPONENTIAL("Exponential"),
    LOGARITHMIC("Logarithmic"),
    SMOOTH("S-curve"),
}

enum class ModChainField {
    RATE,

    DEPTH,
}

/** A target that steers another modulation slot instead of a scene parameter. */
data class ModChain(
    val slot: Int,
    val field: ModChainField,
)

enum class LfoTarget(
    val label: String,
    val scope: ParamScope,
    val chain: ModChain? = null,
) {
    NONE("None", ParamScope.UNIVERSAL),
    SPEED("Speed", ParamScope.SCENE_CLOCK),
    ZOOM("Zoom", ParamScope.UNIVERSAL),
    ROTATION("Rotation", ParamScope.UNIVERSAL),
    SWAY("Sway", ParamScope.UNIVERSAL),
    PULSE("Pulse", ParamScope.UNIVERSAL),
    DRIFT_X("Drift X", ParamScope.UNIVERSAL),
    DRIFT_Y("Drift Y", ParamScope.UNIVERSAL),
    WARP("Warp", ParamScope.UNIVERSAL),
    RIPPLE("Ripple", ParamScope.UNIVERSAL),
    MORPH("Morph", ParamScope.SHADER_LOOK),
    TWIST("Twist", ParamScope.UNIVERSAL),
    TILE("Tile", ParamScope.UNIVERSAL),
    PIXELATE("Pixelate", ParamScope.UNIVERSAL),
    POSTERIZE("Posterize", ParamScope.UNIVERSAL),
    COLOR_SHIFT("Hue shift", ParamScope.UNIVERSAL),
    PALETTE_MIX("Palette blend", ParamScope.SHADER_LOOK),
    SATURATION("Saturation", ParamScope.UNIVERSAL),
    BRIGHTNESS("Brightness", ParamScope.UNIVERSAL),
    INTENSITY("Intensity", ParamScope.UNIVERSAL),
    BLOOM("Bloom", ParamScope.UNIVERSAL),
    TEMPERATURE("Temperature", ParamScope.UNIVERSAL),
    TURBULENCE("Turbulence", ParamScope.TURBULENCE),
    CHROMA_AB("Chroma AB", ParamScope.UNIVERSAL),
    VIGNETTE("Vignette", ParamScope.UNIVERSAL),
    GLITCH("Glitch", ParamScope.UNIVERSAL),
    FISHEYE("Fisheye", ParamScope.UNIVERSAL),
    PARTICLE_SIZE("Particle size", ParamScope.PARTICLE_SPRITE),
    TRAIL_LENGTH("Trail length", ParamScope.TRAIL_LENGTH),
    FLUID_CURL("Fluid curl", ParamScope.FLUID_SIM),
    FLUID_RADIUS("Fluid splat radius", ParamScope.EMITTERS),
    FLUID_FORCE("Fluid splat force", ParamScope.EMITTERS),
    FLUID_GLOW("Fluid glow", ParamScope.FLUID_SIM),
    FLUID_FADE("Fluid fade", ParamScope.FLUID_SIM),
    FLUID_CATCH_PULL("Catch pull", ParamScope.JOURNEY),
    FLUID_CATCH_RADIUS("Catch radius", ParamScope.JOURNEY),
    FLOW_STRENGTH("Flow strength", ParamScope.UNIVERSAL),
    WATER_RIPPLE("Ripple amp", ParamScope.WATER),
    RIPPLE_OVERLAY("Ripple ovl", ParamScope.RIPPLE_OVERLAY),
    LFO1_RATE("Slot 1 rate", ParamScope.UNIVERSAL, ModChain(0, ModChainField.RATE)),
    LFO1_DEPTH("Slot 1 depth", ParamScope.UNIVERSAL, ModChain(0, ModChainField.DEPTH)),
    LFO2_RATE("Slot 2 rate", ParamScope.UNIVERSAL, ModChain(1, ModChainField.RATE)),
    LFO2_DEPTH("Slot 2 depth", ParamScope.UNIVERSAL, ModChain(1, ModChainField.DEPTH)),
    LFO3_RATE("Slot 3 rate", ParamScope.UNIVERSAL, ModChain(2, ModChainField.RATE)),
    LFO3_DEPTH("Slot 3 depth", ParamScope.UNIVERSAL, ModChain(2, ModChainField.DEPTH)),
}

/**
 * One modulation slot.
 *
 * The rate is a PERIOD IN SECONDS, not a frequency: "one sweep every 8 seconds" is the thing a
 * person setting up a slow drift actually wants to say, and it is free-running — there is no
 * tempo sync, because a tempo estimate is not a live signal and a wrong one drags the whole look
 * off the music.
 */
data class LfoConfig(
    val enabled: Boolean = false,
    val source: ModSource = ModSource.LFO,
    val target: LfoTarget = LfoTarget.NONE,
    val wave: LfoWave = LfoWave.SINE,
    val rateSeconds: Float = DEFAULT_RATE_SECONDS,
    val depth: Float = 0.3f,
    val polarity: ModPolarity = ModPolarity.BIPOLAR,
    val curve: ModCurve = ModCurve.LINEAR,
) {
    companion object {
        const val DEFAULT_RATE_SECONDS: Float = 2f

        const val MIN_RATE_SECONDS: Float = 0.05f

        const val MAX_RATE_SECONDS: Float = 60f

        /** The polarity that makes a source useful the moment it is picked. */
        fun naturalPolarity(source: ModSource): ModPolarity =
            when (source) {
                // An oscillator swings both ways; a follower reads 0 in silence, so a bipolar
                // follower would shove the parameter negative every time the music stops.
                ModSource.LFO, ModSource.STEREO_PAN -> ModPolarity.BIPOLAR
                ModSource.BASS,
                ModSource.MID,
                ModSource.TREBLE,
                ModSource.LEVEL,
                ModSource.BRIGHTNESS,
                ModSource.TRANSIENT,
                ModSource.STEREO_WIDTH,
                -> ModPolarity.POSITIVE
            }
    }
}

class LfoEngine {
    @Volatile
    var configs: List<LfoConfig> = List(SLOTS) { LfoConfig() }

    private val phases = FloatArray(SLOTS)
    private val sampleHold = FloatArray(SLOTS)

    private val totalPhase = FloatArray(SLOTS)
    private val lastCycle = IntArray(SLOTS) { -1 }

    /** Follower smoothing state. Reused every frame — this is the render hot path. */
    private val followed = FloatArray(SLOTS)

    private val out = FloatArray(SLOTS)
    private val rateAdd = FloatArray(SLOTS)
    private val depthAdd = FloatArray(SLOTS)

    fun tick(
        dt: Float,
        features: AudioFeatures,
        extRateAdd: FloatArray? = null,
        extDepthAdd: FloatArray? = null,
    ): FloatArray {
        val cfgs = configs
        for (i in 0 until SLOTS) {
            out[i] = 0f
            rateAdd[i] = if (extRateAdd != null && i < extRateAdd.size) extRateAdd[i] else 0f
            depthAdd[i] = if (extDepthAdd != null && i < extDepthAdd.size) extDepthAdd[i] else 0f
        }
        for (i in 0 until minOf(SLOTS, cfgs.size)) {
            val c = cfgs[i]
            if (!c.enabled || c.target == LfoTarget.NONE) continue
            val depth = (c.depth + depthAdd[i]).coerceIn(0f, 2f)
            val raw =
                when (c.source) {
                    ModSource.LFO -> oscillator(i, c, dt)
                    ModSource.BASS -> follow(i, features.bass, dt)
                    ModSource.MID -> follow(i, features.mid, dt)
                    ModSource.TREBLE -> follow(i, features.treble, dt)
                    ModSource.LEVEL -> follow(i, LiveSignal.level(features), dt)
                    ModSource.BRIGHTNESS -> follow(i, LiveSignal.brightness(features), dt)
                    ModSource.TRANSIENT -> follow(i, LiveSignal.hit(features), dt)
                    ModSource.STEREO_WIDTH -> follow(i, LiveSignal.width(features), dt)
                    ModSource.STEREO_PAN -> followBipolar(i, LiveSignal.pan(features), dt)
                }
            val v = shape(polarized(raw, c.polarity), c.curve) * depth
            out[i] = v
            val chain = c.target.chain ?: continue
            // A slot may only steer a LATER slot: slot 2 driving slot 1's rate would need slot 1
            // to have run first, and one of them would always be a frame behind the other.
            if (i >= chain.slot) continue
            when (chain.field) {
                ModChainField.RATE -> rateAdd[chain.slot] += v * CHAIN_RATE_HZ
                ModChainField.DEPTH -> depthAdd[chain.slot] += v
            }
        }
        return out
    }

    /** Returns the oscillator's raw swing, -1..1. */
    private fun oscillator(
        i: Int,
        c: LfoConfig,
        dt: Float,
    ): Float {
        val period = c.rateSeconds.coerceIn(LfoConfig.MIN_RATE_SECONDS, LfoConfig.MAX_RATE_SECONDS)
        val rate =
            VisualSafety.limitLfoRate(
                (1f / period + rateAdd[i]).coerceIn(MIN_RATE_HZ, MAX_RATE_HZ),
                c.target,
            )
        phases[i] = (phases[i] + rate * dt) % 1f
        totalPhase[i] = (totalPhase[i] + rate * dt) % SH_PHASE_WRAP
        val ph = phases[i]
        return when (c.wave) {
            LfoWave.SINE -> sin(ph * TAU)
            LfoWave.TRIANGLE -> 4f * abs(ph - 0.5f) - 1f
            LfoWave.SAW -> ph * 2f - 1f
            LfoWave.SQUARE -> if (ph < 0.5f) 1f else -1f
            LfoWave.RANDOM -> {
                val cycle = floor(totalPhase[i]).toInt()
                if (cycle != lastCycle[i]) {
                    lastCycle[i] = cycle
                    sampleHold[i] = (Math.random().toFloat() * 2f - 1f)
                }
                sampleHold[i]
            }
        }
    }

    /** Smooths a 0..1 live signal and returns it as a -1..1 swing. */
    private fun follow(
        i: Int,
        value: Float,
        dt: Float,
    ): Float = followBipolar(i, value.coerceIn(0f, 1f) * 2f - 1f, dt)

    /** Smooths a signal that is already -1..1. */
    private fun followBipolar(
        i: Int,
        value: Float,
        dt: Float,
    ): Float {
        val target = value.coerceIn(-1f, 1f)
        val tau = if (target > followed[i]) FOLLOW_RISE_SECONDS else FOLLOW_FALL_SECONDS
        followed[i] += (target - followed[i]) * (dt / tau).coerceIn(0f, 1f)
        return followed[i]
    }

    companion object {
        const val SLOTS: Int = 3

        internal const val SH_PHASE_WRAP = 64f

        private const val TAU = 6.2831853f

        private const val CHAIN_RATE_HZ = 4f

        private const val MIN_RATE_HZ = 0.01f

        private const val MAX_RATE_HZ = 30f

        private const val FOLLOW_RISE_SECONDS = 0.02f

        private const val FOLLOW_FALL_SECONDS = 0.16f

        /** Maps a -1..1 swing onto the half or whole range the slot is set to use. */
        internal fun polarized(
            raw: Float,
            polarity: ModPolarity,
        ): Float =
            when (polarity) {
                ModPolarity.BIPOLAR -> raw
                ModPolarity.POSITIVE -> (raw + 1f) * 0.5f
                ModPolarity.NEGATIVE -> -(raw + 1f) * 0.5f
            }

        /** Bends the response without changing its sign or its endpoints. */
        internal fun shape(
            value: Float,
            curve: ModCurve,
        ): Float {
            val m = abs(value).coerceIn(0f, 1f)
            val shaped =
                when (curve) {
                    ModCurve.LINEAR -> m
                    ModCurve.EXPONENTIAL -> m * m
                    ModCurve.LOGARITHMIC -> sqrt(m)
                    ModCurve.SMOOTH -> m * m * (3f - 2f * m)
                }
            return if (value < 0f) -shaped else shaped
        }

        fun apply(
            p: SceneParams,
            cfgs: List<LfoConfig>,
            values: FloatArray,
        ): SceneParams {
            var r = p
            for (i in values.indices) {
                val c = cfgs.getOrNull(i) ?: continue
                if (!c.enabled) continue
                r = applyTarget(r, c.target, values[i])
            }
            return r
        }

        internal fun applyTarget(
            r: SceneParams,
            target: LfoTarget,
            v: Float,
        ): SceneParams =
            when (target) {
                LfoTarget.SPEED -> r.copy(speed = (r.speed + v).coerceIn(0.05f, 4f))
                LfoTarget.ZOOM -> r.copy(zoom = (r.zoom + v).coerceIn(0.3f, 3f))
                LfoTarget.ROTATION -> r.copy(rotation = (r.rotation + v * 1.5f).coerceIn(-3f, 3f))
                LfoTarget.SWAY -> r.copy(sway = (r.sway + v).coerceIn(0f, 1f))
                LfoTarget.PULSE -> r.copy(pulse = (r.pulse + v).coerceIn(0f, 1f))
                LfoTarget.DRIFT_X -> r.copy(driftX = (r.driftX + v).coerceIn(-1f, 1f))
                LfoTarget.DRIFT_Y -> r.copy(driftY = (r.driftY + v).coerceIn(-1f, 1f))
                LfoTarget.WARP -> r.copy(warp = (r.warp + v).coerceIn(0f, 1f))
                LfoTarget.RIPPLE -> r.copy(ripple = (r.ripple + v).coerceIn(0f, 1f))
                LfoTarget.MORPH -> r.copy(morph = (r.morph + v).coerceIn(0f, 1f))
                LfoTarget.TWIST -> r.copy(twist = (r.twist + v).coerceIn(-1f, 1f))
                LfoTarget.TILE -> r.copy(tile = (r.tile + v * 2f).coerceIn(1f, 6f))
                LfoTarget.PIXELATE -> r.copy(pixelate = (r.pixelate + v).coerceIn(0f, 1f))
                LfoTarget.POSTERIZE -> r.copy(posterize = (r.posterize + v).coerceIn(0f, 1f))
                LfoTarget.COLOR_SHIFT -> r.copy(colorShift = r.colorShift + v * 0.5f)
                LfoTarget.PALETTE_MIX -> r.copy(paletteMix = (r.paletteMix + v).coerceIn(0f, 1f))
                LfoTarget.SATURATION -> r.copy(saturation = (r.saturation + v).coerceIn(0f, 1.5f))
                LfoTarget.BRIGHTNESS -> r.copy(brightness = (r.brightness + v).coerceIn(0.2f, 2f))
                LfoTarget.INTENSITY -> r.copy(intensity = (r.intensity + v).coerceIn(0.2f, 2f))
                LfoTarget.BLOOM -> r.copy(bloom = (r.bloom + v).coerceIn(0f, 1f))
                LfoTarget.TEMPERATURE -> r.copy(temperature = (r.temperature + v).coerceIn(-1f, 1f))
                LfoTarget.TURBULENCE -> r.copy(turbulence = (r.turbulence + v).coerceIn(0f, 1.5f))
                LfoTarget.CHROMA_AB -> r.copy(chromaAb = (r.chromaAb + v).coerceIn(0f, 1f))
                LfoTarget.VIGNETTE -> r.copy(vignette = (r.vignette + v).coerceIn(0f, 1f))
                LfoTarget.GLITCH -> r.copy(glitch = (r.glitch + v).coerceIn(0f, 1f))
                LfoTarget.FISHEYE -> r.copy(fisheye = (r.fisheye + v).coerceIn(-1f, 1f))
                LfoTarget.PARTICLE_SIZE -> r.copy(particleSize = (r.particleSize + v).coerceIn(0.3f, 2.5f))
                LfoTarget.TRAIL_LENGTH -> r.copy(trailLength = (r.trailLength + v).coerceIn(0.05f, 0.98f))
                LfoTarget.FLUID_CURL -> r.copy(fluidCurl = (r.fluidCurl + v * 25f).coerceIn(0f, 50f))
                LfoTarget.FLUID_RADIUS -> r.copy(fluidSplatRadius = (r.fluidSplatRadius + v * 0.15f).coerceIn(0.02f, 0.4f))
                LfoTarget.FLUID_FORCE -> r.copy(fluidSplatForce = (r.fluidSplatForce + v * 1.5f).coerceIn(0f, 3f))
                LfoTarget.FLUID_GLOW -> r.copy(fluidBloomIntensity = (r.fluidBloomIntensity + v).coerceIn(0.1f, 2f))
                LfoTarget.FLUID_FADE -> r.copy(fluidDensityDissipation = (r.fluidDensityDissipation + v * 1.5f).coerceIn(0f, 4f))
                LfoTarget.FLUID_CATCH_PULL -> r.copy(fluidCatchPull = (r.fluidCatchPull + v * 1.5f).coerceIn(0f, 3f))
                LfoTarget.FLUID_CATCH_RADIUS -> r.copy(fluidCatchRadius = (r.fluidCatchRadius + v * 0.12f).coerceIn(0.03f, 0.3f))
                LfoTarget.FLOW_STRENGTH -> r.copy(flowStrength = (r.flowStrength + v).coerceIn(0f, 1f))
                LfoTarget.WATER_RIPPLE -> r.copy(waterRippleStrength = (r.waterRippleStrength + v).coerceIn(0f, 2f))
                LfoTarget.RIPPLE_OVERLAY -> r.copy(rippleOverlayStrength = (r.rippleOverlayStrength + v).coerceIn(0f, 1f))
                // Slots that steer another slot write no scene parameter of their own; LfoEngine.tick
                // has already folded them into that slot's rate/depth.
                LfoTarget.NONE,
                LfoTarget.LFO1_RATE,
                LfoTarget.LFO1_DEPTH,
                LfoTarget.LFO2_RATE,
                LfoTarget.LFO2_DEPTH,
                LfoTarget.LFO3_RATE,
                LfoTarget.LFO3_DEPTH,
                -> r
            }
    }
}
