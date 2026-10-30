package dev.geode.ui

import dev.geode.data.MacroSetStore
import dev.geode.data.MapPrefs
import dev.geode.render.ReactionRoute
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test

class MacroControllerTest {
    /** Stands in for the session: params change only when the controller (or a test) writes them. */
    private class FakeHost : MacroController.Host {
        override var params: SceneParams = SceneParams.DEFAULT

        override val sceneId: String = SceneIds.PLASMA

        override var reactions: List<ReactionRoute>? = null

        override fun setReactions(routes: List<ReactionRoute>?) {
            reactions = routes
        }

        override fun editSceneParams(params: SceneParams) {
            this.params = params
        }

        override fun commitSceneParams(params: SceneParams) {
            this.params = params
        }
    }

    private val host = FakeHost()
    private val macros = MacroController(MacroSetStore(MapPrefs()), host)

    private fun knobs(): List<Float> {
        val set = macros.set.value
        return set.macros.map { it.value }
    }

    @Test
    fun undoAndRedoPutEachKnobBackWhereItWasForThatLook() {
        val start = host.params
        val rested = knobs()
        macros.turn(0, 0.9f)
        val afterEnergy = host.params
        macros.turn(1, 0.2f)
        val afterSpeed = host.params
        assertNotEquals(start, afterEnergy)
        assertNotEquals(afterEnergy, afterSpeed)

        macros.followLook { host.params = afterEnergy }
        assertEquals(0.9f, knobs()[0])
        assertEquals(rested[1], knobs()[1])

        macros.followLook { host.params = start }
        assertEquals(rested, knobs())

        macros.followLook { host.params = afterSpeed }
        assertEquals(0.9f, knobs()[0])
        assertEquals(0.2f, knobs()[1])
    }

    @Test
    fun resetAllRestsTheKnobsAndItsUndoBringsThemBack() {
        val rested = knobs()
        macros.turn(0, 0.7f)
        val turned = host.params
        val turnedKnobs = knobs()

        macros.followLook(rest = true) { host.params = SceneParams.DEFAULT }
        assertEquals(rested, knobs())

        macros.followLook { host.params = turned }
        assertEquals(turnedKnobs, knobs())
    }
}
