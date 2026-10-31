package dev.geode.render

/**
 * The ready-made reactions offered in Customize › Events: "when this happens in the music, do that".
 *
 * Every recipe is only a bundle of [ReactionRoute]s, so it runs in the same reaction matrix as
 * the style DNA and the user's own routes: native, on the GL thread, identical in the wallpaper
 * and in exports, and saved wherever routes are saved. A recipe adds no new state, and turning it
 * off takes its routes back out. The amounts and attack/release times come from
 * docs/CUSTOMIZE_MACROS_RESEARCH.md § Event reactions; the flash-prone ones (bloom, brightness,
 * colour steps) still pass through `safety::apply`, which caps flashes per second.
 */
enum class EventRecipe(
    val title: String,
    val sentence: String,
    val routes: List<ReactionRoute>,
) {
    KICK_PUNCH(
        "Kick punch",
        "When the kick hits, push in",
        listOf(
            follow(ReactSource.KICK_ENV, LfoTarget.CAM_PUSH, 0.4f, 0.015f, 0.25f),
            follow(ReactSource.KICK_ENV, LfoTarget.ZOOM, 0.12f, 0.015f, 0.22f),
        ),
    ),
    SNARE_CRACK(
        "Snare crack",
        "When the snare hits, split the colours",
        listOf(
            follow(ReactSource.SNARE_ENV, LfoTarget.CHROMA_AB, 0.5f, 0.01f, 0.18f),
            follow(ReactSource.SNARE_ENV, LfoTarget.GLITCH, 0.25f, 0.01f, 0.12f),
        ),
    ),
    HAT_SHIMMER(
        "Hi-hat shimmer",
        "On every hi-hat, sparkle",
        listOf(
            follow(ReactSource.HAT_ENV, LfoTarget.PARTICLE_SIZE, 0.3f, 0.01f, 0.12f),
            follow(ReactSource.HAT_ENV, LfoTarget.BLOOM, 0.2f, 0.01f, 0.15f),
        ),
    ),
    SUB_WEIGHT(
        "Sub weight",
        "The sub bass bends the picture",
        listOf(
            follow(ReactSource.SUB_ATT, LfoTarget.WARP, 0.35f, 0.06f, 0.4f),
            follow(ReactSource.SUB_ATT, LfoTarget.FLUID_FORCE, 0.5f, 0.05f, 0.4f),
        ),
    ),
    LOUDNESS_GLOW(
        "Loudness glow",
        "The louder it gets, the more it glows",
        listOf(
            follow(ReactSource.LOUDNESS, LfoTarget.BRIGHTNESS, 0.15f, 0.1f, 0.5f),
            follow(ReactSource.LOUDNESS, LfoTarget.BLOOM, 0.25f, 0.1f, 0.5f),
        ),
    ),
    SWELL_BREATHE(
        "Swell breathe",
        "The longer it stays loud, the wider the view",
        listOf(
            follow(ReactSource.INTENSITY, LfoTarget.CAM_FOV, 0.25f, 0.4f, 0.8f),
            follow(ReactSource.INTENSITY, LfoTarget.VIGNETTE, -0.3f, 0.4f, 0.8f),
        ),
    ),
    BUILDUP_RUSH(
        "Build-up rush",
        "As the music rises, speed up and tense",
        listOf(
            follow(ReactSource.ENERGY_TREND, LfoTarget.SPEED, 0.5f, 0.5f, 0.5f),
            follow(ReactSource.ENERGY_TREND, LfoTarget.CAM_SHAKE, 0.3f, 0.5f, 0.3f),
            ReactionRoute(true, ReactSource.ENERGY_TREND, LfoTarget.ROTATION, ReactMode.SPIN, 0.3f, ModCurve.LINEAR, 0.5f, 0.5f),
        ),
    ),
    DROP_BURST(
        "Drop burst",
        "When the music surges back in, burst",
        listOf(
            follow(ReactSource.SURGE, LfoTarget.BLOOM, 0.6f, 0.02f, 1.5f),
            follow(ReactSource.SURGE, LfoTarget.CAM_SHAKE, 0.6f, 0.02f, 1.2f),
            follow(ReactSource.SURGE, LfoTarget.CAM_FLY, 1f, 0.25f, 2.2f),
            follow(ReactSource.SURGE, LfoTarget.FLUID_FORCE, 1f, 0.02f, 1.2f),
        ),
    ),
    SECTION_COLOURS(
        "New colours on big moments",
        "When everything jumps at once, change colour",
        listOf(
            step(ReactSource.HARD_CUT, LfoTarget.COLOR_SHIFT, 0.33f, 1.2f),
            step(ReactSource.HARD_CUT, LfoTarget.PALETTE_MIX, 0.5f, 1.2f),
        ),
    ),
    BEAT_TURN(
        "Kick turn",
        "Tilt the camera on every kick",
        listOf(step(ReactSource.KICK, LfoTarget.CAM_ROLL, 0.15f, 0.08f)),
    ),
    TIMBRE_TURN(
        "New angle every 8 kicks",
        "Every eighth kick, look elsewhere",
        listOf(step(ReactSource.KICK_8, LfoTarget.CAM_YAW, 0.4f, 0.8f)),
    ),
    PITCH_TINT(
        "Melody tint",
        "When the melody moves to a new note, nudge the hue",
        listOf(step(ReactSource.NOTE_CHANGE, LfoTarget.COLOR_SHIFT, 0.08f, 0.25f)),
    ),
    FLUX_TEXTURE(
        "Texture on change",
        "Busy passages ripple and twist",
        listOf(
            follow(ReactSource.FLUX, LfoTarget.RIPPLE, 0.3f, 0.03f, 0.3f),
            follow(ReactSource.FLUX, LfoTarget.TWIST, 0.2f, 0.03f, 0.3f),
        ),
    ),
    STEREO_SWAY(
        "Stereo sway",
        "Follow the sound left and right",
        listOf(
            follow(ReactSource.STEREO_PAN, LfoTarget.CAM_YAW, 0.35f, 0.3f, 0.6f),
            follow(ReactSource.STEREO_WIDTH, LfoTarget.CAM_ORBIT, 0.3f, 0.3f, 0.6f),
        ),
    ),
    BAR_HUE(
        "Colour every 4 kicks",
        "Shift the hue on every fourth kick",
        listOf(step(ReactSource.KICK_4, LfoTarget.COLOR_SHIFT, 0.1f, 0.2f)),
    ),
    HIT_CHAOS(
        "Chaos on hit",
        "Every hit roughs the picture up",
        listOf(
            follow(ReactSource.ONSET, LfoTarget.TURBULENCE, 0.3f, 0.01f, 0.3f),
            follow(ReactSource.ONSET, LfoTarget.GLITCH, 0.15f, 0.01f, 0.3f),
        ),
    ),

    // The sound engine's signals the recipes above leave alone: toggles and randoms per hit, melody,
    // silence and return, slow activity and the music-driven clocks.
    KICK_SWAP(
        "Kick colour swap",
        "Every kick swaps between two colourings",
        listOf(
            follow(ReactSource.KICK_TOGGLE, LfoTarget.COLOR_SHIFT, 0.25f, 0.06f, 0.06f),
            follow(ReactSource.KICK_TOGGLE, LfoTarget.PALETTE_MIX, 0.6f, 0.06f, 0.06f),
        ),
    ),
    KICK_ANGLE(
        "New angle each kick",
        "Every kick looks somewhere new",
        listOf(
            follow(ReactSource.KICK_RANDOM, LfoTarget.CAM_YAW, 0.35f, 0.15f, 0.15f),
            follow(ReactSource.KICK_RANDOM, LfoTarget.CAM_PITCH, 0.15f, 0.15f, 0.15f),
        ),
    ),
    SNARE_ROLL(
        "Roll every 2nd snare",
        "Every second snare rolls the camera",
        listOf(step(ReactSource.SNARE_2, LfoTarget.CAM_ROLL, 0.12f, 0.15f)),
    ),
    MELODY_LIFT(
        "Melody lift",
        "The higher the melody, the higher the camera looks",
        listOf(follow(ReactSource.PITCH_HEIGHT, LfoTarget.CAM_PITCH, 0.3f, 0.3f, 0.6f)),
    ),
    NOTE_HUE(
        "A colour for each note",
        "Each note of the melody has its own hue",
        listOf(follow(ReactSource.PITCH_CLASS, LfoTarget.COLOR_SHIFT, 0.5f, 0.15f, 0.4f)),
    ),
    GLIDE_TWIST(
        "Slides twist",
        "When the pitch slides, twist with it",
        listOf(follow(ReactSource.GLIDE, LfoTarget.TWIST, 0.4f, 0.05f, 0.3f)),
    ),
    VIBRATO_SHIMMER(
        "Vibrato shimmer",
        "A wobbling note makes the picture shimmer",
        listOf(
            follow(ReactSource.VIBRATO, LfoTarget.RIPPLE, 0.3f, 0.1f, 0.3f),
            follow(ReactSource.VIBRATO, LfoTarget.CHROMA_AB, 0.2f, 0.1f, 0.3f),
        ),
    ),
    SLIDE_SMEAR(
        "Slide smear",
        "When a note slides into the next, smear",
        listOf(
            follow(ReactSource.LEGATO, LfoTarget.WARP, 0.25f, 0.05f, 0.5f),
            follow(ReactSource.LEGATO, LfoTarget.TRAIL_LENGTH, 0.4f, 0.05f, 0.5f),
        ),
    ),
    SILENCE_FADE(
        "Fade in the quiet",
        "When the music stops, dim and close in",
        listOf(
            follow(ReactSource.SILENCE, LfoTarget.BRIGHTNESS, -0.4f, 0.3f, 1.5f),
            follow(ReactSource.SILENCE, LfoTarget.VIGNETTE, 0.4f, 0.3f, 1.5f),
        ),
    ),
    RETURN_FLASH(
        "Back with a flash",
        "When the sound comes back, flash and push in",
        listOf(
            follow(ReactSource.RETURNED, LfoTarget.BLOOM, 0.6f, 0.02f, 1.5f),
            follow(ReactSource.RETURNED, LfoTarget.CAM_PUSH, 0.5f, 0.02f, 1.5f),
        ),
    ),
    BASS_PULL(
        "Bass pulls you in",
        "The busier the bass, the closer the view",
        listOf(follow(ReactSource.BASS_PRESENCE, LfoTarget.ZOOM, 0.2f, 1f, 2f)),
    ),
    CLOCK_ORBIT(
        "Orbit with the music",
        "The camera circles as fast as the music moves",
        listOf(
            spin(ReactSource.MID_TIME, LfoTarget.CAM_ORBIT, 0.15f),
            spin(ReactSource.MID_TIME, LfoTarget.ROTATION, 0.1f),
        ),
    ),
    ;

    /** This recipe's routes that the style [sceneId] can actually show. */
    fun routesFor(sceneId: String): List<ReactionRoute> = routes.filter { it.target.scope.appliesTo(sceneId) }

    /** True when every route this recipe would add to [sceneId] is in [current]. */
    fun isOn(
        current: List<ReactionRoute>,
        sceneId: String,
    ): Boolean {
        val mine = routesFor(sceneId)
        return mine.isNotEmpty() && mine.all { r -> current.any { it.sameSlot(r) } }
    }

    /** Whether turning this recipe on leaves the matrix within [ReactionRoute.MAX_ROUTES]. */
    fun fits(
        current: List<ReactionRoute>,
        sceneId: String,
    ): Boolean = current.size + routesFor(sceneId).count { r -> current.none { it.sameSlot(r) } } <= ReactionRoute.MAX_ROUTES

    /**
     * [current] with this recipe turned off if it is on, or on if it is off and [fits].
     *
     * Turning on replaces a route already on the same sound and target rather than stacking a
     * second one, so a recipe tapped over the style DNA's own kick push does not double it.
     */
    fun toggle(
        current: List<ReactionRoute>,
        sceneId: String,
    ): List<ReactionRoute> {
        val mine = routesFor(sceneId)
        if (isOn(current, sceneId)) return current.filterNot { r -> mine.any { it.sameSlot(r) } }
        if (!fits(current, sceneId)) return current
        return current.filterNot { r -> mine.any { it.sameSlot(r) } } + mine
    }

    companion object {
        /** A route in plain words, the way the Events list shows it. */
        fun sentence(route: ReactionRoute): String = "When ${route.source.label} → ${route.target.label}"
    }
}

private fun ReactionRoute.sameSlot(other: ReactionRoute) = source == other.source && target == other.target

private fun follow(
    source: ReactSource,
    target: LfoTarget,
    amount: Float,
    attack: Float,
    release: Float,
) = ReactionRoute(true, source, target, ReactMode.FOLLOW, amount, ModCurve.LINEAR, attack, release)

private fun step(
    source: ReactSource,
    target: LfoTarget,
    amount: Float,
    seconds: Float,
) = ReactionRoute(true, source, target, ReactMode.STEP, amount, ModCurve.LINEAR, seconds, seconds)

private fun spin(
    source: ReactSource,
    target: LfoTarget,
    amount: Float,
) = ReactionRoute(true, source, target, ReactMode.SPIN, amount, ModCurve.LINEAR, 0.5f, 0.5f)
