package dev.geode.render

/**
 * Whole reaction sets for the Customize grid, named by feel (docs/research/music-visual-mapping.md
 * § 3). Each one layers its routes over different time scales so several things move at once on
 * different parts of the music: a hit layer (kick, snare, hats), a level layer, a trend layer
 * (build-up, intensity, melody) and a structure layer (big moments, counted kicks). Picking one
 * replaces the routes; "Back to style DNA" brings the style's own back.
 */
enum class ReactionPreset(
    val title: String,
    val description: String,
    val routes: List<ReactionRoute>,
) {
    PUNCHY(
        "Punchy",
        "Kick-led: the camera pushes and shakes on the kick, the snare cracks the colour, big moments cut.",
        listOf(
            lane(ReactSource.KICK_ENV, LfoTarget.CAM_PUSH, 0.55f, 0.01f, 0.25f),
            lane(ReactSource.KICK_ENV, LfoTarget.CAM_SHAKE, 0.4f, 0.01f, 0.3f),
            lane(ReactSource.SNARE_ENV, LfoTarget.CHROMA_AB, 0.6f, 0.01f, 0.2f),
            lane(ReactSource.HAT_ENV, LfoTarget.PARTICLES, 0.9f, 0.01f, 0.3f),
            lane(ReactSource.BASS_ATT, LfoTarget.ZOOM, 0.1f, 0.03f, 0.3f),
            lane(ReactSource.KICK, LfoTarget.SPAWN, 1f, 0f, 0.2f),
            lane(ReactSource.KICK_4, LfoTarget.CAM_CUT, 1f, 0f, 0.3f),
            lane(ReactSource.HARD_CUT, LfoTarget.SHATTER, 1f, 0f, 0.3f),
            lane(ReactSource.LOUDNESS, LfoTarget.BLOOM, 0.3f, 0.1f, 0.5f),
            ReactionRoute(true, ReactSource.SNARE_TOGGLE, LfoTarget.CAM_ROLL, ReactMode.FOLLOW, 0.25f, ModCurve.LINEAR, 0.06f, 0.06f),
            drive(ReactSource.HIT_TIME, LfoTarget.SPEED),
        ),
    ),
    FLOATY(
        "Floaty",
        "Melody-led: the camera drifts and rises with the tune, colours follow the notes, nothing cuts.",
        listOf(
            drive(ReactSource.CURVED_TIME, LfoTarget.CAM_FLY),
            ReactionRoute(true, ReactSource.MID_TIME, LfoTarget.CAM_YAW, ReactMode.SPIN, 0.25f, ModCurve.LINEAR, 0.6f, 1.2f),
            lane(ReactSource.PITCH_HEIGHT, LfoTarget.CAM_CRANE, 0.6f, 1f, 2f, ModCurve.SMOOTH),
            lane(ReactSource.PITCH_CLASS, LfoTarget.COLOR_SHIFT, 0.6f, 0.4f, 0.9f),
            lane(ReactSource.MID_PRESENCE, LfoTarget.MORPH, 0.4f, 0.6f, 1.4f, ModCurve.SMOOTH),
            lane(ReactSource.GLIDE, LfoTarget.MELT, 0.8f, 0.2f, 1f, ModCurve.SMOOTH),
            lane(ReactSource.INTERVAL, LfoTarget.FALL, 1.2f, 0.2f, 0.8f, ModCurve.SMOOTH),
            lane(ReactSource.NOTE_CHANGE, LfoTarget.SPAWN, 1f, 0f, 0.2f),
            lane(ReactSource.INTENSITY, LfoTarget.SATURATION, 0.3f, 0.8f, 1.6f, ModCurve.SMOOTH),
            lane(ReactSource.VIBRATO, LfoTarget.PARTICLES, 0.7f, 0.1f, 0.6f),
            drive(ReactSource.NOTE_TIME, LfoTarget.SPEED),
        ),
    ),
    BUILDS_AND_DROPS(
        "Builds & drops",
        "Tension and release: the picture tightens and falls in through a build, then bursts at the drop.",
        listOf(
            lane(ReactSource.ENERGY_TREND, LfoTarget.CAM_PUSH, 0.6f, 0.6f, 1.5f, ModCurve.SMOOTH),
            lane(ReactSource.ENERGY_TREND, LfoTarget.FALL, 1f, 0.4f, 0.8f, ModCurve.SMOOTH),
            lane(ReactSource.RETURNED, LfoTarget.FALL, -1.2f, 0.05f, 1.5f),
            lane(ReactSource.ENERGY_TREND, LfoTarget.VIGNETTE, 0.4f, 0.5f, 0.5f),
            lane(ReactSource.INTENSITY, LfoTarget.FRACTAL, 1f, 0.4f, 1.5f, ModCurve.SMOOTH),
            lane(ReactSource.SURGE, LfoTarget.BLOOM, 0.7f, 0.02f, 1.5f),
            lane(ReactSource.SURGE, LfoTarget.SHATTER, 1f, 0f, 0.3f),
            lane(ReactSource.HARD_CUT, LfoTarget.CAM_CUT, 1f, 0f, 0.3f),
            lane(ReactSource.SILENCE, LfoTarget.FREEZE, 1f, 0f, 0.3f),
            lane(ReactSource.KICK_ENV, LfoTarget.CAM_SHAKE, 0.2f, 0.01f, 0.3f),
            drive(ReactSource.CURVED_TIME, LfoTarget.SPEED),
        ),
    ),
    MELODIC(
        "Melodic",
        "The tune paints it: hue from the note, height from the pitch, a new shape on each note change.",
        listOf(
            lane(ReactSource.PITCH_CLASS, LfoTarget.COLOR_SHIFT, 0.7f, 0.3f, 0.7f),
            lane(ReactSource.PITCH_HEIGHT, LfoTarget.CAM_CRANE, 0.5f, 0.6f, 1.2f, ModCurve.SMOOTH),
            lane(ReactSource.PITCH_CONFIDENCE, LfoTarget.SATURATION, 0.35f, 0.6f, 1.2f, ModCurve.SMOOTH),
            ReactionRoute(true, ReactSource.NOTE_CHANGE, LfoTarget.TWIST, ReactMode.JUMP, 0.35f, ModCurve.LINEAR, 0.15f, 0.4f),
            lane(ReactSource.EVENNESS, LfoTarget.TEMPERATURE, 0.4f, 0.8f, 1.6f, ModCurve.SMOOTH),
            lane(ReactSource.RUN_DENSITY, LfoTarget.PARTICLES, 0.8f, 0.1f, 0.6f),
            lane(ReactSource.LEGATO, LfoTarget.MELT, 0.8f, 0.1f, 1f),
            lane(ReactSource.REATTACK, LfoTarget.SPAWN, 1f, 0f, 0.2f),
            lane(ReactSource.INTERVAL, LfoTarget.FALL, 1f, 0.2f, 0.8f, ModCurve.SMOOTH),
            drive(ReactSource.NOTE_TIME, LfoTarget.SPEED),
        ),
    ),
    GLITCH(
        "Glitch",
        "Broken and twitchy: hits split and shatter the picture, freezes on the count.",
        listOf(
            lane(ReactSource.SNARE_ENV, LfoTarget.GLITCH, 0.3f, 0.08f, 0.3f),
            lane(ReactSource.HAT_ENV, LfoTarget.CHROMA_AB, 0.6f, 0.01f, 0.15f),
            lane(ReactSource.FLATNESS, LfoTarget.PIXELATE, 0.35f, 0.1f, 0.4f),
            lane(ReactSource.SNARE_4, LfoTarget.SHATTER, 1f, 0f, 0.3f),
            lane(ReactSource.KICK_8, LfoTarget.FREEZE, 1f, 0f, 0.3f),
            lane(ReactSource.KICK_ENV, LfoTarget.CAM_SHAKE, 0.45f, 0.01f, 0.25f),
            ReactionRoute(true, ReactSource.KICK, LfoTarget.COLOR_SHIFT, ReactMode.JUMP, 0.4f, ModCurve.LINEAR, 0.08f, 0.2f),
            lane(ReactSource.KICK_4, LfoTarget.CAM_CUT, 1f, 0f, 0.3f),
            lane(ReactSource.FLUX, LfoTarget.TURBULENCE, 0.5f, 0.05f, 0.4f),
            drive(ReactSource.HIT_TIME, LfoTarget.SPEED),
        ),
    ),
    FRACTAL_DIVE(
        "Fractal dive",
        "Falls through itself forever: in on the bass, back out with the melody, growing outward as it builds.",
        listOf(
            lane(ReactSource.BASS_ATT, LfoTarget.FALL, 1f, 0.03f, 0.5f),
            lane(ReactSource.PITCH_HEIGHT, LfoTarget.FALL, -0.7f, 0.3f, 0.8f, ModCurve.SMOOTH),
            lane(ReactSource.INTENSITY, LfoTarget.FRACTAL, 1.2f, 0.4f, 1.5f, ModCurve.SMOOTH),
            lane(ReactSource.KICK, LfoTarget.SPAWN, 1f, 0f, 0.2f),
            lane(ReactSource.FLUX, LfoTarget.TWIST, 0.35f, 0.1f, 0.5f, ModCurve.SMOOTH),
            ReactionRoute(true, ReactSource.MID_TIME, LfoTarget.CAM_YAW, ReactMode.SPIN, 0.2f, ModCurve.LINEAR, 0.4f, 0.8f),
            lane(ReactSource.HARD_CUT, LfoTarget.SHATTER, 1f, 0f, 0.3f),
            lane(ReactSource.CENTROID, LfoTarget.COLOR_SHIFT, 0.4f, 0.6f, 1.2f),
            drive(ReactSource.CURVED_TIME, LfoTarget.DIVE_SPEED),
        ),
    ),
    CALM(
        "Calm",
        "Slow and soft: only the slow trends move it, so it breathes rather than jumps.",
        listOf(
            lane(ReactSource.BASS_PRESENCE, LfoTarget.ZOOM, 0.08f, 1f, 2f, ModCurve.SMOOTH),
            lane(ReactSource.MID_PRESENCE, LfoTarget.MORPH, 0.35f, 1.5f, 3f, ModCurve.SMOOTH),
            lane(ReactSource.CENTROID, LfoTarget.TEMPERATURE, 0.35f, 1.5f, 3f, ModCurve.SMOOTH),
            lane(ReactSource.PITCH_HEIGHT, LfoTarget.CAM_CRANE, 0.4f, 1.5f, 3f, ModCurve.SMOOTH),
            lane(ReactSource.INTENSITY, LfoTarget.FRACTAL, 0.6f, 1.5f, 3f, ModCurve.SMOOTH),
            lane(ReactSource.VIBRATO, LfoTarget.MELT, 0.5f, 0.5f, 2f, ModCurve.SMOOTH),
            drive(ReactSource.MID_TIME, LfoTarget.CAM_FLY),
            drive(ReactSource.CURVED_TIME, LfoTarget.SPEED),
        ),
    ),
}

private fun lane(
    source: ReactSource,
    target: LfoTarget,
    amount: Float,
    attack: Float,
    release: Float,
    curve: ModCurve = ModCurve.LINEAR,
) = ReactionRoute(true, source, target, ReactMode.FOLLOW, amount, curve, attack, release)

private fun drive(
    source: ReactSource,
    target: LfoTarget,
) = ReactionRoute(true, source, target, ReactMode.DRIVE, 1f, ModCurve.LINEAR, 0.3f, 0.6f)
