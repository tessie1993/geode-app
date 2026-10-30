package dev.geode.render.scene

import dev.geode.render.ModCurve
import dev.geode.render.ReactMode
import dev.geode.render.ReactSource
import dev.geode.render.ReactionRoute
import kotlin.math.abs

/**
 * One parameter a macro knob drives, and by how much: a full turn of the knob moves it [amount]
 * of its slider's span ([MacroParam.span]). Negative runs the parameter against the knob.
 */
data class MacroDestination(
    val key: String,
    val amount: Float,
)

/**
 * A sound that turns the knob by itself: while [source] is heard the macro's destinations move
 * as if the knob were [amount] further round, rising over [attackSeconds] and falling back over
 * [releaseSeconds]. Played as reaction routes (see [MacroKnobs.soundRoutes]).
 */
data class MacroSound(
    val source: ReactSource,
    val amount: Float,
    val attackSeconds: Float = 0.05f,
    val releaseSeconds: Float = 0.4f,
)

/**
 * One twist knob, the way Serum's macros work: a name and icon on the button, any number of
 * [destinations] each with its own amount, and optional [sounds] that push it with the music.
 *
 * A [bipolar] knob rests in the middle ("as styled") and turns both ways; a unipolar one rests at
 * zero and only adds. [value] is where the knob sits now (0..1); it is the knob's own state, kept
 * with the set, and the parameters it drives are written as it turns, so the detail sliders move
 * with it and everything that saves or renders parameters keeps working unchanged.
 */
data class UserMacro(
    val name: String,
    val icon: String = MacroIcons.DEFAULT,
    val bipolar: Boolean = false,
    val value: Float = if (bipolar) BIPOLAR_REST else 0f,
    val destinations: List<MacroDestination> = emptyList(),
    val sounds: List<MacroSound> = emptyList(),
) {
    val rest: Float get() = if (bipolar) BIPOLAR_REST else 0f

    private companion object {
        const val BIPOLAR_REST = 0.5f
    }
}

/** A named set of macro knobs: the standard six, or one the user made. Any number of knobs. */
data class MacroSet(
    val name: String,
    val macros: List<UserMacro>,
)

/** The icon ids a knob can show; the UI maps each to a glyph. Unknown ids fall back to [DEFAULT]. */
object MacroIcons {
    const val DEFAULT = "tune"

    val ALL: List<String> =
        listOf(DEFAULT, "bolt", "speed", "layers", "mood", "space", "chaos", "palette", "camera", "waves", "sparkle", "blur")
}

/** What a knob does to the parameters: pure, so the controller and the tests share it. */
object MacroKnobs {
    private const val EPSILON = 1e-4f

    /**
     * [anchor] with [macro] turned from [from] to [to]: each live destination moves by its amount
     * times the turn, kept inside its slider range. A whole drag is applied to the params it
     * started from, so a drag that comes back to where it began restores the look exactly.
     */
    fun turn(
        anchor: SceneParams,
        macro: UserMacro,
        from: Float,
        to: Float,
        sceneId: String,
    ): SceneParams {
        val turn = to.coerceIn(0f, 1f) - from.coerceIn(0f, 1f)
        if (turn == 0f) return anchor
        var out = anchor
        for (d in macro.destinations) {
            val param = MacroParams.of(d.key)?.takeIf { it.scope.appliesTo(sceneId) } ?: continue
            val moved = param.get(anchor) + d.amount * param.span * turn
            out = param.put(out, moved.coerceIn(param.range))
        }
        return out
    }

    /**
     * Destinations that take [base] to [look] as the knob goes from its rest to the top: "record
     * from the current look". Only controls the style can show, and only those that differ.
     */
    fun capture(
        base: SceneParams,
        look: SceneParams,
        sceneId: String,
        rest: Float,
    ): List<MacroDestination> {
        val travel = 1f - rest
        return MacroParams.liveOn(sceneId).mapNotNull { param ->
            val delta = param.get(look) - param.get(base)
            if (abs(delta) < EPSILON) null else MacroDestination(param.key, (delta / param.span / travel).coerceIn(-1f, 1f))
        }
    }

    /**
     * The reaction routes that play [macro]'s sounds on [sceneId]: one per sound and destination
     * that the reaction matrix can reach, in that target's own units, so a kick moves every
     * destination as far as turning the knob by the sound's amount would.
     */
    fun soundRoutes(
        macro: UserMacro,
        sceneId: String,
    ): List<ReactionRoute> =
        macro.sounds.flatMap { sound ->
            macro.destinations.mapNotNull { d ->
                val param = MacroParams.of(d.key) ?: return@mapNotNull null
                val target = param.reactTarget?.takeIf { it.scope.appliesTo(sceneId) } ?: return@mapNotNull null
                val amount = sound.amount * d.amount * param.span / param.reactScale
                ReactionRoute(
                    enabled = true,
                    source = sound.source,
                    target = target,
                    mode = ReactMode.FOLLOW,
                    amount = amount.coerceIn(ReactionRoute.MIN_AMOUNT, ReactionRoute.MAX_AMOUNT),
                    curve = ModCurve.LINEAR,
                    attackSeconds = sound.attackSeconds,
                    releaseSeconds = sound.releaseSeconds,
                )
            }
        }
}

/**
 * The six knobs every user starts with. The research and the choices behind them are in
 * docs/CUSTOMIZE_MACROS_RESEARCH.md: Energy, Speed, Mood and Space rest in the middle and turn
 * both ways; Complexity and Chaos rest at zero, because every control they drive already sits at
 * its clean end by default. No control belongs to two of them, so one knob never drags another.
 */
object StandardMacros {
    /** A standard destination written as "at the top of the knob, this value". */
    private fun to(
        key: String,
        top: Float,
        bipolar: Boolean,
    ): MacroDestination {
        val param = requireNotNull(MacroParams.of(key)) { key }
        val travel = if (bipolar) 0.5f else 1f
        return MacroDestination(key, ((top - param.get(SceneParams.DEFAULT)) / param.span / travel).coerceIn(-1f, 1f))
    }

    private fun knob(
        name: String,
        icon: String,
        bipolar: Boolean,
        vararg tops: Pair<String, Float>,
    ) = UserMacro(name, icon, bipolar, destinations = tops.map { (k, top) -> to(k, top, bipolar) })

    val SET: MacroSet =
        MacroSet(
            name = "Standard",
            macros =
                listOf(
                    knob(
                        "Energy",
                        "bolt",
                        true,
                        ParamKeys.REACT_AMOUNT to 1.8f,
                        ParamKeys.AUDIO_DRIVE to 2f,
                        ParamKeys.MOTION_AMOUNT to 1f,
                        ParamKeys.INTENSITY to 1.2f,
                        ParamKeys.FLUID_SPLAT_FORCE to 2f,
                        ParamKeys.GLOW_FROM_LOUDNESS to 1f,
                        ParamKeys.RIPPLE_STRENGTH to 1.8f,
                    ),
                    knob(
                        "Speed",
                        "speed",
                        true,
                        ParamKeys.SPEED to 2.5f,
                        ParamKeys.CAM_FLY to 1.4f,
                        ParamKeys.MOTION_DRIFT to 0.9f,
                        ParamKeys.CYCLE_SPEED to 0.35f,
                        ParamKeys.DIVE_SPEED to 0.9f,
                        ParamKeys.STIRRER_SPEED to 1.9f,
                        ParamKeys.WAVE_SPEED to 1.9f,
                        ParamKeys.WAVE_FLOW to 0.8f,
                    ),
                    knob(
                        "Complexity",
                        "layers",
                        false,
                        ParamKeys.DOMAIN_WARP to 0.7f,
                        ParamKeys.RIPPLE to 0.5f,
                        ParamKeys.MORPH to 0.6f,
                        ParamKeys.TWIST to 0.5f,
                        ParamKeys.TILE to 2.5f,
                        ParamKeys.TRAIL_WARP_LIQUID_ECHO to 0.5f,
                        ParamKeys.FLUID_CURL to 48f,
                        ParamKeys.FIELD_SCALE to 6f,
                    ),
                    knob(
                        "Mood",
                        "mood",
                        true,
                        ParamKeys.BRIGHTNESS to 1.2f,
                        ParamKeys.SATURATION to 1.35f,
                        ParamKeys.TEMPERATURE to 0.4f,
                        ParamKeys.GAMMA to 1.15f,
                        ParamKeys.CONTRAST to 0.9f,
                        ParamKeys.VIGNETTE to -0.5f,
                        ParamKeys.FILM_GRAIN to -0.3f,
                        ParamKeys.BLOOM to 0.15f,
                    ),
                    knob(
                        "Space",
                        "space",
                        true,
                        ParamKeys.ZOOM to 0.7f,
                        ParamKeys.CAM_FOV to 1.4f,
                        ParamKeys.CAM_ORBIT to 0.9f,
                        ParamKeys.TRAIL_LENGTH to 0.85f,
                        ParamKeys.DEPTH to 0.95f,
                    ),
                    knob(
                        "Chaos",
                        "chaos",
                        false,
                        ParamKeys.TURBULENCE to 1.2f,
                        ParamKeys.SWAY to 0.7f,
                        ParamKeys.CAM_SHAKE to 0.5f,
                        ParamKeys.CHROMATIC_ABERRATION to 0.6f,
                        ParamKeys.GLITCH to 0.15f,
                        ParamKeys.FISHEYE to 0.4f,
                    ),
                ),
        )
}
