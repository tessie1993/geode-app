package dev.geode.render

import dev.geode.render.scene.BuildStyleReactionsUseCase

/**
 * What a reaction route listens to: one value of the sound engine's frame (`core/sound/SoundFrame.h`).
 * Mirrors `ReactSource` in core/viz/Reactions.hpp; the ordinal is the wire value, so append only.
 *
 * This is the second numbering. The old analyser's 34 sources were replaced wholesale, and routes saved
 * before it are not read back (ReactionRouteCodec's `reactions/2`), so each style restarts on its DNA.
 *
 * A route sees each source as 0..1 (pan and interval -1..1): relative levels halved (typical 0.5, a
 * strong hit 1), shaped values as they are, a clock as how fast it runs, and a trigger ([isEvent]) as an
 * instant rise and a fall over a fraction of a second to a few seconds.
 */
enum class ReactSource(
    val label: String,
    val group: ReactSourceGroup,
    val isEvent: Boolean = false,
) {
    BASS("Bass", ReactSourceGroup.LEVELS),
    MID("Mids", ReactSourceGroup.LEVELS),
    TREBLE("Treble", ReactSourceGroup.LEVELS),
    VOL("Volume", ReactSourceGroup.LEVELS),
    BASS_ATT("Bass, fast", ReactSourceGroup.LEVELS),
    MID_ATT("Mids, fast", ReactSourceGroup.LEVELS),
    TREBLE_ATT("Treble, fast", ReactSourceGroup.LEVELS),
    VOL_ATT("Volume, fast", ReactSourceGroup.LEVELS),
    SUB("Sub", ReactSourceGroup.LEVELS),
    SUB_ATT("Sub, fast", ReactSourceGroup.LEVELS),
    MID_HIGH("Presence band", ReactSourceGroup.LEVELS),
    MID_HIGH_ATT("Presence band, fast", ReactSourceGroup.LEVELS),
    LOW("Low (shaped)", ReactSourceGroup.LEVELS),
    MIDDLE("Middle (shaped)", ReactSourceGroup.LEVELS),
    HIGH("High (shaped)", ReactSourceGroup.LEVELS),
    LEVEL("Level", ReactSourceGroup.LEVELS),
    BASS_PRESENCE("Bass activity", ReactSourceGroup.LEVELS),
    MID_PRESENCE("Mid activity", ReactSourceGroup.LEVELS),
    TREBLE_PRESENCE("Treble activity", ReactSourceGroup.LEVELS),
    INTENSITY("Intensity", ReactSourceGroup.LEVELS),
    PRESENCE("Music playing", ReactSourceGroup.LEVELS),
    BASS_TIME("Bass clock", ReactSourceGroup.CLOCKS),
    MID_TIME("Mid clock", ReactSourceGroup.CLOCKS),
    TREBLE_TIME("Treble clock", ReactSourceGroup.CLOCKS),
    CURVED_TIME("Loudness clock", ReactSourceGroup.CLOCKS),
    HIT_TIME("Hit clock", ReactSourceGroup.CLOCKS),
    NOTE_TIME("Note clock", ReactSourceGroup.CLOCKS),
    LOUDNESS("Loudness", ReactSourceGroup.TEXTURE),
    CENTROID("Brightness", ReactSourceGroup.TEXTURE),
    ROLLOFF("Roll-off", ReactSourceGroup.TEXTURE),
    FLATNESS("Noisiness", ReactSourceGroup.TEXTURE),
    DENSITY_SLOW("Density", ReactSourceGroup.TEXTURE),
    DENSITY_FAST("Density, fast", ReactSourceGroup.TEXTURE),
    FLUX("Spectral flux", ReactSourceGroup.TEXTURE),
    ENERGY_TREND("Rise", ReactSourceGroup.TEXTURE),
    PEAK_FREQ("Peak pitch", ReactSourceGroup.TEXTURE),
    PEAK_MAG("Peak level", ReactSourceGroup.TEXTURE),
    STEREO_WIDTH("Stereo width", ReactSourceGroup.TEXTURE),
    STEREO_PAN("L/R pan", ReactSourceGroup.TEXTURE),
    KICK_ENV("Kick", ReactSourceGroup.DRUMS),
    SNARE_ENV("Snare", ReactSourceGroup.DRUMS),
    HAT_ENV("Hi-hat", ReactSourceGroup.DRUMS),
    ONSET_ENV("Note start", ReactSourceGroup.DRUMS),
    KICK_TOGGLE("Kick toggle", ReactSourceGroup.DRUMS),
    SNARE_TOGGLE("Snare toggle", ReactSourceGroup.DRUMS),
    KICK_RANDOM("Kick random", ReactSourceGroup.DRUMS),
    KICK("Kick hit", ReactSourceGroup.DRUMS, isEvent = true),
    SNARE("Snare hit", ReactSourceGroup.DRUMS, isEvent = true),
    HAT("Hi-hat hit", ReactSourceGroup.DRUMS, isEvent = true),
    KICK_2("Every 2nd kick", ReactSourceGroup.DRUMS, isEvent = true),
    KICK_4("Every 4th kick", ReactSourceGroup.DRUMS, isEvent = true),
    KICK_8("Every 8th kick", ReactSourceGroup.DRUMS, isEvent = true),
    KICK_16("Every 16th kick", ReactSourceGroup.DRUMS, isEvent = true),
    SNARE_2("Every 2nd snare", ReactSourceGroup.DRUMS, isEvent = true),
    SNARE_4("Every 4th snare", ReactSourceGroup.DRUMS, isEvent = true),
    HARD_CUT("Big moment", ReactSourceGroup.EVENTS, isEvent = true),
    ONSET("Onset", ReactSourceGroup.EVENTS, isEvent = true),
    SILENCE("Silence", ReactSourceGroup.EVENTS, isEvent = true),
    RETURNED("Sound returns", ReactSourceGroup.EVENTS, isEvent = true),
    SURGE("Surge", ReactSourceGroup.EVENTS, isEvent = true),
    NOTE_CHANGE("Note change", ReactSourceGroup.MELODY, isEvent = true),
    LEGATO("Slide", ReactSourceGroup.MELODY, isEvent = true),
    REATTACK("Same note again", ReactSourceGroup.MELODY, isEvent = true),
    NOTE_RANDOM("Note random", ReactSourceGroup.MELODY),
    PITCH_CLASS("Pitch class", ReactSourceGroup.MELODY),
    PITCH_HEIGHT("Pitch height", ReactSourceGroup.MELODY),
    PITCH_CONFIDENCE("Pitch clarity", ReactSourceGroup.MELODY),
    INTERVAL("Interval", ReactSourceGroup.MELODY),
    GLIDE("Glide", ReactSourceGroup.MELODY),
    VIBRATO("Vibrato", ReactSourceGroup.MELODY),
    EVENNESS("Evenness", ReactSourceGroup.MELODY),
    RUN_DENSITY("Busy melody", ReactSourceGroup.MELODY),
    EQ_25("25 Hz", ReactSourceGroup.EQ),
    EQ_40("40 Hz", ReactSourceGroup.EQ),
    EQ_60("60 Hz", ReactSourceGroup.EQ),
    EQ_90("90 Hz", ReactSourceGroup.EQ),
    EQ_150("150 Hz", ReactSourceGroup.EQ),
    EQ_240("240 Hz", ReactSourceGroup.EQ),
    EQ_370("370 Hz", ReactSourceGroup.EQ),
    EQ_590("590 Hz", ReactSourceGroup.EQ),
    EQ_930("930 Hz", ReactSourceGroup.EQ),
    EQ_1K5("1.5 kHz", ReactSourceGroup.EQ),
    EQ_2K3("2.3 kHz", ReactSourceGroup.EQ),
    EQ_3K6("3.6 kHz", ReactSourceGroup.EQ),
    EQ_5K6("5.6 kHz", ReactSourceGroup.EQ),
    EQ_8K9("8.9 kHz", ReactSourceGroup.EQ),
    EQ_14K("14 kHz", ReactSourceGroup.EQ),
    EQ_22K("22 kHz", ReactSourceGroup.EQ),
}

/** The Customize picker's sections, in the order it shows them. */
enum class ReactSourceGroup(
    val label: String,
) {
    LEVELS("Levels"),
    DRUMS("Drums"),
    EVENTS("Events"),
    MELODY("Melody"),
    CLOCKS("Clocks"),
    TEXTURE("Texture"),
    EQ("EQ bands"),
}

/**
 * How a route moves its target. Mirrors `ReactMode` in core/viz/Reactions.hpp.
 *
 * [FOLLOW] adds amount × source. [STEP] adds amount once per rising edge (a hue jump, a new
 * camera angle). [SPIN] keeps adding amount × source per second (keeps turning while the sound
 * lasts). Stepped and spun values fold through a triangle wave, so they stay bounded forever.
 * [JUMP] holds a new seeded random value in -amount..amount on each rising edge (sample and
 * hold), so every hit lands somewhere new. [TOGGLE] flips between 0 and amount on each rising
 * edge (a palette swap every sixteen kicks).
 */
enum class ReactMode(
    val label: String,
) {
    FOLLOW("Follow"),
    STEP("Step"),
    SPIN("Spin"),
    JUMP("Jump"),
    TOGGLE("Toggle"),

    /** The music is what moves the target: the slider's rate times how busy the source is (core/viz/ParamMatrix). */
    DRIVE("Drive"),
}

/** One route of the reaction matrix: [source] moves [target] by [amount], smoothed. */
data class ReactionRoute(
    val enabled: Boolean = true,
    val source: ReactSource = ReactSource.BASS,
    val target: LfoTarget = LfoTarget.NONE,
    val mode: ReactMode = ReactMode.FOLLOW,
    val amount: Float = 0.3f,
    val curve: ModCurve = ModCurve.LINEAR,
    val attackSeconds: Float = 0.05f,
    val releaseSeconds: Float = 0.3f,
) {
    companion object {
        /** Mirrors `ReactionEngine::kRoutes`. */
        const val MAX_ROUTES: Int = 64

        const val MIN_AMOUNT: Float = -2f
        const val MAX_AMOUNT: Float = 2f
        const val MAX_ATTACK_SECONDS: Float = 5f
        const val MAX_RELEASE_SECONDS: Float = 10f
    }
}

/**
 * Remembers the last style DNA built, so the GL thread rebuilds it only when the scene or the
 * seed changes, and hands back the same list instance otherwise (callers diff by identity).
 * Single-thread use.
 */
class StyleReactionsCache(
    private val build: BuildStyleReactionsUseCase = BuildStyleReactionsUseCase(),
) {
    private var sceneId: String? = null
    private var seed = 0
    private var routes: List<ReactionRoute> = emptyList()

    fun routesFor(
        sceneId: String,
        seed: Int,
    ): List<ReactionRoute> {
        if (sceneId != this.sceneId || seed != this.seed) {
            this.sceneId = sceneId
            this.seed = seed
            routes = build(sceneId, seed)
        }
        return routes
    }
}
