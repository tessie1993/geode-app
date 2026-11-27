package dev.geode.ui

import android.Manifest
import android.app.Application
import android.app.KeyguardManager
import android.content.Context
import android.content.Intent
import android.media.projection.MediaProjection
import android.os.Looper
import androidx.test.core.app.ApplicationProvider
import dev.geode.audio.CaptureEnd
import dev.geode.audio.CaptureFailure
import dev.geode.audio.FakeProjection
import dev.geode.audio.MediaProjectionHolder
import dev.geode.audio.PlaybackCaptureService
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.android.controller.ServiceController

/**
 * A capture's projection going, through the real service and holder with libgeode stood in for: Android's ending is
 * said and nothing is listened to after it; the person's own stop is quiet.
 */
@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(RobolectricTestRunner::class)
class CaptureControllerEndTest {
    private val app: Application get() = ApplicationProvider.getApplicationContext()

    private class FakeNatives : CaptureNatives {
        override fun micActive() = false

        override var capturing = false

        override var quiet = false

        override var mixRunning = false

        var mixStarts = 0

        override fun startCapture(projection: MediaProjection): CaptureFailure? {
            capturing = true
            return null
        }

        override fun stopCapture() {
            capturing = false
        }

        override fun startMix(context: Context): Boolean {
            mixStarts++
            mixRunning = true
            return true
        }

        override fun stopMix() {
            mixRunning = false
        }
    }

    private class FakeHost : CaptureController.Host {
        val playerOwnsRing = mutableListOf<Boolean>()

        override fun pausePlayback() = Unit

        override fun resetSound(playerOwnsRing: Boolean) {
            this.playerOwnsRing += playerOwnsRing
        }

        override fun setMicReactivePref(on: Boolean) = Unit

        override val micReactivePref = false
    }

    private val natives = FakeNatives()
    private val host = FakeHost()
    private lateinit var service: ServiceController<PlaybackCaptureService>
    private lateinit var projection: FakeProjection

    @Before
    fun setUp() {
        MediaProjectionHolder.reset()
        shadowOf(app).grantPermissions(Manifest.permission.RECORD_AUDIO)
    }

    @After
    fun tearDown() {
        MediaProjectionHolder.reset()
    }

    private fun TestScope.controller(): CaptureController =
        CaptureController(app, backgroundScope, 0L, 0L, host, natives, StandardTestDispatcher(testScheduler)).also {
            it.start()
            runCurrent()
        }

    private fun TestScope.consent() {
        service = Robolectric.buildService(PlaybackCaptureService::class.java).create()
        projection = FakeProjection()
        service.get().adopt(projection.projection)
        runCurrent()
    }

    private fun TestScope.capturing(): CaptureController =
        controller().also {
            consent()
            assertTrue(it.externalAudio.value.active)
        }

    private fun TestScope.androidStops(locked: Boolean) {
        shadowOf(app.getSystemService(KeyguardManager::class.java)).setKeyguardLocked(locked)
        projection.stopBySystem()
        shadowOf(Looper.getMainLooper()).idle()
        runCurrent()
    }

    private fun TestScope.hearsQuiet(controller: CaptureController) {
        natives.quiet = true
        controller.refreshExternalAudio()
        runCurrent()
        assertTrue(natives.mixRunning)
    }

    private fun ended(controller: CaptureController): CaptureEnd? = (controller.notice.value as? CaptureNotice.Ended)?.reason

    @Test
    fun theLockEndingIsSaidAndNothingIsListenedTo() =
        runTest {
            val controller = capturing()
            androidStops(locked = true)
            assertEquals(CaptureEnd.SCREEN_LOCKED, ended(controller))
            assertEquals(CaptureEnd.SCREEN_LOCKED, controller.externalAudio.value.ended)
            assertFalse(controller.externalAudio.value.active)
            assertFalse(natives.capturing)
            assertEquals(0, natives.mixStarts)
            assertFalse(natives.mixRunning)
            assertTrue(host.playerOwnsRing.last())
        }

    @Test
    fun androidsEndingStopsTheMixAQuietCaptureOpened() =
        runTest {
            val controller = capturing()
            hearsQuiet(controller)
            androidStops(locked = false)
            assertEquals(CaptureEnd.SYSTEM, ended(controller))
            assertFalse(natives.mixRunning)
            assertEquals(1, natives.mixStarts)
            controller.refreshExternalAudio()
            runCurrent()
            assertFalse(natives.mixRunning)
        }

    @Test
    fun theNotificationsStopIsQuiet() =
        runTest {
            val controller = capturing()
            hearsQuiet(controller)
            service.withIntent(Intent().setAction(PlaybackCaptureService.ACTION_STOP)).startCommand(0, 1).destroy()
            runCurrent()
            assertNull(controller.notice.value)
            assertNull(controller.externalAudio.value.ended)
            assertFalse(controller.externalAudio.value.active)
            assertFalse(natives.mixRunning)
        }

    @Test
    fun resumingClearsTheNotice() =
        runTest {
            val controller = capturing()
            androidStops(locked = true)
            assertNotNull(controller.notice.value)
            consent()
            assertNull(controller.notice.value)
            assertNull(controller.externalAudio.value.ended)
            assertTrue(controller.externalAudio.value.active)
        }

    @Test
    fun cancellingTheConsentRaisesNoBanner() =
        runTest {
            val controller = controller()
            controller.noteExternalAudioConsentPending()
            controller.noteExternalAudioConsentDenied()
            assertNull(controller.notice.value)
            assertEquals(CaptureFailure.CONSENT, controller.externalAudio.value.failure)
        }

    @Test
    fun aServiceThatCannotStartIsAFailure() =
        runTest {
            val controller = controller()
            controller.noteExternalAudioConsentPending()
            MediaProjectionHolder.noteStartFailure()
            runCurrent()
            assertEquals(CaptureNotice.Failed(CaptureFailure.UNAVAILABLE), controller.notice.value)
            assertEquals(CaptureFailure.UNAVAILABLE, controller.externalAudio.value.failure)
            controller.clearNotice()
            controller.noteExternalAudioStartFailed()
            assertEquals(CaptureNotice.Failed(CaptureFailure.UNAVAILABLE), controller.notice.value)
        }

    // The lock ends the AudioRecord too, and the poll can see that before the projection's onStop arrives.
    @Test
    fun aCaptureSeenDeadBeforeOnStopStillTellsWhy() =
        runTest {
            val controller = capturing()
            natives.capturing = false
            controller.refreshExternalAudio()
            runCurrent()
            assertFalse(controller.externalAudio.value.active)
            assertTrue(host.playerOwnsRing.last())
            assertNull(shadowOf(app).nextStoppedService)
            androidStops(locked = true)
            assertEquals(CaptureEnd.SCREEN_LOCKED, ended(controller))
        }

    @Test
    fun aCaptureThatDiesUnderALiveProjectionStillStops() =
        runTest {
            val controller = capturing()
            natives.capturing = false
            controller.refreshExternalAudio()
            runCurrent()
            assertTrue(host.playerOwnsRing.last())
            advanceTimeBy(10_000)
            runCurrent()
            assertNotNull(shadowOf(app).nextStoppedService)
            assertNull(controller.notice.value)
            assertFalse(controller.externalAudio.value.active)
        }
}
