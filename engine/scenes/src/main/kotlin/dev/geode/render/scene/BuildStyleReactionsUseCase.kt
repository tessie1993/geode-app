package dev.geode.render.scene

import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.ReactionRoute
import dev.geode.render.bridge.ModConfigCodec

/**
 * A style's reaction DNA: the default routing of the reaction matrix for [sceneId].
 *
 * The DNA itself is built natively (core/viz/ReactionDna), next to the reaction matrix it feeds:
 * each style draws a personality (what its picture does on the beat: a hue jump, a bloom, a new
 * camera angle, a warp, a push...) and a second one from another group for alternate phrases,
 * plus layers for the backbeat, the sustained bands, the top end, colour over the song, build-ups
 * and drops. This class only asks for it and decodes the floats, so the Reactivity tab can show
 * the routes (the renderers resolve their own natively, core/viz/ReactionResolve).
 *
 * [playing] is what the matrix plays once the user has routes of their own: "the hand wins per
 * moment" (Customize › Music), so their routes play only for the sources their hand mask owns and the
 * live DNA for every other, re-dealt with the style and seed. The merge is native (mergeHand in
 * core/viz/ReactionDna); this only marshals the routes and the mask across.
 *
 * Deterministic: the live renderer, the wallpaper, the dream and export all get the same routes
 * from the same id and seed, so an export reacts as the screen did. [native] and [nativeHand] are
 * seams for JVM tests, which have no libgeode.
 */
class BuildStyleReactionsUseCase(
    private val native: (String, Int) -> FloatArray = GeodeNative::styleReactions,
    private val nativeHand: (String, Int, FloatArray?, Int) -> FloatArray = GeodeNative::styleReactionsHand,
) {
    operator fun invoke(
        sceneId: String,
        seed: Int,
    ): List<ReactionRoute> = ModConfigCodec.unpackReactions(native(sceneId, seed)).take(ReactionRoute.MAX_ROUTES)

    /** What plays for [user]'s routes (null: none of their own) under the hand [mask] (`GeodeNative.handSet`). */
    fun playing(
        sceneId: String,
        seed: Int,
        user: List<ReactionRoute>?,
        mask: Int,
    ): List<ReactionRoute> =
        ModConfigCodec.unpackReactions(nativeHand(sceneId, seed, user?.let { packExact(it) }, mask)).take(ReactionRoute.MAX_ROUTES)

    companion object {
        /** [routes] in the wire layout, without the padding slots [ModConfigCodec.packReactions] adds. */
        fun packExact(routes: List<ReactionRoute>): FloatArray =
            ModConfigCodec
                .packReactions(routes)
                .copyOf(routes.size.coerceAtMost(ReactionRoute.MAX_ROUTES) * ModConfigCodec.REACT_ROUTE_FLOATS)
    }
}
