package dev.geode.render

import dev.geode.engine.bridge.GeodeNative
import java.io.File

enum class LfoWave(
    val label: String,
) {
    SINE("Sine"),
    TRIANGLE("Triangle"),
    SAW("Saw"),
    SQUARE("Square"),
    RANDOM("Random steps"),
}

/**
 * What a modulation slot listens to.
 *
 * [LFO] is the free-running oscillator; everything else is a follower on the sound engine's frame
 * (`core/sound`): the bands' fast envelopes, the shaped level, the centroid, the note-start envelope
 * ([TRANSIENT]) and the stereo field. That is what makes "this parameter listens to the treble
 * band" a thing you can set rather than something baked into a scene. Which polarity each starts on
 * is native ([ModTable.naturalPolarity]).
 */
enum class ModSource(
    val label: String,
) {
    LFO("Steady wave"),
    BASS("Bass band"),
    MID("Mid band"),
    TREBLE("Treble band"),
    LEVEL("Level"),
    BRIGHTNESS("Brightness"),
    TRANSIENT("Note starts"),
    STEREO_WIDTH("Stereo width"),
    STEREO_PAN("L/R movement"),
}

enum class ModPolarity(
    val label: String,
) {
    BIPOLAR("Both ways"),
    POSITIVE("Up only"),
    NEGATIVE("Down only"),
}

enum class ModCurve(
    val label: String,
) {
    LINEAR("Linear"),
    EXPONENTIAL("Exponential"),
    LOGARITHMIC("Logarithmic"),
    SMOOTH("S-curve"),
}

/**
 * Every modulation target, in wire order, with its label: the ordinal is what LfoStore saves and
 * native reads, so a new target is appended, never inserted. Everything else about a target (which
 * styles it reaches, the wave a chained one steers, which picker may offer it) is native
 * (core/viz/ModTargets), read through [ModTable], so the shell cannot disagree with what the engine runs.
 */
enum class LfoTarget(
    val label: String,
) {
    NONE("None"),
    SPEED("Speed"),
    ZOOM("Zoom"),
    ROTATION("Rotation"),
    SWAY("Sway"),
    PULSE("Pulse"),
    DRIFT_X("Drift X"),
    DRIFT_Y("Drift Y"),
    WARP("Warp"),
    RIPPLE("Ripple"),
    MORPH("Morph"),
    TWIST("Twist"),
    TILE("Tile"),
    PIXELATE("Pixelate"),
    POSTERIZE("Posterize"),
    COLOR_SHIFT("Hue shift"),
    PALETTE_MIX("Palette blend"),
    SATURATION("Saturation"),
    BRIGHTNESS("Brightness"),
    INTENSITY("Intensity"),
    BLOOM("Bloom"),
    TEMPERATURE("Temperature"),
    TURBULENCE("Turbulence"),
    CHROMA_AB("Chroma AB"),
    VIGNETTE("Vignette"),

    // Was GLITCH: holds the slot so later ordinals match native. Never offered; a saved "GLITCH" loads as dropped.
    RETIRED_25(""),

    FISHEYE("Fisheye"),
    PARTICLE_SIZE("Particle size"),
    TRAIL_LENGTH("Trail length"),
    FLUID_CURL("Fluid curl"),
    FLUID_RADIUS("Fluid splat radius"),
    FLUID_FORCE("Fluid splat force"),
    FLUID_GLOW("Fluid glow"),
    FLUID_FADE("Fluid fade"),
    FLUID_CATCH_PULL("Catch pull"),
    FLUID_CATCH_RADIUS("Catch radius"),
    FLOW_STRENGTH("Flow strength"),
    WATER_RIPPLE("Ripple amp"),
    RIPPLE_OVERLAY("Ripple ovl"),
    LFO1_RATE("Wave 1 speed"),
    LFO1_DEPTH("Wave 1 depth"),
    LFO2_RATE("Wave 2 speed"),
    LFO2_DEPTH("Wave 2 depth"),
    LFO3_RATE("Wave 3 speed"),
    LFO3_DEPTH("Wave 3 depth"),

    // The camera rig's offsets (core/viz/CameraRig). Every family is viewed through the rig,
    // directly or through the composite's flat projection, so native gives them every style.
    CAM_FLY("Camera speed"),
    CAM_ORBIT("Camera swing"),
    CAM_PUSH("Camera push"),
    CAM_YAW("Camera turn"),
    CAM_PITCH("Camera tilt"),
    CAM_ROLL("Camera roll"),
    CAM_FOV("Camera FOV"),
    CAM_SHAKE("Camera shake"),

    // Liquid mode (core/viz/liquid), appended so every wire ordinal above survives.
    FLOW_CURL("Swirl"),
    FLOW_FORCE("Push"),
    DIVE_SPEED("Dive"),
}

/**
 * One modulation slot.
 *
 * The rate is a PERIOD IN SECONDS, not a frequency: "one sweep every 8 seconds" is the thing a
 * person setting up a slow drift actually wants to say, and it is free-running: there is no
 * tempo sync, because a tempo estimate is not a live signal and a wrong one drags the whole look
 * off the music. Defaults and rate range are native's ([ModTable]): a fresh slot starts as core/viz/Lfo's does.
 */
data class LfoConfig(
    val enabled: Boolean = false,
    val source: ModSource = ModSource.LFO,
    val target: LfoTarget = LfoTarget.NONE,
    val wave: LfoWave = LfoWave.SINE,
    val rateSeconds: Float = ModTable.defaultRateSeconds,
    val depth: Float = ModTable.defaultDepth,
    val polarity: ModPolarity = ModPolarity.BIPOLAR,
    val curve: ModCurve = ModCurve.LINEAR,
)

/**
 * The modulation-target table as C++ owns it (core/viz/ModTargets, #642), parsed once like render/scene/Catalogue: per style,
 * which targets each picker may move (the answer, not a scope to work it out from, so the two cannot drift); each source's
 * natural polarity; a slot's defaults and rate range. JVM tests read core/tests/mod_targets.txt via [TEXT_FILE_PROPERTY].
 */
object ModTable {
    const val TEXT_FILE_PROPERTY: String = "geode.modTargets"

    /** A picker for [offeredTo] that is not a wave slot's index: a reaction route, or an envelope. */
    const val REACTION: Int = -2
    const val ENVELOPE: Int = -1

    private val rows: Map<String, List<List<String>>> by lazy {
        val text = System.getProperty(TEXT_FILE_PROPERTY)?.let { File(it).readText() } ?: GeodeNative.modTargetsText()
        val lines = text.lines().filter { it.isNotBlank() }.map { it.split('|') }
        check(lines.firstOrNull() == listOf("modulation", "2")) { "native mod table is ${lines.firstOrNull()}; this shell reads 2" }
        lines.drop(1).groupBy { it[0] }
    }
    private val offers by lazy { column("target", 5).map { it == "1" } }
    private val offerMasks by lazy { rows["reach"].orEmpty().associate { r -> r[1] to LongArray(r.size - 2) { r[it + 2].toLong(16) } } }
    private val natural by lazy { column("source", 3).map { ModPolarity.valueOf(it) } }
    val defaultRateSeconds: Float get() = config(1)
    val minRateSeconds: Float get() = config(2)
    val maxRateSeconds: Float get() = config(3)
    val defaultDepth: Float get() = config(4)

    /** Each row type's names in ordinal order ("target", "source", "polarity", "curve", "wave"); the enums match them. */
    fun names(tag: String): List<String> = column(tag, 2)

    /** Whether moving [target] changes [sceneId] at all (native's reach; an envelope may move anything that does). */
    fun reaches(target: LfoTarget, sceneId: String): Boolean = offeredTo(target, sceneId, ENVELOPE)

    /**
     * Whether [picker] ([REACTION], [ENVELOPE] or a wave slot's index) may move [target] on [sceneId]: native's
     * offer mask (modtargets::offerMask), with a style the catalogue lacks answered by its "*" row.
     */
    fun offeredTo(
        target: LfoTarget,
        sceneId: String,
        picker: Int,
    ): Boolean =
        (offerMasks[sceneId] ?: offerMasks.getValue("*")).getOrElse(picker - REACTION) { 0L } shr target.ordinal and 1L != 0L

    fun offered(target: LfoTarget): Boolean = offers[target.ordinal]

    /** The polarity that makes a source useful the moment it is picked. */
    fun naturalPolarity(source: ModSource): ModPolarity = natural[source.ordinal]

    private fun column(tag: String, field: Int): List<String> = rows[tag].orEmpty().map { it[field] }

    private fun config(field: Int): Float = rows.getValue("config")[0][field].toFloat()
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
