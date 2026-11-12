package dev.geode.data

import dev.geode.pro.Entitlement
import dev.geode.pro.FeatureGate
import dev.geode.pro.FreeTier
import dev.geode.render.scene.StyleCatalogue
import dev.geode.render.scene.TestCatalogue
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

    @Test
    fun `a template's preset carries the template's style to the gate`() {
        val look = TemplateLook(sceneId = StyleCatalogue.DEFAULT_ID, milkPreset = null)
        assertEquals(StyleCatalogue.DEFAULT_ID, look.toPreset("Night drive").sceneId)
    }

    @Test
    fun `a template on a Pro style is refused for free users and allowed for Pro`() {
        val pro = TemplateLook(sceneId = StyleCatalogue.DEFAULT_ID).toPreset("Pro look")
        assertFalse("silk is a Pro renderer", FeatureGate.FREE.allowsStyle(pro.sceneId))
        assertTrue(FeatureGate(Entitlement.Pro).allowsStyle(pro.sceneId))
    }

    @Test
    fun `a template on a free style still applies for free users`() {
        val free = TemplateLook(sceneId = FreeTier.FALLBACK_STYLE_ID).toPreset("Free look")
        assertTrue(FeatureGate.FREE.allowsStyle(free.sceneId))
    }
}
