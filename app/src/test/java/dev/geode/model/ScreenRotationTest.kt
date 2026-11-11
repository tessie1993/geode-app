package dev.geode.model

import org.junit.Assert.assertEquals
import org.junit.Test

class ScreenRotationTest {
    @Test
    fun aSavedNameReadsBack() {
        for (choice in ScreenRotation.entries) assertEquals(choice, ScreenRotation.fromName(choice.name))
    }

    @Test
    fun nothingSavedOrAnUnknownNameFollowsThePhone() {
        assertEquals(ScreenRotation.FOLLOW, ScreenRotation.fromName(null))
        assertEquals(ScreenRotation.FOLLOW, ScreenRotation.fromName("SIDEWAYS"))
    }
}
