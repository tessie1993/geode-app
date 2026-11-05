package dev.geode.render.look

import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.AdsrConfig
import dev.geode.render.LfoConfig
import dev.geode.render.ReactionRoute
import dev.geode.render.scene.SceneParams
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

/** The look snapshot (#345) through its seam, against a renderer that only records what it was told. */
class LookApplierTest {
    private class FakeTarget : LookTarget {
        val calls = mutableListOf<String>()
        var knows = true
        var params: SceneParams? = null
        var reducedMotion: Boolean? = null
        var transition: Pair<String, Long>? = null

        override fun setScene(sceneId: String): Boolean {
            calls += "scene $sceneId"
            return knows
        }

        override fun setParams(p: SceneParams) {
            calls += "params"
            params = p
        }

        override fun setLfoConfigs(configs: List<LfoConfig>) {
            calls += "lfos"
        }

        override fun setAdsrConfigs(configs: List<AdsrConfig>) {
            calls += "adsrs"
        }

        override fun setUserReactions(routes: List<ReactionRoute>?) {
            calls += "reactions"
        }

        override fun setSoundTuning(floats: FloatArray) {
            calls += "tuning"
        }

        override fun setReducedMotion(on: Boolean) {
            calls += "reducedMotion"
            reducedMotion = on
        }

        override fun setTransition(
            id: String,
            durationMs: Long,
        ) {
            calls += "transition"
            transition = id to durationMs
        }

        override fun setUnderlay(
            pixels: IntArray?,
            width: Int,
            height: Int,
            blend: Int,
            amount: Float,
        ) {
            calls += "underlay"
        }
    }

    /** A source whose stores the test edits, as the app would while a render runs. */
    private class EditableSource(
        var look: LookSnapshot,
    ) : LookSource {
        override fun current(): LookSnapshot = look
    }

    @Test
    fun theFirstApplySendsTheWholeLook() {
        val target = FakeTarget()
        LookApplier().apply(LookSnapshot(), target)
        assertEquals(
            listOf("scene neon_chrome_sea", "params", "lfos", "adsrs", "reactions", "tuning", "reducedMotion", "transition", "underlay"),
            target.calls,
        )
    }

    @Test
    fun applyingTheSameSnapshotTwicePushesNothingTheSecondTime() {
        val target = FakeTarget()
        val applier = LookApplier()
        val look = LookSnapshot()
        applier.apply(look, target)
        target.calls.clear()
        applier.apply(look, target)
        // A new snapshot holding the same parts is no change either: the live stage rebuilds it on every edit.
        applier.apply(look.copy(), target)
        assertTrue(target.calls.isEmpty())
    }

    @Test
    fun aParamEditPushesTheParamsAlone() {
        val target = FakeTarget()
        val applier = LookApplier()
        val look = LookSnapshot()
        applier.apply(look, target)
        target.calls.clear()
        applier.apply(look.copy(params = look.params.copy(reactSeed = 7)), target)
        assertEquals(listOf("params"), target.calls)
        assertEquals(7, target.params?.reactSeed)
    }

    @Test
    fun aResetTargetGetsTheWholeLookAgain() {
        val target = FakeTarget()
        val applier = LookApplier()
        val look = LookSnapshot()
        applier.apply(look, target)
        target.calls.clear()
        applier.reset()
        applier.apply(look, target)
        assertEquals(9, target.calls.size)
    }

    @Test
    fun aSceneTheRendererCannotDrawYetIsAskedAgain() {
        val target = FakeTarget().apply { knows = false }
        val applier = LookApplier()
        val look = LookSnapshot()
        applier.apply(look, target)
        target.calls.clear()
        target.knows = true
        applier.apply(look, target)
        assertEquals(listOf("scene neon_chrome_sea"), target.calls)
        target.calls.clear()
        applier.apply(look, target)
        assertTrue(target.calls.isEmpty())
    }

    @Test
    fun aPinnedSnapshotIgnoresLaterStoreChanges() {
        val before = SceneParams.DEFAULT.copy(reactSeed = 1)
        val source = EditableSource(LookSnapshot(params = before))
        val pinned = source.pin()
        source.look = source.look.copy(params = before.copy(reactSeed = 2), reducedMotion = true)

        val target = FakeTarget()
        LookApplier().apply(pinned, target)
        assertSame(before, target.params)
        assertEquals(false, target.reducedMotion)
        assertEquals(2, source.current().params.reactSeed)
    }

    @Test
    fun theDreamOverrideSetsTheGentleFlight() {
        val target = FakeTarget()
        LookApplier().apply(LookSnapshot(reducedMotion = false).gentleFlight(), target)
        assertEquals(true, target.reducedMotion)
    }

    @Test
    fun nativeDueBitsDecideWhenParamsAndConfigsCross() {
        val target = FakeTarget()
        val applier = LookApplier()
        val look = LookSnapshot()
        applier.apply(look, target, GeodeNative.FRAME_PUSH_PARAMS or GeodeNative.FRAME_PUSH_TUNING)
        assertTrue("params" in target.calls && "tuning" in target.calls)
        assertTrue("lfos" !in target.calls && "adsrs" !in target.calls)
        target.calls.clear()
        applier.apply(look, target, GeodeNative.FRAME_PUSH_LFO)
        assertEquals(listOf("lfos"), target.calls.filter { it != "scene ${look.sceneId}" })
    }
}
