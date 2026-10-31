package dev.geode.render

import dev.geode.render.scene.SceneIds
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class EventRecipeTest {
    private val shader = SceneIds.PLASMA

    @Test
    fun everyRecipeFitsTheMatrixAndListensToSomething() {
        EventRecipe.entries.forEach { r ->
            assertTrue(r.name, r.routes.isNotEmpty())
            assertTrue(r.name, r.routes.size <= ReactionRoute.MAX_ROUTES)
            r.routes.forEach { route ->
                assertTrue(r.name, route.target != LfoTarget.NONE)
                assertTrue(r.name, route.amount in ReactionRoute.MIN_AMOUNT..ReactionRoute.MAX_AMOUNT)
            }
        }
    }

    @Test
    fun addingARecipeThenTakingItOffLeavesTheListAsItWas() {
        val start = listOf(ReactionRoute(source = ReactSource.BASS, target = LfoTarget.ZOOM))
        val on = EventRecipe.KICK_PUNCH.toggle(start, shader)
        assertTrue(EventRecipe.KICK_PUNCH.isOn(on, shader))
        val off = EventRecipe.KICK_PUNCH.toggle(on, shader)
        assertFalse(EventRecipe.KICK_PUNCH.isOn(off, shader))
        assertEquals(start, off)
    }

    @Test
    fun addingReplacesTheSameSoundOnTheSameTargetInsteadOfStacking() {
        val start = listOf(ReactionRoute(source = ReactSource.KICK_ENV, target = LfoTarget.CAM_PUSH, amount = 0.9f))
        val on = EventRecipe.KICK_PUNCH.toggle(start, shader)
        assertEquals(1, on.count { it.source == ReactSource.KICK_ENV && it.target == LfoTarget.CAM_PUSH })
    }

    @Test
    fun aRecipeOnlyAddsWhatTheStyleCanShow() {
        // Fluid splat force has nothing to push on a shader style.
        val on = EventRecipe.SUB_WEIGHT.toggle(emptyList(), shader)
        assertTrue(on.none { it.target == LfoTarget.FLUID_FORCE })
        assertTrue(on.isNotEmpty())
    }

    @Test
    fun aFullMatrixRefusesARecipeRatherThanDroppingRoutes() {
        val full = List(ReactionRoute.MAX_ROUTES) { ReactionRoute(source = ReactSource.HIGH, target = LfoTarget.WARP) }
        assertFalse(EventRecipe.DROP_BURST.fits(full, shader))
        assertEquals(full, EventRecipe.DROP_BURST.toggle(full, shader))
    }

    @Test
    fun everyRecipeDoesSomethingOnAShaderStyle() {
        EventRecipe.entries.forEach { r -> assertTrue(r.name, r.routesFor(shader).isNotEmpty()) }
    }

    @Test
    fun aRouteReadsAsASentence() {
        val route = ReactionRoute(source = ReactSource.KICK, target = LfoTarget.CAM_PUSH)
        assertEquals("When Kick → Camera push", EventRecipe.sentence(route))
    }
}
