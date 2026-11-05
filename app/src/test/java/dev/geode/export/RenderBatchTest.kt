package dev.geode.export

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The scope rules that stay Kotlin. The batch's bookkeeping (each scope's tracks, skip a failure,
 * cancel the rest, progress, the summary) is native now and tested by core/tests/output_test.cpp.
 */
class RenderBatchTest {
    @Test
    fun onlyTheCurrentTrackIsNotABatch() {
        assertFalse(RenderScope.CURRENT_TRACK.isBatch)
        assertTrue(RenderScope.entries.filter { it != RenderScope.CURRENT_TRACK }.all { it.isBatch })
    }

    @Test
    fun batchesNeedAndroid10BecauseTheyCanOnlySaveThroughMediaStore() {
        // Android 8.0 to 9 (minSdk 26..28): the Movies/MuzViz insert needs a permission Geode does not hold.
        for (sdk in 26..28) assertFalse("API $sdk", RenderScope.batchSupported(sdk))
        for (sdk in listOf(29, 30, 34, 36)) assertTrue("API $sdk", RenderScope.batchSupported(sdk))
    }
}
