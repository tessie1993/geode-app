package dev.geode.model

import org.junit.Assert.assertEquals
import org.junit.Test

class KnobSizeTest {
    @Test
    fun smallUnlessAnotherSizeWasSaved() {
        assertEquals(KnobSize.SMALL, KnobSize.fromName(null))
        assertEquals(KnobSize.SMALL, KnobSize.fromName("HUGE"))
        KnobSize.entries.forEach { assertEquals(it, KnobSize.fromName(it.name)) }
    }
}
