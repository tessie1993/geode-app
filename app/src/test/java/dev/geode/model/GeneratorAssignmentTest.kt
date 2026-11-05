package dev.geode.model

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class GeneratorAssignmentTest {
    @Test
    fun readsEverySlotAndThePolicy() {
        val text = "0\taurora\t1\n1\t\t0\n2\tsilk\t0\n3\tkifs\t1\npolicy\t1\n"
        val a = GeneratorAssignment.parse(text)
        assertEquals(GeneratorSlot("aurora", pinned = true), a.slots[0])
        assertEquals(GeneratorSlot("", pinned = false), a.slots[1])
        assertEquals(GeneratorSlot("silk", pinned = false), a.slots[2])
        assertEquals(GeneratorSlot("kifs", pinned = true), a.slots[3])
        assertEquals(GeneratorAssignment.POLICY_DIRECTOR_SWAPS, a.policy)
    }

    @Test
    fun emptyTextIsTheDefault() {
        val a = GeneratorAssignment.parse("")
        assertEquals(GeneratorAssignment(), a)
        assertEquals(GeneratorAssignment.SLOT_COUNT, a.slots.size)
        assertTrue(a.slots.all { it.styleId.isEmpty() && !it.pinned })
        assertEquals(GeneratorAssignment.POLICY_ONLY_ME, a.policy)
    }

    @Test
    fun skipsLinesThatDoNotRead() {
        val a = GeneratorAssignment.parse("7\tfoo\t1\nx\tbar\t1\n2\tsilk\nnoise\npolicy\tx\n1\tglow\t1\r\n")
        assertEquals(GeneratorSlot("glow", pinned = true), a.slots[1])
        assertFalse(a.slots[2].pinned)
        assertEquals("", a.slots[2].styleId)
        assertEquals(GeneratorAssignment.POLICY_ONLY_ME, a.policy)
    }

    @Test
    fun poolIsOneIdPerLineInOrder() {
        assertEquals(listOf("a", "b", "c"), GeneratorAssignment.parsePool("a\nb\n\nc\n"))
        assertTrue(GeneratorAssignment.parsePool("").isEmpty())
    }
}
