package dev.geode.render

import dev.geode.render.scene.BuildStyleReactionsUseCase

/**
 * What a reaction route listens to. Mirrors `ReactSource` in core/viz/Reactions.hpp; the ordinal
 * is the wire value, so append only.
 *
 * The continuous sources are each normalised against their own slow running peak, so a quiet
 * track still uses the whole range. The [isEvent] sources are envelopes on a one-hop flag (an
 * onset, a drop, a key change): an instant rise and a fall over a fraction of a second to a few
 * seconds, which is what lets a hit drive a colour jump or a camera push without snapping.
 */
enum class ReactSource(
    val label: String,
    val isEvent: Boolean = false,
) {
    SUB_BASS("Sub bass"),
    BASS("Bass"),
    LOW_MID("Low mids"),
    MID("Mids"),
    HIGH_MID("High mids"),
    PRESENCE("Presence"),
    AIR("Air"),
    LOUDNESS("Loudness"),
    SWELL("Swell"),
    BRIGHTNESS("Brightness"),
    NOISINESS("Noisiness"),
    STEREO_WIDTH("Stereo width"),
    STEREO_PAN("L/R pan"),
    BEAT_OSC("Beat wave"),
    BAR_OSC("Bar wave"),
    TEMPO("Tempo"),
    BUILDUP("Build-up"),
    FLUX("Spectral flux"),
    KICK("Kick"),
    SNARE("Snare"),
    HAT("Hi-hat"),
    ONSET("Onset", isEvent = true),
    BEAT("Beat", isEvent = true),
    DOWNBEAT("Downbeat", isEvent = true),
    DROP("Drop", isEvent = true),
    SECTION("Section change", isEvent = true),
    PITCH_CHANGE("Pitch change", isEvent = true),
    TIMBRE_JUMP("Timbre jump", isEvent = true),

    /** The analyser's macroEnergy (level over its 20 s peak), smoothed over seconds: the song's arc. */
    MACRO_ENERGY("Song energy"),

    /** The music coming back after two quiet seconds. */
    ARRIVAL("Arrival", isEvent = true),

    /**
     * The beat, only in even phrases (eight bars, or less when the section changes). Paired with
     * [PHRASE_BEAT_B], one style answers the beat one way for a phrase and another way for the
     * next. Onsets stand in when no beat has come for two seconds.
     */
    PHRASE_BEAT_A("Beat, phrase A", isEvent = true),

    /** The beat, only in odd phrases. See [PHRASE_BEAT_A]. */
    PHRASE_BEAT_B("Beat, phrase B", isEvent = true),

    /** The beat on counts two and four only. */
    BACKBEAT("Backbeat", isEvent = true),

    /** A new phrase starting. */
    PHRASE("Phrase", isEvent = true),
}

/**
 * How a route moves its target. Mirrors `ReactMode` in core/viz/Reactions.hpp.
 *
 * [FOLLOW] adds amount × source. [STEP] adds amount once per rising edge (a hue jump, a new
 * camera angle). [SPIN] keeps adding amount × source per second (keeps turning while the sound
 * lasts). Stepped and spun values fold through a triangle wave, so they stay bounded forever.
 * [JUMP] holds a new seeded random value in -amount..amount on each rising edge (sample and
 * hold), so every hit lands somewhere new. [TOGGLE] flips between 0 and amount on each rising
 * edge (a palette swap per phrase).
 */
enum class ReactMode(
    val label: String,
) {
    FOLLOW("Follow"),
    STEP("Step"),
    SPIN("Spin"),
    JUMP("Jump"),
    TOGGLE("Toggle"),
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
        const val MAX_ROUTES: Int = 12

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
