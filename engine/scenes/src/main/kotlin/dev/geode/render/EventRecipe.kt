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
            follow(ReactSource.KICK, LfoTarget.CAM_PUSH, 0.4f, 0.015f, 0.25f),
            follow(ReactSource.KICK, LfoTarget.ZOOM, 0.12f, 0.015f, 0.22f),
        ),
    ),
    SNARE_CRACK(
        "Snare crack",
        "When the snare hits, split the colours",
        listOf(
            follow(ReactSource.SNARE, LfoTarget.CHROMA_AB, 0.5f, 0.01f, 0.18f),
            follow(ReactSource.SNARE, LfoTarget.GLITCH, 0.25f, 0.01f, 0.12f),
        ),
    ),
    HAT_SHIMMER(
        "Hi-hat shimmer",
        "On every hi-hat, sparkle",
        listOf(
            follow(ReactSource.HAT, LfoTarget.PARTICLE_SIZE, 0.3f, 0.01f, 0.12f),
            follow(ReactSource.HAT, LfoTarget.BLOOM, 0.2f, 0.01f, 0.15f),
        ),
    ),
    SUB_WEIGHT(
        "Sub weight",
        "The sub bass bends the picture",
        listOf(
            follow(ReactSource.SUB_BASS, LfoTarget.WARP, 0.35f, 0.06f, 0.4f),
            follow(ReactSource.SUB_BASS, LfoTarget.FLUID_FORCE, 0.5f, 0.05f, 0.4f),
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
        "Long swells open the view",
        listOf(
            follow(ReactSource.SWELL, LfoTarget.CAM_FOV, 0.25f, 0.4f, 0.8f),
            follow(ReactSource.SWELL, LfoTarget.VIGNETTE, -0.3f, 0.4f, 0.8f),
        ),
    ),
    BUILDUP_RUSH(
        "Build-up rush",
        "As a build-up rises, speed up and tense",
        listOf(
            follow(ReactSource.BUILDUP, LfoTarget.SPEED, 0.5f, 0.5f, 0.5f),
            follow(ReactSource.BUILDUP, LfoTarget.CAM_SHAKE, 0.3f, 0.5f, 0.3f),
            ReactionRoute(true, ReactSource.BUILDUP, LfoTarget.ROTATION, ReactMode.SPIN, 0.3f, ModCurve.LINEAR, 0.5f, 0.5f),
        ),
    ),
    DROP_BURST(
        "Drop burst",
        "When the drop lands, burst",
        listOf(
            follow(ReactSource.DROP, LfoTarget.BLOOM, 0.6f, 0.02f, 1.5f),
            follow(ReactSource.DROP, LfoTarget.CAM_SHAKE, 0.6f, 0.02f, 1.2f),
            follow(ReactSource.DROP, LfoTarget.CAM_FLY, 1f, 0.25f, 2.2f),
            follow(ReactSource.DROP, LfoTarget.FLUID_FORCE, 1f, 0.02f, 1.2f),
        ),
    ),
    SECTION_COLOURS(
        "New colours each section",
        "When the song moves on, change colour",
        listOf(
            step(ReactSource.SECTION, LfoTarget.COLOR_SHIFT, 0.33f, 1.2f),
            step(ReactSource.SECTION, LfoTarget.PALETTE_MIX, 0.5f, 1.2f),
        ),
    ),
    BEAT_TURN(
        "Beat turn",
        "Tilt the camera on every beat",
        listOf(step(ReactSource.BEAT, LfoTarget.CAM_ROLL, 0.15f, 0.08f)),
    ),
    TIMBRE_TURN(
        "New sound, new angle",
        "When the sound changes character, look elsewhere",
        listOf(step(ReactSource.TIMBRE_JUMP, LfoTarget.CAM_YAW, 0.4f, 0.8f)),
    ),
    PITCH_TINT(
        "Key change tint",
        "When the pitch moves, nudge the hue",
        listOf(step(ReactSource.PITCH_CHANGE, LfoTarget.COLOR_SHIFT, 0.08f, 0.25f)),
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
        "Bar colour swing",
        "Swing the hue with every bar",
        listOf(follow(ReactSource.BAR_OSC, LfoTarget.COLOR_SHIFT, 0.1f, 0.2f, 0.2f)),
    ),
    HIT_CHAOS(
        "Chaos on hit",
        "Every hit roughs the picture up",
        listOf(
            follow(ReactSource.ONSET, LfoTarget.TURBULENCE, 0.3f, 0.01f, 0.3f),
            follow(ReactSource.ONSET, LfoTarget.GLITCH, 0.15f, 0.01f, 0.3f),
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
