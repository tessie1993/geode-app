package dev.geode.render

import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.bridge.ModConfigCodec
import dev.geode.render.scene.BuildStyleReactionsUseCase

/**
 * The ready-made reactions offered in Customize › Music: "when this happens in the music, do that".
 *
 * Only each recipe's title and sentence live here. Its routes (source, target, amount, attack and
 * release), which of them a style can show, whether it is on, whether it fits the matrix and how a tap
 * merges it into what plays are native beside the style DNA (`EventRecipe` in core/viz/ReactionDna,
 * ADR 0003), so a recipe runs in the same reaction matrix as the DNA and the user's own routes and adds
 * no state of its own. The ordinal is the wire value, so the order is the native enum's: append only.
 */
enum class EventRecipe(
    val title: String,
    val sentence: String,
) {
    KICK_PUNCH("Kick punch", "When the kick hits, push in"),
    SNARE_CRACK("Snare crack", "When the snare hits, split the colours"),
    HAT_SHIMMER("Hi-hat shimmer", "On every hi-hat, sparkle"),
    SUB_WEIGHT("Sub weight", "The sub bass bends the picture"),
    LOUDNESS_GLOW("Loudness glow", "The louder it gets, the more it glows"),
    SWELL_BREATHE("Swell breathe", "The longer it stays loud, the wider the view"),
    BUILDUP_RUSH("Build-up rush", "As the music rises, speed up and tense"),
    SECTION_COLOURS("New colours on big moments", "When everything jumps at once, change colour"),
    BEAT_TURN("Kick turn", "Tilt the camera on every kick"),
    TIMBRE_TURN("New angle every 8 kicks", "Every eighth kick, look elsewhere"),
    PITCH_TINT("Melody tint", "When the melody moves to a new note, nudge the hue"),
    FLUX_TEXTURE("Texture on change", "Busy passages ripple and twist"),
    STEREO_SWAY("Stereo sway", "Follow the sound left and right"),
    BAR_HUE("Colour every 4 kicks", "Shift the hue on every fourth kick"),
    HIT_CHAOS("Chaos on hit", "Every hit roughs the picture up"),
    KICK_SWAP("Kick colour swap", "Every kick swaps between two colourings"),
    KICK_ANGLE("New angle each kick", "Every kick looks somewhere new"),
    SNARE_ROLL("Roll every 2nd snare", "Every second snare rolls the camera"),
    MELODY_LIFT("Melody lift", "The higher the melody, the higher the camera looks"),
    NOTE_HUE("A colour for each note", "Each note of the melody has its own hue"),
    GLIDE_TWIST("Slides twist", "When the pitch slides, twist with it"),
    VIBRATO_SHIMMER("Vibrato shimmer", "A wobbling note makes the picture shimmer"),
    SLIDE_SMEAR("Slide smear", "When a note slides into the next, smear"),
    SILENCE_FADE("Fade in the quiet", "When the music stops, dim and close in"),
    RETURN_FLASH("Back with a flash", "When the sound comes back, flash and push in"),
    BASS_PULL("Bass pulls you in", "The busier the bass, the closer the view"),
    CLOCK_ORBIT("Orbit with the music", "The camera circles as fast as the music moves"),
    BAR_TURN("Turn on the bar", "On the first beat of every bar, turn the view"),
    SCALE_MOOD("Scale mood", "Bright scales lift the light, tense scales stir the picture"),
    BUILD_AND_DROP("Build and drop", "Speed up through the build, glow on the drop"),
    ;

    /** This recipe's routes that the style [sceneId] can actually show (geode_event_recipe_routes). */
    fun routesFor(sceneId: String): List<ReactionRoute> =
        runCatching { ModConfigCodec.unpackReactions(GeodeNative.eventRecipeRoutes(ordinal, sceneId)) }.getOrDefault(emptyList())

    /** True when every route this recipe would add to [sceneId] is in [current]. */
    fun isOn(
        current: List<ReactionRoute>,
        sceneId: String,
    ): Boolean = (state(current, sceneId) and GeodeNative.RECIPE_ON) != 0

    /** Whether turning this recipe on leaves the matrix within [ReactionRoute.MAX_ROUTES]. */
    fun fits(
        current: List<ReactionRoute>,
        sceneId: String,
    ): Boolean = (state(current, sceneId) and GeodeNative.RECIPE_FITS) != 0

    /** [current] with this recipe turned off if it is on, or on if it is off and [fits]; native merges it. */
    fun toggle(
        current: List<ReactionRoute>,
        sceneId: String,
    ): List<ReactionRoute> =
        runCatching {
            ModConfigCodec.unpackReactions(GeodeNative.eventRecipeToggle(ordinal, sceneId, BuildStyleReactionsUseCase.packExact(current)))
        }.getOrDefault(current)

    private fun state(
        current: List<ReactionRoute>,
        sceneId: String,
    ): Int = runCatching { GeodeNative.eventRecipeState(ordinal, sceneId, BuildStyleReactionsUseCase.packExact(current)) }.getOrDefault(0)

    companion object {
        /** A route in plain words, the way the Events list shows it. */
        fun sentence(route: ReactionRoute): String = "When ${route.source.label} → ${route.target.label}"
    }
}
