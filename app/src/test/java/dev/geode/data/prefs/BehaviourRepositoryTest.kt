package dev.geode.data.prefs

import dev.geode.data.BehaviourCodec
import dev.geode.data.BehaviourPrefs
import dev.geode.data.CanvasGesture
import dev.geode.data.GestureAction
import dev.geode.data.NotificationButton
import dev.geode.data.QueueEndAction
import dev.geode.data.StartScreen
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Test

/** Behaviour's options on the one repository (#1009, PR 4): same keys and values, and a screen change writes only its key. */
@OptIn(ExperimentalCoroutinesApi::class)
class BehaviourRepositoryTest {
    private val file = RecordingPrefs()

    private fun TestScope.repository() = PrefsSettingsRepository(file.prefs, BehaviourCodec, this, StandardTestDispatcher(testScheduler))

    private val chosen =
        BehaviourPrefs(
            startScreen = StartScreen.LAST_USED,
            lastScreen = StartScreen.STUDIO,
            keepScreenOnVisuals = false,
            startOnHeadphones = true,
            notificationButtons = setOf(NotificationButton.FAVOURITE),
            rememberShuffleRepeat = false,
            queueEnd = QueueEndAction.SIMILAR,
            dreamScreenBright = false,
            dreamShowClock = false,
            dreamShowNowPlaying = false,
            dreamHearOtherApps = true,
            dreamFps = 24,
            gestures = BehaviourPrefs.DEFAULT_GESTURES + (CanvasGesture.SWIPE_UP to GestureAction.PLAY_PAUSE),
        )

    @Test
    fun everyFieldRoundTripsUnderTodaysKeys() {
        file.prefs
            .edit()
            .also { BehaviourCodec.write(it, chosen) }
            .apply()
        assertEquals(chosen, BehaviourCodec.read(file.prefs))
        assertEquals("LAST_USED", file.values["behaviour_start_screen"])
        assertEquals(setOf("FAVOURITE"), file.values["behaviour_notification_buttons"])
        assertEquals("PLAY_PAUSE", file.values["behaviour_gesture_SWIPE_UP"])
        assertEquals(24, file.values["behaviour_dream_fps"])
    }

    @Test
    fun recordingTheLastScreenWritesThatKeyAlone() =
        runTest {
            val repo = repository()
            repo.update { it.copy(lastScreen = StartScreen.VISUALS) }
            assertEquals(mapOf("behaviour_last_screen" to "VISUALS"), file.written.toMap())
            repo.update { it.copy(gestures = it.gestures + (CanvasGesture.SWIRL to GestureAction.OFF)) }
            assertEquals(setOf("behaviour_last_screen", "behaviour_gesture_SWIRL"), file.written.keys)
        }

    @Test
    fun aStoredLastUsedOrUnknownNameReadsAsTheDefault() =
        runTest {
            file.values += mapOf("behaviour_last_screen" to "LAST_USED", "behaviour_queue_end" to "SHUFFLE_FOREVER")
            val loaded = repository().loaded()
            assertEquals(BehaviourPrefs().lastScreen, loaded.lastScreen)
            assertEquals(QueueEndAction.STOP, loaded.queueEnd)
        }

    @Test
    fun currentReadsTheFileBeforeTheLoadAndTheLatestValueAfter() =
        runTest {
            file.values += mapOf("behaviour_start_screen" to "VISUALS")
            val repo = repository()
            assertEquals(StartScreen.VISUALS, repo.current().startScreen)
            repo.update { it.copy(startScreen = StartScreen.STUDIO) }
            assertEquals(StartScreen.STUDIO, repo.current().startScreen)
        }
}
