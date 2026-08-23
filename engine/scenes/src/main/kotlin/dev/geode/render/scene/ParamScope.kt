package dev.geode.render.scene

/**
 * Which styles a Customize parameter can actually move.
 *
 * WHY (R4, no dead controls): a control that does nothing for the style on screen has to be
 * HIDDEN, not greyed out with an apology under it. This is the one place that answers "is this
 * parameter live right now?", and every surface that shows, rolls or modulates a parameter asks
 * it: the Customize tabs, the modulation target picker and the randomizer. Keeping the answer in
 * one enum is what stops the panel drifting back into three different half-truths.
 *
 * [UNIVERSAL] is not a shrug — it means the composite pass applies the parameter to the finished
 * frame for every style (see `composite_frag.glsl` and `CompositeGrade.gateFor`), so it really
 * does reach all of them.
 */
enum class ParamScope {
    /** Applied to the final frame by the composite pass, so every style honours it. */
    UNIVERSAL,

    /** The scene clock. MilkDrop presets pace themselves and Beam draws the signal directly. */
    SCENE_CLOCK,

    /** Scales the band/level envelopes a scene reads. MilkDrop takes raw PCM instead. */
    AUDIO_DRIVE,

    BASS_BAND,

    MID_BAND,

    TREBLE_BAND,

    /** Reads at least one band envelope. Beam and MilkDrop take raw PCM and read none. */
    BAND_GAINS,

    /** Lives in the shader-scene fragment programs (morph, dual palette, colour map, duotone). */
    SHADER_LOOK,

    /**
     * The raymarch step budget, live only on the five marched fragment styles.
     *
     * The only scope whose answer depends on the STYLE rather than the renderer behind it -
     * see [appliesTo] - because Plasma and Vanishing are both [SceneKind.SHADER] and only one
     * of them has a march to spend steps on.
     */
    MARCH_DETAIL,

    /** The endless-dive phase, integrated by the shader scenes and MilkDrop. */
    ENDLESS_ZOOM,

    /** An in-scene force, not a screen effect. */
    TURBULENCE,

    /** Thins Fluid's particle population. */
    DYE_DENSITY,

    /** Whether a persistent scene keeps its previous frame at all. */
    TRAIL_TOGGLE,

    /** How long a persistent scene's afterimage lasts. */
    TRAIL_LENGTH,

    /** Echo zoom/warp, applied by `TrailPass` to the styles that persist their target. */
    TRAIL_ECHO,

    /** Point-sprite look, for the styles that draw particles as sprites. */
    PARTICLE_SPRITE,

    MILKDROP,

    /** The Fluid solver itself. */
    FLUID_SIM,

    /** Splat emitters: Fluid and Water. */
    EMITTERS,

    /** Spawn/catch choreography: Fluid, Curl Flow and Water. */
    JOURNEY,

    WATER,

    CYMATICS,

    BEAM,

    /** The standalone FlowField sim. Fluid drives the field from its own solver instead. */
    FLOW_FIELD_SIM,

    /** The shared ripple overlay. Water's own surface already refracts, so it stays off there. */
    RIPPLE_OVERLAY,
    ;

    fun appliesTo(sceneId: String): Boolean =
        // MARCH_DETAIL is the one scope a SceneKind cannot answer: the marched styles are a
        // subset of one kind, so the id is the only thing that separates them.
        if (this == MARCH_DETAIL) {
            sceneId in SceneCapabilities.MARCHED_SCENES
        } else {
            appliesTo(SceneCapabilities.kindOf(sceneId))
        }

    fun appliesTo(kind: SceneKind): Boolean =
        when (this) {
            UNIVERSAL -> true
            SCENE_CLOCK -> kind != SceneKind.MILKDROP && kind != SceneKind.BEAM
            AUDIO_DRIVE -> kind != SceneKind.MILKDROP
            BASS_BAND -> kind in BASS_READERS
            MID_BAND -> kind in MID_READERS
            TREBLE_BAND -> kind in TREBLE_READERS
            BAND_GAINS -> kind in BASS_READERS || kind in MID_READERS || kind in TREBLE_READERS
            SHADER_LOOK -> kind == SceneKind.SHADER
            // The widest honest kind-level answer: a marched style is always a SHADER, but not
            // every SHADER marches. Callers holding a style id get the exact answer above.
            MARCH_DETAIL -> kind == SceneKind.SHADER
            ENDLESS_ZOOM -> kind == SceneKind.SHADER || kind == SceneKind.MILKDROP
            TURBULENCE -> kind in TURBULENCE_READERS
            DYE_DENSITY -> kind == SceneKind.FLUID
            TRAIL_TOGGLE -> kind == SceneKind.SILK || kind == SceneKind.CURL_FLOW
            TRAIL_LENGTH -> kind == SceneKind.SILK || kind == SceneKind.CURL_FLOW || kind == SceneKind.BEAM
            TRAIL_ECHO -> kind == SceneKind.CURL_FLOW || kind == SceneKind.BEAM
            PARTICLE_SPRITE -> kind == SceneKind.FLUID || kind == SceneKind.CURL_FLOW
            MILKDROP -> kind == SceneKind.MILKDROP
            FLUID_SIM -> kind == SceneKind.FLUID
            EMITTERS -> kind == SceneKind.FLUID || kind == SceneKind.WATER
            JOURNEY -> kind == SceneKind.FLUID || kind == SceneKind.CURL_FLOW || kind == SceneKind.WATER
            WATER -> kind == SceneKind.WATER
            CYMATICS -> kind == SceneKind.CYMATICS
            BEAM -> kind == SceneKind.BEAM
            FLOW_FIELD_SIM -> kind != SceneKind.FLUID
            RIPPLE_OVERLAY -> kind != SceneKind.WATER
        }

    companion object {
        /**
         * The scope of the control named [paramKey] — a [ParamKeys] constant.
         *
         * Everything that shows, searches, rolls or modulates a parameter goes through this, so
         * "does this control do anything here?" has exactly one answer. A key that is not a scene
         * parameter at all (a panel preference, an envelope field) is [UNIVERSAL]: it belongs to
         * the panel rather than to a style, so it is always live.
         */
        fun of(paramKey: String): ParamScope = BY_KEY[paramKey] ?: UNIVERSAL

        private val BY_KEY: Map<String, ParamScope> =
            buildMap {
                // Named `scoped` rather than `put` so nothing has to reason about whether an
                // overload resolves to the local helper or to MutableMap.put.
                fun scoped(
                    scope: ParamScope,
                    vararg keys: String,
                ) = keys.forEach { key -> put(key, scope) }

                scoped(
                    UNIVERSAL,
                    ParamKeys.ZOOM,
                    ParamKeys.ROTATION,
                    ParamKeys.SWAY,
                    ParamKeys.DRIFT_X,
                    ParamKeys.DRIFT_Y,
                    ParamKeys.BEAT_PULSE,
                    ParamKeys.BEAT_SHAKE,
                    ParamKeys.DOMAIN_WARP,
                    ParamKeys.RIPPLE,
                    ParamKeys.TWIST,
                    ParamKeys.KALEIDOSCOPE,
                    ParamKeys.TILE,
                    ParamKeys.PIXELATE,
                    ParamKeys.POSTERIZE,
                    ParamKeys.MIRROR,
                    ParamKeys.BEAT_RESPONSE,
                    ParamKeys.BEAT_FLASH,
                    ParamKeys.PALETTE,
                    ParamKeys.HUE_SHIFT,
                    ParamKeys.HUE_RANGE,
                    ParamKeys.COLOR_CYCLE,
                    ParamKeys.CYCLE_SPEED,
                    ParamKeys.SATURATION,
                    ParamKeys.BRIGHTNESS,
                    ParamKeys.CONTRAST,
                    ParamKeys.GAMMA,
                    ParamKeys.INTENSITY,
                    ParamKeys.TEMPERATURE,
                    ParamKeys.BLOOM,
                    ParamKeys.SOLARIZE,
                    ParamKeys.INVERT,
                    ParamKeys.CHROMATIC_ABERRATION,
                    ParamKeys.VIGNETTE,
                    ParamKeys.SCANLINES,
                    ParamKeys.FILM_GRAIN,
                    ParamKeys.GLITCH,
                    ParamKeys.FISHEYE,
                    ParamKeys.STROBE,
                    ParamKeys.FLOW_STRENGTH,
                    ParamKeys.WAVE_SPEED,
                    ParamKeys.DAMPING,
                )
                scoped(SCENE_CLOCK, ParamKeys.SPEED)
                scoped(AUDIO_DRIVE, ParamKeys.AUDIO_DRIVE)
                scoped(BASS_BAND, ParamKeys.BASS_GAIN)
                scoped(MID_BAND, ParamKeys.MID_GAIN)
                scoped(TREBLE_BAND, ParamKeys.TREBLE_GAIN)
                scoped(MARCH_DETAIL, ParamKeys.MARCH_DETAIL)
                scoped(SHADER_LOOK, ParamKeys.MORPH, ParamKeys.COLOUR_MAP, ParamKeys.PALETTE_2, ParamKeys.PALETTE_BLEND, ParamKeys.DUOTONE)
                scoped(ENDLESS_ZOOM, ParamKeys.ENDLESS_ZOOM, ParamKeys.DIVE_SPEED)
                scoped(TURBULENCE, ParamKeys.TURBULENCE)
                scoped(DYE_DENSITY, ParamKeys.DENSITY)
                scoped(TRAIL_TOGGLE, ParamKeys.TRAILS)
                scoped(TRAIL_LENGTH, ParamKeys.TRAIL_LENGTH)
                scoped(TRAIL_ECHO, ParamKeys.TRAIL_ZOOM_ECHO_IN_OUT, ParamKeys.TRAIL_WARP_LIQUID_ECHO)
                scoped(
                    PARTICLE_SPRITE,
                    ParamKeys.PARTICLE_SHAPE,
                    ParamKeys.PARTICLE_SIZE,
                    ParamKeys.PARTICLE_LIFE_S,
                    ParamKeys.PARTICLE_DRAG,
                )
                scoped(MILKDROP, ParamKeys.MILKDROP_PALETTE_TINT, ParamKeys.BLEND_PRESET_CHANGES)
                scoped(
                    FLUID_SIM,
                    ParamKeys.SOLVER_ITERATIONS,
                    ParamKeys.PRESSURE,
                    ParamKeys.FLUID_CURL,
                    ParamKeys.MOTION_FADE,
                    ParamKeys.FLUID_FADE,
                    ParamKeys.CHROMATIC_AGING,
                    ParamKeys.PALETTE_CYCLE,
                    ParamKeys.PARTICLE_BRIGHTNESS,
                    ParamKeys.SHADING_EMBOSSED_INK,
                    ParamKeys.GLOW_FLUID,
                    ParamKeys.FLUID_GLOW,
                    ParamKeys.GLOW_THRESHOLD,
                    ParamKeys.SUNRAYS,
                    ParamKeys.SUNRAYS_WEIGHT,
                    ParamKeys.CURL_FROM_MIDS,
                    ParamKeys.GLOW_FROM_LOUDNESS,
                    ParamKeys.FADE_WHEN_QUIET,
                )
                scoped(
                    EMITTERS,
                    ParamKeys.BEAT_PATTERN,
                    ParamKeys.BEAT_SPLATS,
                    ParamKeys.STIRRERS,
                    ParamKeys.STIRRER_SPEED,
                    ParamKeys.FLUID_SPLAT_RADIUS,
                    ParamKeys.FLUID_SPLAT_FORCE,
                    ParamKeys.BASS_PUMP,
                    ParamKeys.TREBLE_SPARKLE,
                    ParamKeys.RADIUS_ON_BEAT,
                )
                scoped(
                    JOURNEY,
                    ParamKeys.PATH,
                    ParamKeys.SPAWN_POINTS,
                    ParamKeys.CATCH_POINTS,
                    ParamKeys.CATCH_PULL,
                    ParamKeys.CATCH_RADIUS,
                )
                scoped(FLOW_FIELD_SIM, ParamKeys.FLOW_FORCE, ParamKeys.FLOW_CURL)
                scoped(
                    WATER,
                    ParamKeys.RIPPLE_STRENGTH,
                    ParamKeys.DEPTH,
                    ParamKeys.SPECULAR,
                    ParamKeys.FLOW_DRIFT,
                    ParamKeys.LIQUID,
                    ParamKeys.LIQUID_FLOW,
                    ParamKeys.LIQUID_FADE,
                )
                scoped(RIPPLE_OVERLAY, ParamKeys.RIPPLE_OVERLAY_STRENGTH, ParamKeys.RIPPLE_GLINT)
                scoped(
                    CYMATICS,
                    ParamKeys.GEOMETRY,
                    ParamKeys.FUNDAMENTAL_HZ,
                    ParamKeys.STANDING_WAVES,
                    ParamKeys.TONAL_FOCUS,
                    ParamKeys.PLATE_RING,
                    ParamKeys.FIELD_SCALE,
                    ParamKeys.WAVE_FLOW,
                    ParamKeys.FIELD_SWIRL,
                    ParamKeys.FILL,
                    ParamKeys.NODAL_LINES,
                    ParamKeys.NODAL_GLOW,
                    ParamKeys.IRIDESCENCE,
                    ParamKeys.CAUSTIC_SHEEN,
                )
                scoped(BEAM, ParamKeys.XY_PLOT, ParamKeys.BEAM_WIDTH, ParamKeys.BEAM_BRIGHTNESS, ParamKeys.BEAM_TAIL)
            }

        // Per-band gain only reaches a style that reads that band's envelope. Beam and MilkDrop
        // consume raw PCM and never look at bass/mid/treble at all.
        private val BASS_READERS =
            setOf(
                SceneKind.SHADER,
                SceneKind.SILK,
                SceneKind.MYCELIUM,
                SceneKind.ACID,
                SceneKind.FLUID,
                SceneKind.WATER,
            )

        private val MID_READERS =
            setOf(
                SceneKind.SHADER,
                SceneKind.SILK,
                SceneKind.ACID,
                SceneKind.FLUID,
                SceneKind.CURL_FLOW,
                SceneKind.WATER,
            )

        private val TREBLE_READERS =
            setOf(
                SceneKind.SHADER,
                SceneKind.SILK,
                SceneKind.LIFE,
                SceneKind.MYCELIUM,
                SceneKind.ACID,
                SceneKind.FLUID,
                SceneKind.CURL_FLOW,
                SceneKind.WATER,
                SceneKind.CYMATICS,
            )

        private val TURBULENCE_READERS =
            setOf(
                SceneKind.SHADER,
                SceneKind.MYCELIUM,
                SceneKind.ACID,
                SceneKind.CURL_FLOW,
            )
    }
}
