package dev.geode.render.scene

import dev.geode.render.LfoTarget
import dev.geode.render.ReactSource
import dev.geode.render.ReactionRoute
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class BuildStyleReactionsUseCaseTest {
    private val build = BuildStyleReactionsUseCase()
    private val styles = SceneCapabilities.SHADER_SCENES.keys + listOf(SceneIds.FLUID, SceneIds.WATER, SceneIds.DEFAULT)

    @Test
    fun sameStyleAndSeedGiveTheSameRoutes() {
        // Live view and export each build the DNA themselves; they must agree.
        styles.forEach { id -> assertEquals(id, build(id, 3), build(id, 3)) }
    }

    @Test
    fun stylesDoNotAllReactAlike() {
        val distinct = styles.map { build(it, 0) }.toSet()
        assertTrue("only ${distinct.size} distinct DNAs over ${styles.size} styles", distinct.size > styles.size / 2)
    }

    @Test
    fun rerollingTheSeedChangesThePersonality() {
        assertNotEquals(build(SceneIds.KIFS, 0), build(SceneIds.KIFS, 1))
    }

    @Test
    fun everyRouteMovesSomethingTheStyleCanShow() {
        styles.forEach { id ->
            val routes = build(id, 0)
            assertTrue(id, routes.size <= ReactionRoute.MAX_ROUTES)
            routes.forEach { r ->
                assertTrue("$id: ${r.target}", r.target != LfoTarget.NONE && r.target.scope.appliesTo(id))
                assertTrue("$id: ${r.target} is a chain target", r.target.chain == null)
            }
        }
    }

    @Test
    fun everyStyleGetsTheBeatDollyAndTheDropSurge() {
        styles.forEach { id ->
            val routes = build(id, 0)
            assertTrue(id, routes.any { it.target == LfoTarget.CAM_PUSH })
            assertTrue(id, routes.any { it.source == ReactSource.DROP && it.target == LfoTarget.CAM_FLY })
            assertTrue(id, routes.any { it.target == LfoTarget.COLOR_SHIFT && it.source.isEvent })
        }
    }

    @Test
    fun onlyTurningStylesSpinOrRollByDefault() {
        val turning = setOf(LfoTarget.ROTATION, LfoTarget.CAM_ROLL)
        styles.forEach { id ->
            (0..4).forEach { seed ->
                val spins = build(id, seed).any { it.target in turning }
                if (id !in SceneCapabilities.SPINNING_SCENES) assertTrue("$id seed $seed spins", !spins)
            }
        }
        assertTrue(build(SceneIds.KALEIDO, 0).any { it.target == LfoTarget.ROTATION })
    }

    @Test
    fun someStylesRideTheSongEnergy() {
        assertTrue(styles.any { id -> build(id, 0).any { it.source == ReactSource.MACRO_ENERGY } })
    }

    @Test
    fun amountsAndTimesStayInsideWhatNativeAccepts() {
        styles.forEach { id ->
            build(id, 7).forEach { r ->
                assertTrue(r.amount in ReactionRoute.MIN_AMOUNT..ReactionRoute.MAX_AMOUNT)
                assertTrue(r.attackSeconds in 0f..ReactionRoute.MAX_ATTACK_SECONDS)
                assertTrue(r.releaseSeconds in 0f..ReactionRoute.MAX_RELEASE_SECONDS)
            }
        }
    }
}
