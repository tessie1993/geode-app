package dev.geode.render.scene

import dev.geode.render.LfoTarget
import dev.geode.render.ModCurve
import dev.geode.render.ReactMode
import dev.geode.render.ReactSource
import dev.geode.render.ReactionRoute
import kotlin.random.Random

/**
 * A style's reaction DNA: the default routing of the reaction matrix for [sceneId].
 *
 * Every style gets the same *kinds* of reaction (a camera that answers kicks, drops and
 * section changes, a sound event that changes colour, one that reshapes, one that spins, a band
 * that makes it grow, a texture that follows the top end), but which sound drives which, and
 * by how much, is drawn from a generator seeded by the style id and [seed]. Two styles hear the
 * same bar differently, and re-rolling the seed gives one style a new personality without
 * losing what makes it musical.
 *
 * Pure and deterministic: the live renderer, the wallpaper, the dream and export all call this
 * with the same id and seed and get the same routes, so an export reacts as the screen did.
 * Routes whose target does not apply to the style (per [dev.geode.render.LfoTarget.scope]) are
 * never produced, so the DNA never spends a slot on a dead control.
 */
class BuildStyleReactionsUseCase {
    operator fun invoke(
        sceneId: String,
        seed: Int,
    ): List<ReactionRoute> {
        val rng = Random(sceneId.hashCode() * GOLDEN + seed)
        val routes = mutableListOf<ReactionRoute>()
        fun add(route: ReactionRoute) {
            if (route.target.scope.appliesTo(sceneId) && routes.none { it.target == route.target && it.source == route.source }) {
                routes += route
            }
        }

        // Camera: the beat dolly and the drop surge always; the rest drawn.
        add(route(rng.pick(KICKS), LfoTarget.CAM_PUSH, ReactMode.FOLLOW, rng.range(0.25f, 0.5f), 0.02f, 0.28f))
        add(route(ReactSource.DROP, LfoTarget.CAM_FLY, ReactMode.FOLLOW, rng.range(0.8f, 1.4f), 0.25f, 2.2f))
        add(route(ReactSource.DROP, LfoTarget.CAM_FOV, ReactMode.FOLLOW, rng.range(0.2f, 0.4f), 0.15f, 1.8f))
        add(route(ReactSource.SECTION, LfoTarget.CAM_YAW, ReactMode.STEP, rng.sign() * rng.range(0.3f, 0.7f), 0.8f, 0.8f))
        rng.maybe {
            add(route(ReactSource.BUILDUP, LfoTarget.CAM_FOV, ReactMode.FOLLOW, -rng.range(0.15f, 0.3f), 0.5f, 0.5f))
        }
        rng.maybe {
            add(route(ReactSource.STEREO_PAN, LfoTarget.CAM_YAW, ReactMode.FOLLOW, rng.range(0.2f, 0.45f), 0.3f, 0.6f))
        }
        rng.maybe {
            add(route(ReactSource.DOWNBEAT, LfoTarget.CAM_ROLL, ReactMode.STEP, rng.sign() * rng.range(0.15f, 0.35f), 0.4f, 0.4f))
        }

        // A sound event changes the colour.
        add(route(rng.pick(COLOUR_EVENTS), LfoTarget.COLOR_SHIFT, ReactMode.STEP, rng.range(0.08f, 0.3f), 0.25f, 0.25f))
        // One reshapes it: morph, twist, warp or ripple, from a hit.
        add(route(rng.pick(SHAPE_SOURCES), rng.pick(SHAPE_TARGETS), ReactMode.FOLLOW, rng.range(0.2f, 0.5f), 0.03f, 0.35f, ModCurve.SMOOTH))
        // One spins it while the sound lasts.
        add(route(rng.pick(SPIN_SOURCES), LfoTarget.ROTATION, ReactMode.FOLLOW, rng.sign() * rng.range(0.2f, 0.5f), 0.1f, 0.6f))
        // A band makes it grow.
        add(route(rng.pick(GROW_SOURCES), LfoTarget.ZOOM, ReactMode.FOLLOW, rng.range(0.08f, 0.22f), 0.06f, 0.4f, ModCurve.SMOOTH))
        // The top end gives it texture.
        add(route(rng.pick(TEXTURE_SOURCES), rng.pick(TEXTURE_TARGETS), ReactMode.FOLLOW, rng.range(0.15f, 0.4f), 0.05f, 0.3f))

        return routes.take(ReactionRoute.MAX_ROUTES)
    }

    private fun route(
        source: ReactSource,
        target: LfoTarget,
        mode: ReactMode,
        amount: Float,
        attack: Float,
        release: Float,
        curve: ModCurve = ModCurve.LINEAR,
    ) = ReactionRoute(true, source, target, mode, amount, curve, attack, release)

    private fun <T> Random.pick(options: List<T>): T = options[nextInt(options.size)]

    private fun Random.range(
        from: Float,
        to: Float,
    ): Float = from + (to - from) * nextFloat()

    private fun Random.sign(): Float = if (nextBoolean()) 1f else -1f

    private inline fun Random.maybe(block: () -> Unit) {
        if (nextBoolean()) block()
    }

    private companion object {
        const val GOLDEN = -0x61c88647 // 0x9E3779B9: spreads consecutive seeds apart

        val KICKS = listOf(ReactSource.KICK, ReactSource.KICK, ReactSource.SUB_BASS, ReactSource.BEAT)
        val COLOUR_EVENTS = listOf(ReactSource.PITCH_CHANGE, ReactSource.SECTION, ReactSource.DOWNBEAT, ReactSource.ONSET, ReactSource.TIMBRE_JUMP)
        val SHAPE_SOURCES = listOf(ReactSource.SNARE, ReactSource.ONSET, ReactSource.HAT, ReactSource.FLUX, ReactSource.KICK)
        val SHAPE_TARGETS = listOf(LfoTarget.MORPH, LfoTarget.TWIST, LfoTarget.WARP, LfoTarget.RIPPLE)
        val SPIN_SOURCES = listOf(ReactSource.MID, ReactSource.HIGH_MID, ReactSource.SNARE, ReactSource.BAR_OSC, ReactSource.LOW_MID)
        val GROW_SOURCES = listOf(ReactSource.SUB_BASS, ReactSource.BASS, ReactSource.LOW_MID, ReactSource.SWELL, ReactSource.LOUDNESS)
        val TEXTURE_SOURCES = listOf(ReactSource.AIR, ReactSource.PRESENCE, ReactSource.HAT, ReactSource.NOISINESS, ReactSource.BRIGHTNESS)
        val TEXTURE_TARGETS = listOf(LfoTarget.TURBULENCE, LfoTarget.CHROMA_AB, LfoTarget.PALETTE_MIX, LfoTarget.SATURATION, LfoTarget.WARP)
    }
}
