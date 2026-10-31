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
 * the routes and the renderers can send them back.
 *
 * Deterministic: the live renderer, the wallpaper, the dream and export all get the same routes
 * from the same id and seed, so an export reacts as the screen did. [native] is a seam for JVM
 * tests, which have no libgeode.
 */
class BuildStyleReactionsUseCase(
    private val native: (String, Int) -> FloatArray = GeodeNative::styleReactions,
) {
    operator fun invoke(
        sceneId: String,
        seed: Int,
    ): List<ReactionRoute> = ModConfigCodec.unpackReactions(native(sceneId, seed)).take(ReactionRoute.MAX_ROUTES)
}
