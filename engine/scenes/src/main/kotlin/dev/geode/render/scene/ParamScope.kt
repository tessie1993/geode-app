package dev.geode.render.scene

/**
 * Which styles a Customize parameter can actually move.
 *
 * WHY (R4, no dead controls): a control that does nothing for the style on screen has to be
 * HIDDEN, not greyed out with an apology under it. This is the one place that answers "is this
 * parameter live right now?", and every surface that shows, rolls or modulates a parameter asks
 * it: the Customize tabs, the modulation target picker and the randomizer.
 *
 * The answers are the native catalogue's (core/viz/Catalogue, through [Catalogue]): which scope
 * each control has, and which scopes reach which styles. The names here match it one for one.
 *
 * [UNIVERSAL] is not a shrug — it means the composite pass applies the parameter to the finished
 * frame for every style (see `composite_frag.glsl` and `CompositeGrade.gateFor`), so it really
 * does reach all of them.
 */
enum class ParamScope {
    /** Applied to the final frame by the composite pass, so every style honours it. */
    UNIVERSAL,

    /** The scene clock. MilkDrop presets pace themselves and read none of it. */
    SCENE_CLOCK,

    /** Scales the band/level envelopes a scene reads. MilkDrop takes raw PCM instead. */
    AUDIO_DRIVE,

    BASS_BAND,

    MID_BAND,

    TREBLE_BAND,

    /** Reads at least one band envelope. MilkDrop takes raw PCM and reads none. */
    BAND_GAINS,

    /** Lives in the shader-scene fragment programs (morph, dual palette, colour map, duotone). */
    SHADER_LOOK,

    /** The raymarch step budget, live only on the marched fragment styles: per style, not per renderer. */
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

    /** The standalone FlowField sim. Fluid drives the field from its own solver instead. */
    FLOW_FIELD_SIM,

    /** The shared ripple overlay. Water's own surface already refracts, so it stays off there. */
    RIPPLE_OVERLAY,
    ;

    fun appliesTo(sceneId: String): Boolean = Catalogue.reaches(this, sceneId)

    companion object {
        /**
         * The scope of the control named [paramKey] — a [ParamKeys] constant.
         *
         * Everything that shows, searches, rolls or modulates a parameter goes through this, so
         * "does this control do anything here?" has exactly one answer. A key that is not a scene
         * parameter at all (a panel preference, an envelope field) is [UNIVERSAL]: it belongs to
         * the panel rather than to a style, so it is always live.
         */
        fun of(paramKey: String): ParamScope = Catalogue.scopeOf(paramKey)
    }
}
