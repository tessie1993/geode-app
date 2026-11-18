package dev.geode.data

import dev.geode.model.SceneParams
import dev.geode.model.StyleCatalogue
import dev.geode.model.TestCatalogue
import dev.geode.pro.Entitlement
import dev.geode.pro.FeatureGate
import dev.geode.pro.FreeTier
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * PRO-1: applying a template used to select its style with no Pro check. A template is now applied
 * as the preset [TemplateLook.toPreset] makes, through PlayerSession.applyPreset, the path the
 * style gate guards. These pin what that gate is handed: the template's own style.
 */
class TemplateStyleGateTest {
    init {
        TestCatalogue.install()
    }

    private fun lookOn(sceneId: String) = TemplateLook(sceneId = sceneId, attack = 0.5f, decay = 0.1f, params = SceneParams.DEFAULT)

    @Test
    fun `a template's preset carries the template's style to the gate`() {
        val look = lookOn(StyleCatalogue.DEFAULT_ID)
        assertEquals(StyleCatalogue.DEFAULT_ID, look.toPreset("Night drive").sceneId)
    }

    @Test
    fun `a template on a Pro style is refused for free users and allowed for Pro`() {
        val pro = lookOn(StyleCatalogue.DEFAULT_ID).toPreset("Pro look")
        assertFalse("silk is a Pro renderer", FeatureGate.FREE.allowsStyle(pro.sceneId))
        assertTrue(FeatureGate(Entitlement.Pro).allowsStyle(pro.sceneId))
    }

    @Test
    fun `a template on a free style still applies for free users`() {
        val free = lookOn(FreeTier.FALLBACK_STYLE_ID).toPreset("Free look")
        assertTrue(FeatureGate.FREE.allowsStyle(free.sceneId))
    }
}
