package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Test

/** A geode:// link from any app or web page imports nothing unless it was read and offered for confirmation. */
class SharedLinkRouteTest {
    private val offer = SharedLinkOffer.Preset("geode://preset/x", "Shared")

    @Test
    fun aReadableLinkGoesToTheConfirmDialog() {
        assertEquals(SharedLinkRoute.Confirm(offer), sharedLinkRoute(offer, isPresetLink = true, isTemplateLink = false))
    }

    @Test
    fun aPresetLinkThatCannotBeReadIsOnlyReported() {
        // A preset link wrapping a template link (geode://preset/geode://template/...) fails the
        // preset read; it used to fall through to both imports, which wrote the template unasked.
        assertEquals(SharedLinkRoute.UnreadablePreset, sharedLinkRoute(null, isPresetLink = true, isTemplateLink = true))
        assertEquals(SharedLinkRoute.UnreadablePreset, sharedLinkRoute(null, isPresetLink = true, isTemplateLink = false))
    }

    @Test
    fun aTemplateLinkThatCannotBeReadIsOnlyReported() {
        assertEquals(SharedLinkRoute.UnreadableTemplate, sharedLinkRoute(null, isPresetLink = false, isTemplateLink = true))
    }

    @Test
    fun anythingElseIsLeftAlone() {
        assertEquals(SharedLinkRoute.NotOurs, sharedLinkRoute(null, isPresetLink = false, isTemplateLink = false))
    }
}
