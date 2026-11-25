package dev.geode.playback

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

/** The playback service is promised to the foreground only when a plain start is refused. */
class ServiceStartTest {
    @Test
    fun aPlainStartThatTakesMakesNoPromise() {
        var promises = 0
        startPromisingOnlyWhenRefused(plain = {}, promised = { promises++ })
        assertEquals(0, promises)
    }

    @Test
    fun aRefusedPlainStartFallsBackToThePromiseOnce() {
        var promises = 0
        startPromisingOnlyWhenRefused(
            plain = { throw IllegalStateException("Not allowed to start service: app is in background") },
            promised = { promises++ },
        )
        assertEquals(1, promises)
    }

    @Test
    fun aRefusedPromiseReachesTheCaller() {
        assertThrows(IllegalStateException::class.java) {
            startPromisingOnlyWhenRefused(
                plain = { throw IllegalStateException("background") },
                promised = { throw IllegalStateException("ForegroundServiceStartNotAllowedException") },
            )
        }
    }
}
