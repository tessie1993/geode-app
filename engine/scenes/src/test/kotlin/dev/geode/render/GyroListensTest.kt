package dev.geode.render

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** [GyroSensor] runs only while all three hold; any one of them dropping stops it (leaving Visuals, a pause). */
class GyroListensTest {
    @Test
    fun listensOnlyWhenWatchedResumedAndWanted() {
        assertTrue(gyroListens(watched = true, resumed = true, wanted = true))
        assertFalse(gyroListens(watched = false, resumed = true, wanted = true))
        assertFalse(gyroListens(watched = true, resumed = false, wanted = true))
        assertFalse(gyroListens(watched = true, resumed = true, wanted = false))
    }
}
