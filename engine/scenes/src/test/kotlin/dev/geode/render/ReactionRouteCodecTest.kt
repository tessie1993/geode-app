package dev.geode.render

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ReactionRouteCodecTest {
    private val dna =
        listOf(
            ReactionRoute(true, ReactSource.KICK, LfoTarget.COLOR_SHIFT, ReactMode.JUMP, 0.35f, ModCurve.LINEAR, 0.08f, 0.35f),
            ReactionRoute(true, ReactSource.SNARE_2, LfoTarget.WARP, ReactMode.FOLLOW, 0.6f, ModCurve.SMOOTH, 0.03f, 0.45f),
            ReactionRoute(true, ReactSource.HARD_CUT, LfoTarget.CAM_FLY, ReactMode.FOLLOW, 1.1f, ModCurve.LINEAR, 0.25f, 2.2f),
            ReactionRoute(false, ReactSource.KICK_4, LfoTarget.ROTATION, ReactMode.STEP, -0.2f, ModCurve.LINEAR, 0.12f, 0.12f),
        )

    @Test
    fun roundTripsTheRoutes() {
        assertEquals(dna, ReactionRouteCodec.decode(ReactionRouteCodec.encode(dna)))
    }

    @Test
    fun anEmptyListIsKeptRatherThanReadAsNothingSaved() {
        // The user removed every route: that is a choice, not "play the DNA".
        assertEquals(emptyList<ReactionRoute>(), ReactionRouteCodec.decode(ReactionRouteCodec.encode(emptyList())))
    }

    @Test
    fun nothingSavedOrAnUnknownVersionFallsBackToTheDna() {
        assertNull(ReactionRouteCodec.decode(null))
        assertNull(ReactionRouteCodec.decode(""))
        assertNull(ReactionRouteCodec.decode("""[{"source":"KICK"}]"""))
        assertNull(ReactionRouteCodec.decode("reactions/99\ntrue|KICK|ZOOM|FOLLOW|0.3|LINEAR|0.05|0.3"))
    }

    @Test
    fun routesSavedForTheOldAnalyserResetToTheDna() {
        // KICK meant a level then and is a hit now: reading it back would change what the route does.
        assertNull(ReactionRouteCodec.decode("reactions/1\ntrue|KICK|ZOOM|FOLLOW|0.3|LINEAR|0.05|0.3"))
    }

    @Test
    fun corruptedLinesDropOnlyThemselves() {
        val text =
            """
            reactions/2
            true|KICK|ZOOM|FOLLOW|0.3|LINEAR|0.05|0.3
            this line is garbage
            true|SNARE|TWIST
            ||||||||
            true|DROP|CAM_FLY|STEP|1.0|SMOOTH|0.2|2.0
            """.trimIndent()
        val routes = ReactionRouteCodec.decode(text)!!
        assertEquals(listOf(LfoTarget.ZOOM, LfoTarget.CAM_FLY), routes.map { it.target })
    }

    @Test
    fun unknownSourcesAndTargetsDropTheRouteUnknownModesAndCurvesTakeDefaults() {
        val text =
            """
            reactions/2
            true|THEREMIN|ZOOM|FOLLOW|0.3|LINEAR|0.05|0.3
            true|KICK|WARP_DRIVE|FOLLOW|0.3|LINEAR|0.05|0.3
            true|KICK|NONE|FOLLOW|0.3|LINEAR|0.05|0.3
            maybe|HAT|TWIST|WOBBLE|0.4|SQUIGGLE|0.05|0.3
            """.trimIndent()
        val routes = ReactionRouteCodec.decode(text)!!
        assertEquals(1, routes.size)
        val r = routes.single()
        assertEquals(ReactSource.HAT, r.source)
        assertEquals(LfoTarget.TWIST, r.target)
        assertEquals(ReactionRoute().mode, r.mode)
        assertEquals(ReactionRoute().curve, r.curve)
        assertEquals(ReactionRoute().enabled, r.enabled)
    }

    @Test
    fun badNumbersTakeDefaultsAndRangesAreClamped() {
        val text =
            """
            reactions/2
            true|KICK|ZOOM|FOLLOW|NaN|LINEAR|-4|1e9
            true|KICK|TWIST|FOLLOW|99|LINEAR|abc|Infinity
            """.trimIndent()
        val (a, b) = ReactionRouteCodec.decode(text)!!
        assertEquals(ReactionRoute().amount, a.amount)
        assertEquals(0f, a.attackSeconds)
        assertEquals(ReactionRoute.MAX_RELEASE_SECONDS, a.releaseSeconds)
        assertEquals(ReactionRoute.MAX_AMOUNT, b.amount)
        assertEquals(ReactionRoute().attackSeconds, b.attackSeconds)
        assertEquals(ReactionRoute().releaseSeconds, b.releaseSeconds)
    }

    @Test
    fun neverReturnsMoreRoutesThanNativeHasSlots() {
        val many = List(40) { ReactionRoute(source = ReactSource.KICK, target = LfoTarget.ZOOM) }
        val line = "true|KICK|ZOOM|FOLLOW|0.3|LINEAR|0.05|0.3"
        assertTrue(ReactionRouteCodec.encode(many).lines().size <= ReactionRoute.MAX_ROUTES + 1)
        assertEquals(ReactionRoute.MAX_ROUTES, ReactionRouteCodec.decode("reactions/2\n" + List(40) { line }.joinToString("\n"))!!.size)
    }
}
