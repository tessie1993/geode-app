package dev.geode.render.scene

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

class MacrosTest {
    private val shader = SceneIds.PLASMA
    private val eps = 0.002f

    @Test
    fun theDefaultLookSitsEveryMacroAtItsNeutral() {
        Macro.entries.forEach { macro ->
            assertEquals(macro.name, macro.neutral, Macros.position(SceneParams.DEFAULT, macro, shader), eps)
        }
    }

    @Test
    fun settingAMacroReadsBackExactlyWhereItWasSet() {
        // The knob is read from the parameters, so a set that did not read back would make it jump
        // under the finger.
        Macro.entries.forEach { macro ->
            for (to in listOf(0f, 0.2f, 0.5f, 0.8f, 1f)) {
                val out = Macros.set(SceneParams.DEFAULT, macro, to, shader)
                assertEquals("${macro.name} -> $to", to, Macros.position(out, macro, shader), eps)
            }
        }
    }

    @Test
    fun aDragThatReturnsToItsStartRestoresTheLookExactly() {
        val start = SceneParams.DEFAULT.copy(warp = 0.31f, twist = 0.2f, tile = 1.7f)
        val from = Macros.position(start, Macro.COMPLEXITY, shader)
        Macros.set(start, Macro.COMPLEXITY, 1f, shader)
        // A drag is always applied to the params the gesture started from, never chained.
        val back = Macros.set(start, Macro.COMPLEXITY, from, shader)
        assertEquals(start, back)
    }

    @Test
    fun aMacroKeepsTheLooksOwnBalanceBetweenItsTargets() {
        // Warp starts high, ripple at zero: turning Complexity up raises both but keeps warp ahead.
        val start = SceneParams.DEFAULT.copy(warp = 0.5f, ripple = 0f)
        val out = Macros.set(start, Macro.COMPLEXITY, 0.75f, shader)
        assertTrue(out.warp > start.warp)
        assertTrue(out.ripple > start.ripple)
        assertTrue(out.warp > out.ripple)
    }

    @Test
    fun theEndsOfAMacroReachTheEndsOfEveryTarget() {
        Macro.entries.forEach { m ->
            val top = Macros.set(SceneParams.DEFAULT, m, 1f, shader)
            Macros.targets(m, shader).forEach { t -> assertEquals(t.key, t.high, t.get(top), eps) }
            if (m.neutral > 0f) {
                val bottom = Macros.set(SceneParams.DEFAULT, m, 0f, shader)
                Macros.targets(m, shader).forEach { t -> assertEquals(t.key, t.low, t.get(bottom), eps) }
            }
        }
    }

    @Test
    fun aFollowerMovesWithTheKnobWithoutPullingTheReading() {
        // Vignette has nowhere to go on the bright side, so it must not hold Mood at the middle.
        val dark = Macros.set(SceneParams.DEFAULT, Macro.MOOD, 0.1f, shader)
        assertTrue(dark.vignette > 0f)
        val bright = Macros.set(SceneParams.DEFAULT, Macro.MOOD, 0.9f, shader)
        assertEquals(0f, bright.vignette)
        assertEquals(0.9f, Macros.position(bright, Macro.MOOD, shader), eps)
    }

    @Test
    fun aDragBackDownUndoesTheWayUp() {
        val up = Macros.set(SceneParams.DEFAULT, Macro.ENERGY, 0.9f, shader)
        val back = Macros.set(up, Macro.ENERGY, 0.5f, shader)
        Macros.targets(Macro.ENERGY, shader).forEach { t ->
            assertEquals(t.key, t.get(SceneParams.DEFAULT), t.get(back), eps)
        }
    }

    @Test
    fun aMacroOnlyWritesWhatTheStyleOnScreenCanShow() {
        // Morph lives in the shader fragment programs; on a fluid style the macro must leave it be.
        val fluid = SceneIds.FLUID
        assertTrue(Macros.targets(Macro.COMPLEXITY, fluid).none { it.key == ParamKeys.MORPH })
        val out = Macros.set(SceneParams.DEFAULT, Macro.COMPLEXITY, 1f, fluid)
        assertEquals(SceneParams.DEFAULT.morph, out.morph)
    }

    @Test
    fun everyMacroMovesSomethingOnEveryStyle() {
        ALL_STYLES.forEach { id ->
            Macro.entries.forEach { macro ->
                val out = Macros.set(SceneParams.DEFAULT, macro, 1f, id)
                assertNotEquals("${macro.name} on $id", SceneParams.DEFAULT, out)
            }
        }
    }

    @Test
    fun noParameterBelongsToTwoMacros() {
        // Two macros sharing a parameter would drag each other's knob around.
        val owners = Macro.entries.flatMap { m -> Macros.allTargets(m).map { it.key to m } }
        val shared = owners.groupBy({ it.first }, { it.second }).filterValues { it.size > 1 }
        assertEquals(emptyMap<String, List<Macro>>(), shared)
    }

    @Test
    fun everyTargetIsARealControlAndStaysInsideItsSliderRange() {
        Macro.entries.forEach { m ->
            Macros.allTargets(m).forEach { t ->
                assertTrue("${t.key} is not a ParamKeys name", t.key in PARAM_KEY_NAMES)
                assertEquals("${t.key} neutral must be the default", t.get(SceneParams.DEFAULT), t.mid, 1e-6f)
                assertTrue("${t.key} has no travel", t.low != t.mid || t.mid != t.high)
            }
        }
    }

    @Test
    fun aValueSetPastTheMacroRangeSurvivesAMoveThatCannotReachIt() {
        // A hand-set slider beyond the macro's top stays put when the macro is already at the top.
        val start = SceneParams.DEFAULT.copy(speed = 3.9f)
        val out = Macros.set(start, Macro.SPEED, 0.95f, shader)
        assertEquals(3.9f, out.speed)
        assertTrue(out.camFly > start.camFly)
    }

    @Test
    fun positionsStayInsideZeroToOneForAnyLook() {
        val wild = SceneParams.DEFAULT.copy(speed = 99f, warp = -5f, brightness = 0f, camFov = 9f)
        Macro.entries.forEach { m ->
            val p = Macros.position(wild, m, shader)
            assertTrue("${m.name} $p", p in 0f..1f)
        }
    }

    @Test
    fun surpriseMovesTheMacrosButNeverToTheirEnds() {
        val rng = Random(42)
        repeat(20) {
            val out = Macros.surprise(SceneParams.DEFAULT, shader, rng)
            assertNotEquals(SceneParams.DEFAULT, out)
            Macro.entries.forEach { m ->
                val pos = Macros.position(out, m, shader)
                assertTrue("${m.name} $pos", pos in 0.15f..0.85f || (m.neutral == 0f && pos <= 0.6f))
            }
        }
    }

    @Test
    fun resetPutsEveryMacroBackAndLeavesTheRestAlone() {
        val start = SceneParams.DEFAULT.copy(palette = 4, kaleidoscope = true)
        val moved = Macro.entries.fold(start) { p, m -> Macros.set(p, m, 0.9f, shader) }
        assertEquals(start, Macros.reset(moved))
    }

    private companion object {
        val ALL_STYLES: List<String> =
            SceneCapabilities.SHADER_SCENES.keys.toList() +
                VisualStyleCatalog.silkIds + VisualStyleCatalog.lifeIds + VisualStyleCatalog.acidIds +
                VisualStyleCatalog.mycoIds + VisualStyleCatalog.fluidIds + VisualStyleCatalog.cymaticsIds +
                listOf(SceneIds.MILKDROP, SceneIds.CURLFLOW, SceneIds.WATER)

        val PARAM_KEY_NAMES: Set<String> =
            ParamKeys::class.java.declaredFields
                .filter { it.type == String::class.java }
                .map {
                    it.isAccessible = true
                    it.get(null) as String
                }.toSet()
    }
}
