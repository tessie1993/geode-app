package dev.geode.cast

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.emptyFlow
import org.junit.After
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

/** The UI's flows are let go when the screen that fed them goes, and an old screen never stops a newer one's. */
@RunWith(RobolectricTestRunner::class)
class CastMirrorFollowTest {
    private val milk = MutableStateFlow<String?>(null)

    @After
    fun tearDown() {
        CastMirror.unfollow(CastMirror.follow(emptyFlow(), emptyFlow()))
    }

    @Test
    fun unfollowStopsTheCollector() {
        val job = CastMirror.follow(emptyFlow(), milk)
        CastMirror.unfollow(job)
        assertTrue(job.isCancelled)
    }

    @Test
    fun anOldScreenGoingLeavesTheNewOneFollowing() {
        val old = CastMirror.follow(emptyFlow(), milk)
        val next = CastMirror.follow(emptyFlow(), milk)
        assertTrue(old.isCancelled)
        CastMirror.unfollow(old)
        assertFalse(next.isCancelled)
        CastMirror.unfollow(next)
        assertTrue(next.isCancelled)
    }
}
