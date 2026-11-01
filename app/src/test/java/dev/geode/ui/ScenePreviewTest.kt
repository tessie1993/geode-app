package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Leaving the Styles grid over a preview (opening Customize to turn a macro, say) must keep the
 * tapped style. It used to revert, so the first macro turn landed on the old or default style.
 */
class ScenePreviewTest {
    @Test
    fun leavingKeepsAPreviewTheUserMayUse() {
        assertEquals("silk_bloom", sceneAfterPreview("silk_bloom", "silk_web") { true })
    }

    @Test
    fun leavingDropsALockedPreview() {
        assertEquals("silk_web", sceneAfterPreview("arbor_pro", "silk_web") { it != "arbor_pro" })
    }
}
