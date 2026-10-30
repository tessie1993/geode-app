package dev.geode.render.scene

import dev.geode.render.ReactSource
import dev.geode.render.ReactionRoute
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class MacroSetTest {
    private val shader = SceneIds.PLASMA
    private val eps = 1e-4f

    private fun macro(vararg dest: Pair<String, Float>) =
        UserMacro(name = "Test", destinations = dest.map { (k, a) -> MacroDestination(k, a) })

    @Test
    fun turningAKnobAddsItsAmountTimesTheTurnToEveryDestination() {
        // Serum-style: each destination moves by its own amount (a share of the slider's span).
        val m = macro(ParamKeys.SPEED to 0.5f, ParamKeys.BLOOM to -0.25f)
        val start = SceneParams.DEFAULT.copy(bloom = 0.2f)
        val out = MacroKnobs.turn(start, m, from = 0f, to = 0.4f, sceneId = shader)
        val speed = MacroParams.of(ParamKeys.SPEED)!!
        val bloom = MacroParams.of(ParamKeys.BLOOM)!!
        assertEquals(1f + 0.5f * speed.span * 0.4f, out.speed, eps)
        assertEquals(0.2f - 0.25f * bloom.span * 0.4f, out.bloom, eps)
    }

    @Test
    fun aTurnStaysInsideEachSliderRange() {
        val m = macro(ParamKeys.SPEED to 1f)
        val out = MacroKnobs.turn(SceneParams.DEFAULT, m, from = 0f, to = 1f, sceneId = shader)
        assertEquals(MacroParams.of(ParamKeys.SPEED)!!.range.endInclusive, out.speed, eps)
    }

    @Test
    fun aDragThatReturnsToItsStartRestoresTheLookExactly() {
        val m = macro(ParamKeys.SPEED to 1f, ParamKeys.DOMAIN_WARP to 0.6f)
        val start = SceneParams.DEFAULT.copy(warp = 0.3f)
        // A drag always applies to the params it started from, so overshooting the top and
        // coming back loses nothing to the clamp.
        MacroKnobs.turn(start, m, 0.2f, 1f, shader)
        assertEquals(start, MacroKnobs.turn(start, m, 0.2f, 0.2f, shader))
    }

    @Test
    fun aKnobOnlyMovesWhatTheStyleCanShow() {
        val m = macro(ParamKeys.MORPH to 1f, ParamKeys.SPEED to 0.2f)
        val out = MacroKnobs.turn(SceneParams.DEFAULT, m, 0f, 1f, SceneIds.FLUID)
        assertEquals(SceneParams.DEFAULT.morph, out.morph)
    }

    @Test
    fun captureTurnsTheCurrentLookIntoDestinationsTheKnobReachesAtTheTop() {
        val look = SceneParams.DEFAULT.copy(speed = 2f, bloom = 0.1f, vignette = 0.4f)
        val dests = MacroKnobs.capture(SceneParams.DEFAULT, look, shader, rest = 0f)
        assertEquals(setOf(ParamKeys.SPEED, ParamKeys.BLOOM, ParamKeys.VIGNETTE), dests.map { it.key }.toSet())
        val out = MacroKnobs.turn(SceneParams.DEFAULT, UserMacro("C", destinations = dests), 0f, 1f, shader)
        assertEquals(2f, out.speed, eps)
        assertEquals(0.1f, out.bloom, eps)
        assertEquals(0.4f, out.vignette, eps)
    }

    @Test
    fun aMacroSoundBecomesOneRoutePerDestinationThatCanReact() {
        val m =
            macro(ParamKeys.ZOOM to 0.5f, ParamKeys.AUDIO_DRIVE to 0.5f)
                .copy(sounds = listOf(MacroSound(ReactSource.KICK, amount = 0.8f)))
        val routes = MacroKnobs.soundRoutes(m, shader)
        // Zoom has a reaction target; Audio drive is not reachable by reactions, so it is skipped.
        assertEquals(1, routes.size)
        assertEquals(ReactSource.KICK, routes[0].source)
        assertTrue(routes[0].amount > 0f)
        assertTrue(routes.all { it.amount in ReactionRoute.MIN_AMOUNT..ReactionRoute.MAX_AMOUNT })
    }

    @Test
    fun theStandardSetIsSixMacrosAtRestThatMoveSomethingOnEveryStyle() {
        val set = StandardMacros.SET
        assertEquals(listOf("Energy", "Speed", "Complexity", "Mood", "Space", "Chaos"), set.macros.map { it.name })
        ALL_STYLES.forEach { id ->
            set.macros.forEach { m ->
                assertEquals(m.rest, m.value, eps)
                val out = MacroKnobs.turn(SceneParams.DEFAULT, m, m.rest, 1f, id)
                assertNotEquals("${m.name} on $id", SceneParams.DEFAULT, out)
            }
        }
    }

    @Test
    fun everyDestinationOfTheStandardSetIsInTheCatalogue() {
        val destinations = StandardMacros.SET.macros.flatMap { it.destinations }
        destinations.forEach { d ->
            assertTrue(d.key, MacroParams.of(d.key) != null)
        }
    }

    @Test
    fun theCatalogueOnlyHoldsRealControlsWithTheirDefaultsInsideTheirRange() {
        MacroParams.ALL.forEach { p ->
            assertTrue(p.key, p.key in PARAM_KEY_NAMES)
            assertTrue(p.key, p.get(SceneParams.DEFAULT) in p.range)
        }
        val keys = MacroParams.ALL.map { it.key }
        assertEquals(keys.size, keys.toSet().size)
    }

    @Test
    fun theCodecRoundTripsASet() {
        val set =
            MacroSet(
                name = "Club night",
                macros =
                    listOf(
                        UserMacro(
                            name = "Big drop",
                            icon = "bolt",
                            bipolar = false,
                            value = 0.3f,
                            destinations = listOf(MacroDestination(ParamKeys.BLOOM, 0.7f), MacroDestination(ParamKeys.CAM_FOV, -0.2f)),
                            sounds = listOf(MacroSound(ReactSource.DROP, 0.9f, 0.05f, 1.5f)),
                        ),
                        StandardMacros.SET.macros[0],
                    ),
            )
        assertEquals(set, MacroSetCodec.decode(MacroSetCodec.encode(set)))
    }

    @Test
    fun theCodecSkipsWhatItDoesNotKnowAndRejectsWhatIsNotASet() {
        val text =
            """
            mvzmacro 1
            set Odd one
            macro 1 0.5 tune Fine
            dest 0.3 Speed
            dest 0.3 No such control
            sound NO_SUCH_SOUND 0.5 0.1 0.2
            sound KICK 9 0.1 0.2
            future-line whatever
            """.trimIndent()
        val set = MacroSetCodec.decode(text)!!
        assertEquals("Odd one", set.name)
        val macro = set.macros.single()
        assertEquals(listOf(ParamKeys.SPEED), macro.destinations.map { it.key })
        assertEquals(1f, macro.sounds.single().amount, eps)
        assertNull(MacroSetCodec.decode("{\"name\":\"a preset\"}"))
        assertNull(MacroSetCodec.decode("mvzmacro 99\nset Too new"))
    }

    @Test
    fun namesWithLineBreaksCannotBreakTheFormat() {
        val set = MacroSet("Two\nlines", listOf(UserMacro("A\nB")))
        val back = MacroSetCodec.decode(MacroSetCodec.encode(set))!!
        assertEquals("Two lines", back.name)
        assertEquals("A B", back.macros.single().name)
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
