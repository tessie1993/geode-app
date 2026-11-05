package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * Studio lost its Edit tab when the owner dropped video editing (#227). A tab name saved before
 * then, in `data/StudioPrefsStore` or a process-death bundle, must come back as Render rather than
 * crash or land nowhere.
 */
class StudioSectionTest {
    @Test
    fun theTwoTabsRestoreByName() {
        assertEquals(StudioSection.RENDER, StudioSection.fromName("RENDER"))
        assertEquals(StudioSection.MY_VIDEOS, StudioSection.fromName("MY_VIDEOS"))
    }

    @Test
    fun aSavedEditTabRestoresToRender() {
        assertEquals(StudioSection.RENDER, StudioSection.fromName("EDIT"))
    }

    @Test
    fun noNameOrAnUnknownOneIsNoTab() {
        assertNull(StudioSection.fromName(null))
        assertNull(StudioSection.fromName("TIMELINE"))
    }

    @Test
    fun studioHasExactlyRenderAndMyVideos() {
        assertEquals(listOf(StudioSection.RENDER, StudioSection.MY_VIDEOS), StudioSection.entries.toList())
    }
}
