package dev.geode.ui

import androidx.compose.runtime.mutableStateOf
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/** [CaptureFlow] with the system's dialogs as counters: nothing reaches Android before the disclosure is accepted. */
class CaptureFlowTest {
    private val mic = mutableStateOf(PermissionStatus.NOT_ASKED)
    private var micDialogs = 0
    private var settingsOpened = 0
    private var consents = 0
    private var refusalsNoted = 0

    private val flow =
        CaptureFlow(
            PermissionRequestState(mic, launch = { micDialogs++ }, settings = { settingsOpened++ }),
            askConsent = { consents++ },
            onMicRefused = { refusalsNoted++ },
        )

    @Before
    fun freshProcess() {
        CaptureDisclosure.accepted = false
    }

    @Test
    fun withoutTheMicNothingLaunchesUntilTheDisclosureIsAccepted() {
        flow.start()
        assertTrue(flow.disclosing)
        assertEquals(0, micDialogs)
        assertEquals(0, consents)

        flow.accept()
        assertFalse(flow.disclosing)
        assertEquals(1, micDialogs)
        assertEquals(0, consents)

        mic.value = PermissionStatus.GRANTED
        flow.micGranted()
        assertEquals(1, consents)
    }

    @Test
    fun cancelLaunchesNothing() {
        flow.start()
        flow.cancel()
        assertFalse(flow.disclosing)
        assertEquals(0, micDialogs + settingsOpened + consents)
    }

    @Test
    fun aRefusalIsNotedAndExplainedAndAFinalOneRoutesToSettings() {
        flow.start()
        flow.accept()
        mic.value = PermissionStatus.DENIED
        flow.micRefused()
        assertEquals(1, refusalsNoted)
        assertTrue(flow.disclosing)

        flow.accept()
        assertEquals(2, micDialogs)
        mic.value = PermissionStatus.PERMANENTLY_DENIED
        flow.micRefused()
        flow.accept()
        assertEquals(1, settingsOpened)
        assertEquals(2, micDialogs)
        assertEquals(0, consents)
    }

    @Test
    fun aGrantThisFlowDidNotAskForStartsNothing() {
        mic.value = PermissionStatus.GRANTED
        flow.micGranted()
        flow.micRefused()
        assertEquals(0, consents)
        assertEquals(0, refusalsNoted)
        assertFalse(flow.disclosing)
    }

    @Test
    fun onceAcceptedWithTheMicGrantedATapGoesStraightToConsent() {
        mic.value = PermissionStatus.GRANTED
        flow.start()
        assertTrue(flow.disclosing)
        flow.accept()
        assertEquals(1, consents)

        flow.start()
        assertFalse(flow.disclosing)
        assertEquals(2, consents)
        assertEquals(0, micDialogs)
    }
}
