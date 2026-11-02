package dev.geode.ui

import androidx.annotation.StringRes
import dev.geode.R
import dev.geode.render.LfoTarget
import dev.geode.render.ModCurve
import dev.geode.render.ReactMode
import dev.geode.render.ReactSource
import dev.geode.render.ReactionRoute
import kotlin.math.abs

/**
 * A musical moment on Customize › Music: one row ("Kick", "Drop") that the user gives actions to.
 *
 * WHY a moment is a set of [sources] and not one: the style DNA and the director deal routes from
 * every drum signal (the kick's envelope, its hit, every 4th kick, its toggle), and the listener
 * hears all of them as "the kick". So every route whose source is in the set belongs to this row,
 * whoever wrote it, and the sets never overlap, so a route has at most one row. Routes on sources no
 * moment claims (the mids, the stereo pan, an EQ band) are left as they are; Modulate › Reactions
 * still shows them one by one.
 *
 * A new action follows [pulse] (a smooth envelope); the stepped actions (a hue jump, a new camera
 * angle) fire once per [hit], or follow [pulse] when the moment has no single instant (the bass, a
 * build-up, the loudness). [releaseScale] stretches every action's fall for the moments that come
 * rarely and should be felt for longer (a drop, a new section).
 */
internal enum class MusicMoment(
    @StringRes val label: Int,
    val sources: Set<ReactSource>,
    val pulse: ReactSource,
    val hit: ReactSource?,
    val releaseScale: Float = 1f,
) {
    KICK(
        R.string.music_moment_kick,
        setOf(
            ReactSource.KICK_ENV,
            ReactSource.KICK,
            ReactSource.KICK_2,
            ReactSource.KICK_4,
            ReactSource.KICK_8,
            ReactSource.KICK_16,
            ReactSource.KICK_TOGGLE,
            ReactSource.KICK_RANDOM,
        ),
        ReactSource.KICK_ENV,
        ReactSource.KICK,
    ),
    SNARE(
        R.string.music_moment_snare,
        setOf(ReactSource.SNARE_ENV, ReactSource.SNARE, ReactSource.SNARE_2, ReactSource.SNARE_4, ReactSource.SNARE_TOGGLE),
        ReactSource.SNARE_ENV,
        ReactSource.SNARE,
    ),
    HAT(R.string.music_moment_hat, setOf(ReactSource.HAT_ENV, ReactSource.HAT), ReactSource.HAT_ENV, ReactSource.HAT),
    BASS(
        R.string.music_moment_bass,
        setOf(
            ReactSource.BASS,
            ReactSource.BASS_ATT,
            ReactSource.SUB,
            ReactSource.SUB_ATT,
            ReactSource.LOW,
            ReactSource.BASS_PRESENCE,
            ReactSource.BASS_TIME,
        ),
        ReactSource.BASS_ATT,
        null,
    ),
    DROP(
        R.string.music_moment_drop,
        setOf(ReactSource.DROP, ReactSource.SURGE, ReactSource.RETURNED),
        ReactSource.DROP,
        ReactSource.DROP,
        releaseScale = 4f,
    ),
    BUILD_UP(
        R.string.music_moment_build_up,
        setOf(ReactSource.BUILD_UP, ReactSource.ENERGY_TREND),
        ReactSource.BUILD_UP,
        null,
        releaseScale = 2f,
    ),
    CHORD_CHANGE(
        R.string.music_moment_chord_change,
        setOf(ReactSource.HARMONIC_CHANGE, ReactSource.CHORD_HUE, ReactSource.TENSION),
        ReactSource.HARMONIC_CHANGE,
        ReactSource.HARMONIC_CHANGE,
        releaseScale = 2f,
    ),
    NEW_SECTION(
        R.string.music_moment_new_section,
        setOf(
            ReactSource.SECTION_CHANGE,
            ReactSource.HARD_CUT,
            ReactSource.PHRASE,
            ReactSource.SECTION_STATE,
            ReactSource.SECTION_ID,
        ),
        ReactSource.SECTION_CHANGE,
        ReactSource.SECTION_CHANGE,
        releaseScale = 3f,
    ),
    NOTE(
        R.string.music_moment_note,
        setOf(
            ReactSource.NOTE_CHANGE,
            ReactSource.REATTACK,
            ReactSource.LEGATO,
            ReactSource.NOTE_RANDOM,
            ReactSource.PITCH_CLASS,
            ReactSource.PITCH_HEIGHT,
            ReactSource.GLIDE,
            ReactSource.VIBRATO,
        ),
        ReactSource.NOTE_CHANGE,
        ReactSource.NOTE_CHANGE,
    ),
    LOUDNESS(
        R.string.music_moment_loudness,
        setOf(
            ReactSource.LOUDNESS,
            ReactSource.VOL,
            ReactSource.VOL_ATT,
            ReactSource.LEVEL,
            ReactSource.INTENSITY,
            ReactSource.CURVED_TIME,
        ),
        ReactSource.LOUDNESS,
        null,
        releaseScale = 2f,
    ),
}

/**
 * What a moment can do to the picture. [target] is what a new route moves; [family] is every
 * target that reads as this action on screen, so a DNA route on Camera push lights "Zoom" and
 * turning "Zoom" off takes it away. [stepped] actions jump once per hit instead of following it.
 * [amount] is the route's amount at Strength 1×.
 */
internal enum class MusicAction(
    @StringRes val label: Int,
    val target: LfoTarget,
    val family: Set<LfoTarget>,
    val amount: Float,
    val attackSeconds: Float,
    val releaseSeconds: Float,
    val stepped: Boolean = false,
) {
    PULSE(R.string.music_action_pulse, LfoTarget.PULSE, setOf(LfoTarget.PULSE), 0.3f, 0.015f, 0.25f),
    FLASH(R.string.music_action_flash, LfoTarget.BLOOM, setOf(LfoTarget.BLOOM, LfoTarget.BRIGHTNESS), 0.4f, 0.01f, 0.3f),
    ZOOM(R.string.music_action_zoom, LfoTarget.ZOOM, setOf(LfoTarget.ZOOM, LfoTarget.CAM_PUSH, LfoTarget.CAM_FOV), 0.15f, 0.02f, 0.3f),
    SHAKE(R.string.music_action_shake, LfoTarget.CAM_SHAKE, setOf(LfoTarget.CAM_SHAKE), 0.3f, 0.01f, 0.3f),
    COLOUR(
        R.string.music_action_colour,
        LfoTarget.COLOR_SHIFT,
        setOf(LfoTarget.COLOR_SHIFT, LfoTarget.PALETTE_MIX, LfoTarget.SATURATION, LfoTarget.TEMPERATURE),
        0.15f,
        0.1f,
        0.5f,
        stepped = true,
    ),
    CAMERA(
        R.string.music_action_camera,
        LfoTarget.CAM_YAW,
        setOf(LfoTarget.CAM_YAW, LfoTarget.CAM_PITCH, LfoTarget.CAM_ROLL, LfoTarget.CAM_ORBIT, LfoTarget.CAM_FLY),
        0.25f,
        0.3f,
        0.6f,
        stepped = true,
    ),
    WARP(
        R.string.music_action_warp,
        LfoTarget.WARP,
        setOf(LfoTarget.WARP, LfoTarget.TWIST, LfoTarget.RIPPLE, LfoTarget.TURBULENCE),
        0.3f,
        0.03f,
        0.3f,
    ),
    ;

    /** The route this action adds to [moment] at [strength]. */
    fun routeFor(
        moment: MusicMoment,
        strength: Float,
    ): ReactionRoute {
        val hit = moment.hit
        val scaled = (this.amount * strength).coerceIn(ReactionRoute.MIN_AMOUNT, ReactionRoute.MAX_AMOUNT)
        val release = (releaseSeconds * moment.releaseScale).coerceAtMost(ReactionRoute.MAX_RELEASE_SECONDS)
        if (stepped && hit != null) {
            return ReactionRoute(true, hit, target, ReactMode.STEP, scaled, ModCurve.LINEAR, release, release)
        }
        // A stepped action on a moment with no instant follows it, on the target that reads as a move.
        val follows = if (stepped && this == CAMERA) LfoTarget.CAM_ORBIT else target
        return ReactionRoute(true, moment.pulse, follows, ReactMode.FOLLOW, scaled, ModCurve.LINEAR, attackSeconds, release)
    }

    companion object {
        /** The action [target] reads as, or null for a target no action claims. */
        fun of(target: LfoTarget): MusicAction? = entries.firstOrNull { target in it.family }
    }
}

/**
 * The Music page's edits, as pure list maths over the reaction matrix.
 *
 * WHY no new state: "the hand wins per moment" is kept in the one list the app already saves (the
 * user's routes, `LfoStore`). The style's DNA is the Auto; a moment is the user's own ("Mine") when
 * its routes in that list differ from the DNA's, and putting a moment back copies the DNA's routes
 * for it in again. A list that ends up equal to the DNA becomes null, so the matrix goes back to
 * following the DNA live (a new seed, a new style) rather than a frozen copy of it.
 */
internal object MusicMoments {
    /** The routes of [routes] that belong to [moment]. */
    fun routesOf(
        moment: MusicMoment,
        routes: List<ReactionRoute>,
    ): List<ReactionRoute> = routes.filter { it.source in moment.sources }

    /** True when the user's [reactions] give [moment] something other than the style's [dna]. */
    fun isMine(
        moment: MusicMoment,
        reactions: List<ReactionRoute>?,
        dna: List<ReactionRoute>,
    ): Boolean = reactions != null && !sameRoutes(routesOf(moment, reactions), routesOf(moment, dna))

    /** Whether [action] is one of the things [moment] does in [routes]. */
    fun isOn(
        moment: MusicMoment,
        action: MusicAction,
        routes: List<ReactionRoute>,
    ): Boolean = routesOf(moment, routes).any { it.enabled && it.target in action.family }

    /** Whether turning [action] on for [moment] still fits the matrix's [ReactionRoute.MAX_ROUTES]. */
    fun fits(
        moment: MusicMoment,
        action: MusicAction,
        routes: List<ReactionRoute>,
    ): Boolean {
        val added = action.routeFor(moment, 1f)
        return routes.size + (if (routes.any { it.sameSlot(added) }) 0 else 1) <= ReactionRoute.MAX_ROUTES
    }

    /**
     * How strongly [moment] acts now: its routes' amounts against each action's 1× amount, averaged.
     * 1× when it does nothing, so the first action it is given starts at the usual strength.
     */
    fun strength(
        moment: MusicMoment,
        routes: List<ReactionRoute>,
    ): Float {
        val mine = routesOf(moment, routes)
        if (mine.isEmpty()) return 1f
        val mean = mine.map { abs(it.amount) / (MusicAction.of(it.target)?.amount ?: DEFAULT_AMOUNT) }.average()
        return mean.toFloat().coerceIn(0f, MAX_STRENGTH)
    }

    /**
     * [routes] with [action] on [moment] turned off when it is on (every route of its family), or
     * on at the moment's [strength] when it is off and fits. The result is the user's list, or null
     * when it matches [dna] again.
     */
    fun toggle(
        moment: MusicMoment,
        action: MusicAction,
        routes: List<ReactionRoute>,
        dna: List<ReactionRoute>,
    ): List<ReactionRoute>? {
        val next =
            if (isOn(moment, action, routes)) {
                routes.filterNot { it.source in moment.sources && it.target in action.family }
            } else {
                if (!fits(moment, action, routes)) return settle(routes, dna)
                val added = action.routeFor(moment, strength(moment, routes))
                routes.filterNot { it.sameSlot(added) } + added
            }
        return settle(next, dna)
    }

    /** [routes] with every route of [moment] scaled so the moment's [strength] reads [value]. */
    fun withStrength(
        moment: MusicMoment,
        value: Float,
        routes: List<ReactionRoute>,
        dna: List<ReactionRoute>,
    ): List<ReactionRoute>? {
        val now = strength(moment, routes)
        val next =
            routes.map { r ->
                if (r.source !in moment.sources) return@map r
                val amount =
                    if (now > MIN_SCALABLE) {
                        r.amount * value / now
                    } else {
                        // Every amount is ~0, so a ratio means nothing: start again from each action's 1× amount.
                        (MusicAction.of(r.target)?.amount ?: DEFAULT_AMOUNT) * value
                    }
                r.copy(amount = amount.coerceIn(ReactionRoute.MIN_AMOUNT, ReactionRoute.MAX_AMOUNT))
            }
        return settle(next, dna)
    }

    /**
     * The user's [reactions] with [moment] handed back to Auto: its routes replaced by the DNA's for
     * it, as many as fit. Null (all Auto) when nothing else is the user's.
     */
    fun resetToAuto(
        moment: MusicMoment,
        reactions: List<ReactionRoute>?,
        dna: List<ReactionRoute>,
    ): List<ReactionRoute>? {
        val base = reactions ?: return null
        val others = base.filterNot { it.source in moment.sources }
        val room = (ReactionRoute.MAX_ROUTES - others.size).coerceAtLeast(0)
        return settle(others + routesOf(moment, dna).take(room), dna)
    }

    private fun settle(
        next: List<ReactionRoute>,
        dna: List<ReactionRoute>,
    ): List<ReactionRoute>? = if (sameRoutes(next, dna)) null else next

    /** The same routes, in any order. */
    private fun sameRoutes(
        a: List<ReactionRoute>,
        b: List<ReactionRoute>,
    ): Boolean = a.size == b.size && a.groupingBy { it }.eachCount() == b.groupingBy { it }.eachCount()

    private fun ReactionRoute.sameSlot(other: ReactionRoute) = source == other.source && target == other.target

    /** The 1× amount of a route on a target no action claims. */
    private const val DEFAULT_AMOUNT = 0.3f

    /** The top of each moment's Strength slider. */
    const val MAX_STRENGTH = 2f

    private const val MIN_SCALABLE = 0.01f
}
