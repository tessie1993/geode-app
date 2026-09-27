package dev.geode.render

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

class TouchCoordinatesTest {
    @Test
    fun `pointer up removes only the departing finger and uses surface coordinates`() {
        val output = FloatArray(4)
        val x = floatArrayOf(0f, 100f, 200f)
        val y = floatArrayOf(0f, 50f, 100f)
        assertEquals(2, packTouchPoints(3, 1, 200, 100, output, { x[it] }, { y[it] }))
        assertArrayEquals(floatArrayOf(-1f, 1f, 1f, -1f), output, 0f)
    }

    @Test
    fun `moving outside the canvas clamps coordinates and respects native capacity`() {
        val output = FloatArray(20)
        assertEquals(TouchField.MAX_POINTS, packTouchPoints(10, -1, 200, 100, output, { -50f }, { 150f }))
        assertArrayEquals(FloatArray(10) { -1f }, output.copyOf(10), 0f)
    }

    @Test
    fun `missing surface and empty buffers publish no pointers`() {
        assertEquals(0, packTouchPoints(2, -1, 0, 100, FloatArray(4), { error("unmeasured") }, { error("unmeasured") }))
        assertEquals(0, packTouchPoints(2, -1, 200, 100, FloatArray(0), { error("no capacity") }, { error("no capacity") }))
    }
}
