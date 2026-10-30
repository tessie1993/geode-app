package dev.geode.render.scene

import kotlin.random.Random

/**
 * The six macros at the top of Customize: one knob each, driving several detail parameters at once.
 *
 * A macro has NO state of its own. Its position is read back from the parameters it drives
 * ([Macros.position]), and moving it writes those parameters ([Macros.set]). That is a deliberate
 * break from the "offset layer" most synths use (see docs/CUSTOMIZE_MACROS_RESEARCH.md): with the
 * macro written straight into [SceneParams], the detail sliders move under it, undo/redo, presets,
 * A/B, randomize, export and the wallpaper all keep working with no format change, and a preset
 * saved before macros existed simply shows the knobs where its parameters put them.
 *
 * [neutral] is where the knob sits for the style as authored: 0.5 for the bipolar macros, which
 * go both ways from the default, and 0 for [COMPLEXITY] and [CHAOS], whose defaults are already
 * the clean end of every parameter they drive.
 */
enum class Macro(
    val title: String,
    val hint: String,
    val lowLabel: String,
    val highLabel: String,
    val neutral: Float,
) {
    ENERGY("Energy", "How hard the picture hits with the music.", "Gentle", "Hard", 0.5f),
    SPEED("Speed", "The pace of everything that moves on its own.", "Slow", "Fast", 0.5f),
    COMPLEXITY("Complexity", "Folds, warps and detail layered over the style.", "Clean", "Intricate", 0f),
    MOOD("Mood", "Dark and moody, or bright and euphoric.", "Dark", "Bright", 0.5f),
    SPACE("Space", "Close and intimate, or deep and vast.", "Close", "Vast", 0.5f),
    CHAOS("Chaos", "Turbulence, shake and glitch on top of the look.", "Calm", "Wild", 0f),
}

/**
 * One parameter a macro drives: its value at the knob's bottom ([low]), at [Macro.neutral]
 * ([mid], always the [SceneParams.DEFAULT] value so the authored look reads as neutral) and at
 * the top ([high]). [low] may be above [high] when the parameter runs against the macro (Space
 * zooms OUT as it opens up). [key] is the [ParamKeys] name, which is also how [ParamScope]
 * decides whether the style on screen can show it.
 */
class MacroTarget(
    val key: String,
    val low: Float,
    val mid: Float,
    val high: Float,
    val get: (SceneParams) -> Float,
    val put: (SceneParams, Float) -> SceneParams,
)

object Macros {
    /**
     * Where [macro] sits for [params] on [sceneId], 0..1: the mean of where each live target sits
     * on its own low-mid-high curve. A parameter set by hand past the macro's range counts as
     * the nearest end.
     *
     * Only targets that can say where the knob is are read. A bipolar macro's target with a flat
     * half (Mood's vignette only rises as the mood darkens; it cannot go below zero on the bright
     * side) is a FOLLOWER: the macro writes it but does not read it, because at its default it
     * could be anywhere on that flat half and would drag the reading toward the middle.
     */
    fun position(
        params: SceneParams,
        macro: Macro,
        sceneId: String,
    ): Float {
        val read = targets(macro, sceneId).filter { readsKnob(it, macro) }
        if (read.isEmpty()) return macro.neutral
        val mean = read.map { toKnob(it, it.get(params), macro.neutral) }.average()
        return mean.toFloat().coerceIn(0f, 1f)
    }

    /** False for a [position] follower: see there. */
    fun readsKnob(
        t: MacroTarget,
        macro: Macro,
    ): Boolean = t.mid != t.high && (macro.neutral == 0f || t.mid != t.low)

    /**
     * [params] with [macro] moved to [to].
     *
     * Each target moves in proportion to the room it has left, rather than by the same step: going
     * up, a target closes the same FRACTION of its distance to the top; going down, it keeps the
     * same fraction of its height. That keeps the look's own balance between its parameters, lets
     * both ends of the knob reach both ends of every target, and makes the new position read back
     * as exactly [to]. Callers apply a whole drag to the params the gesture started from, so a
     * drag that comes back to its start restores the look exactly.
     */
    fun set(
        params: SceneParams,
        macro: Macro,
        to: Float,
        sceneId: String,
    ): SceneParams {
        val target = to.coerceIn(0f, 1f)
        val from = position(params, macro, sceneId)
        if (target == from) return params
        var out = params
        for (t in targets(macro, sceneId)) {
            val k = toKnob(t, t.get(params), macro.neutral)
            val moved =
                if (target > from) {
                    k + (1f - k) * (target - from) / (1f - from)
                } else {
                    k * target / from
                }
            if (moved != k) out = t.put(out, fromKnob(t, moved, macro.neutral))
        }
        return out
    }

    /**
     * Every macro at a random position: bipolar ones somewhere in [SURPRISE_BIPOLAR] around the
     * middle, Complexity and Chaos no further than [SURPRISE_UNIPOLAR_MAX]. The ends stay out of
     * reach because "surprise me" should give a different look, not an extreme one.
     */
    fun surprise(
        params: SceneParams,
        sceneId: String,
        rng: Random = Random.Default,
    ): SceneParams =
        Macro.entries.fold(params) { p, m ->
            val to =
                if (m.neutral > 0f) {
                    SURPRISE_BIPOLAR.start + rng.nextFloat() * (SURPRISE_BIPOLAR.endInclusive - SURPRISE_BIPOLAR.start)
                } else {
                    rng.nextFloat() * SURPRISE_UNIPOLAR_MAX
                }
            set(p, m, to, sceneId)
        }

    /**
     * [params] with every parameter [macros] drive back at its default, and nothing else touched.
     *
     * Not the same as moving the knob to [Macro.neutral]: that keeps the look's balance between
     * the targets (one may sit above its default, another below), while this clears them.
     */
    fun reset(
        params: SceneParams,
        macros: Collection<Macro> = Macro.entries,
    ): SceneParams = macros.flatMap(::allTargets).fold(params) { p, t -> t.put(p, t.mid) }

    private val SURPRISE_BIPOLAR = 0.2f..0.8f
    private const val SURPRISE_UNIPOLAR_MAX = 0.55f

    /** The targets of [macro] that the style [sceneId] can actually show. */
    fun targets(
        macro: Macro,
        sceneId: String,
    ): List<MacroTarget> = allTargets(macro).filter { ParamScope.of(it.key).appliesTo(sceneId) }

    /** Every target of [macro], whatever the style. */
    fun allTargets(macro: Macro): List<MacroTarget> = TABLE.getValue(macro)

    private fun toKnob(
        t: MacroTarget,
        value: Float,
        neutral: Float,
    ): Float {
        // Upper half first: a value sitting on a flat lower half (low == mid) reads as neutral.
        val up = fraction(value, t.mid, t.high)
        if (up != null && (up > 0f || t.low == t.mid)) return neutral + (1f - neutral) * up
        val down = fraction(value, t.mid, t.low) ?: return neutral
        return neutral - neutral * down
    }

    /** How far [value] has gone from [from] toward [to], 0..1, or null when it went the other way. */
    private fun fraction(
        value: Float,
        from: Float,
        to: Float,
    ): Float? {
        if (to == from) return null
        val f = (value - from) / (to - from)
        return if (f < 0f) null else f.coerceAtMost(1f)
    }

    private fun fromKnob(
        t: MacroTarget,
        knob: Float,
        neutral: Float,
    ): Float =
        if (knob >= neutral) {
            val f = if (neutral >= 1f) 0f else (knob - neutral) / (1f - neutral)
            t.mid + (t.high - t.mid) * f
        } else {
            val f = (neutral - knob) / neutral
            t.mid + (t.low - t.mid) * f
        }

    private fun target(
        key: String,
        low: Float,
        high: Float,
        get: (SceneParams) -> Float,
        put: (SceneParams, Float) -> SceneParams,
    ) = MacroTarget(key, low, get(SceneParams.DEFAULT), high, get, put)

    /**
     * The mapping, macro to parameters. The research behind each choice is in
     * docs/CUSTOMIZE_MACROS_RESEARCH.md. Ranges stay inside each control's own slider range, so a
     * macro never drives a slider somewhere it cannot be dragged back from, and no parameter
     * belongs to two macros (two knobs sharing one would drag each other around). [COMPLEXITY]
     * and [CHAOS] start at zero, so their [MacroTarget.low] is the default and is never used.
     */
    private val TABLE: Map<Macro, List<MacroTarget>> =
        mapOf(
            Macro.ENERGY to
                listOf(
                    target(ParamKeys.REACT_AMOUNT, 0.2f, 1.8f, { it.reactAmount }) { p, v -> p.copy(reactAmount = v) },
                    target(ParamKeys.AUDIO_DRIVE, 0.4f, 2f, { it.audioDrive }) { p, v -> p.copy(audioDrive = v) },
                    target(ParamKeys.MOTION_AMOUNT, 0.15f, 1f, { it.motionAmount }) { p, v -> p.copy(motionAmount = v) },
                    target(ParamKeys.INTENSITY, 0.7f, 1.2f, { it.intensity }) { p, v -> p.copy(intensity = v) },
                    target(ParamKeys.FLUID_SPLAT_FORCE, 0.4f, 2f, { it.fluidSplatForce }) { p, v -> p.copy(fluidSplatForce = v) },
                    target(ParamKeys.GLOW_FROM_LOUDNESS, 0.1f, 1f, { it.fluidBloomAudio }) { p, v -> p.copy(fluidBloomAudio = v) },
                    target(ParamKeys.RIPPLE_STRENGTH, 0.4f, 2f, { it.waterRippleStrength }) { p, v ->
                        p.copy(waterRippleStrength = v)
                    },
                ),
            Macro.SPEED to
                listOf(
                    target(ParamKeys.SPEED, 0.3f, 2.5f, { it.speed }) { p, v -> p.copy(speed = v) },
                    target(ParamKeys.CAM_FLY, 0.1f, 1.4f, { it.camFly }) { p, v -> p.copy(camFly = v) },
                    target(ParamKeys.MOTION_DRIFT, 0.15f, 0.9f, { it.motionDrift }) { p, v -> p.copy(motionDrift = v) },
                    target(ParamKeys.CYCLE_SPEED, 0.03f, 0.35f, { it.cycleSpeed }) { p, v -> p.copy(cycleSpeed = v) },
                    target(ParamKeys.DIVE_SPEED, 0.1f, 0.9f, { it.endlessZoomSpeed }) { p, v -> p.copy(endlessZoomSpeed = v) },
                    target(ParamKeys.STIRRER_SPEED, 0.3f, 1.9f, { it.fluidStirrerSpeed }) { p, v -> p.copy(fluidStirrerSpeed = v) },
                    target(ParamKeys.WAVE_SPEED, 0.4f, 1.9f, { it.waterWaveSpeed }) { p, v -> p.copy(waterWaveSpeed = v) },
                    target(ParamKeys.WAVE_FLOW, 0.1f, 0.8f, { it.cymaticsFlow }) { p, v -> p.copy(cymaticsFlow = v) },
                ),
            Macro.COMPLEXITY to
                listOf(
                    target(ParamKeys.DOMAIN_WARP, 0f, 0.7f, { it.warp }) { p, v -> p.copy(warp = v) },
                    target(ParamKeys.RIPPLE, 0f, 0.5f, { it.ripple }) { p, v -> p.copy(ripple = v) },
                    target(ParamKeys.MORPH, 0f, 0.6f, { it.morph }) { p, v -> p.copy(morph = v) },
                    target(ParamKeys.TWIST, 0f, 0.5f, { it.twist }) { p, v -> p.copy(twist = v) },
                    target(ParamKeys.TILE, 1f, 2.5f, { it.tile }) { p, v -> p.copy(tile = v) },
                    target(ParamKeys.TRAIL_WARP_LIQUID_ECHO, 0f, 0.5f, { it.trailWarp }) { p, v -> p.copy(trailWarp = v) },
                    target(ParamKeys.FLUID_CURL, 30f, 48f, { it.fluidCurl }) { p, v -> p.copy(fluidCurl = v) },
                    target(ParamKeys.FIELD_SCALE, 3.2f, 6f, { it.cymaticsScale }) { p, v -> p.copy(cymaticsScale = v) },
                ),
            Macro.MOOD to
                listOf(
                    target(ParamKeys.BRIGHTNESS, 0.65f, 1.2f, { it.brightness }) { p, v -> p.copy(brightness = v) },
                    target(ParamKeys.SATURATION, 0.5f, 1.35f, { it.saturation }) { p, v -> p.copy(saturation = v) },
                    target(ParamKeys.TEMPERATURE, -0.5f, 0.4f, { it.temperature }) { p, v -> p.copy(temperature = v) },
                    target(ParamKeys.GAMMA, 0.8f, 1.15f, { it.gamma }) { p, v -> p.copy(gamma = v) },
                    target(ParamKeys.CONTRAST, 1.15f, 0.95f, { it.contrast }) { p, v -> p.copy(contrast = v) },
                    target(ParamKeys.VIGNETTE, 0.6f, 0f, { it.vignette }) { p, v -> p.copy(vignette = v) },
                    target(ParamKeys.FILM_GRAIN, 0.3f, 0f, { it.grain }) { p, v -> p.copy(grain = v) },
                    target(ParamKeys.BLOOM, 0f, 0.15f, { it.bloom }) { p, v -> p.copy(bloom = v) },
                ),
            Macro.SPACE to
                listOf(
                    target(ParamKeys.ZOOM, 1.6f, 0.7f, { it.zoom }) { p, v -> p.copy(zoom = v) },
                    target(ParamKeys.CAM_FOV, 0.75f, 1.4f, { it.camFov }) { p, v -> p.copy(camFov = v) },
                    target(ParamKeys.CAM_ORBIT, 0.1f, 0.9f, { it.camOrbit }) { p, v -> p.copy(camOrbit = v) },
                    target(ParamKeys.TRAIL_LENGTH, 0.3f, 0.85f, { it.trailLength }) { p, v -> p.copy(trailLength = v) },
                    target(ParamKeys.DEPTH, 0.3f, 0.95f, { it.waterDepth }) { p, v -> p.copy(waterDepth = v) },
                ),
            Macro.CHAOS to
                listOf(
                    target(ParamKeys.TURBULENCE, 0f, 1.2f, { it.turbulence }) { p, v -> p.copy(turbulence = v) },
                    target(ParamKeys.SWAY, 0f, 0.7f, { it.sway }) { p, v -> p.copy(sway = v) },
                    target(ParamKeys.CAM_SHAKE, 0f, 0.5f, { it.camShake }) { p, v -> p.copy(camShake = v) },
                    target(ParamKeys.CHROMATIC_ABERRATION, 0f, 0.6f, { it.chromaAb }) { p, v -> p.copy(chromaAb = v) },
                    target(ParamKeys.GLITCH, 0f, 0.15f, { it.glitch }) { p, v -> p.copy(glitch = v) },
                    target(ParamKeys.FISHEYE, 0f, 0.4f, { it.fisheye }) { p, v -> p.copy(fisheye = v) },
                ),
        )
}
