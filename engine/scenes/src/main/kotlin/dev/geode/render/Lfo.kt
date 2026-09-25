package dev.geode.render

import dev.geode.render.scene.ParamScope

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

    // Wave three: the native side (Lfo.cpp) now follows live::level() for this
    // source too (nothing keys off a transient) - relabelled, ordinal kept for
    // wire order. CustomizeTabs.kt (outside this unit) is the only picker that
    // lists ModSource.entries, and still offers it under this label.
    TRANSIENT("Level (legacy)"),
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

// Modulation is applied natively every frame (see core/viz/RendererFrame.cpp, which ticks its own
// LfoEngine/AdsrEngine against the configs pushed by geode_viz_set_lfo/geode_viz_set_adsr), so this
// class only needs to hold the config the UI edits and NativeViz.setLfoConfigs forwards; it does not
// run the oscillator/follower/apply logic itself.
class LfoEngine {
    @Volatile
    var configs: List<LfoConfig> = List(SLOTS) { LfoConfig() }

    companion object {
        const val SLOTS: Int = 3
    }
}
