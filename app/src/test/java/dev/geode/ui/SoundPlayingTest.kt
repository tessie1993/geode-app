package dev.geode.ui

import dev.geode.audio.NowPlayingBridge
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Auto's "sound is playing" input follows the source driving the visual, not the app's own player alone. */
class SoundPlayingTest {
    private val idle = ExternalAudioState()
    private val micOff = MicState()

    private fun session(playing: Boolean) = NowPlayingBridge.External("com.other", "Other", "Song", "Artist", playing = playing)

    @Test
    fun theOwnPlayerPlayingDrives() {
        assertTrue(soundPlaying(playerPlaying = true, external = idle, mic = micOff))
        assertFalse(soundPlaying(playerPlaying = false, external = idle, mic = micOff))
    }

    @Test
    fun aCapturedAppThatIsHeardDrivesWhileThePlayerIsPaused() {
        assertTrue(soundPlaying(false, ExternalAudioState(active = true), micOff))
        assertTrue(soundPlaying(false, ExternalAudioState(active = true, nowPlaying = session(playing = true)), micOff))
    }

    @Test
    fun aCaptureHearingSilenceOrAPausedSessionDoesNotDrive() {
        assertFalse(soundPlaying(false, ExternalAudioState(active = true, hearingSilence = true), micOff))
        assertFalse(soundPlaying(false, ExternalAudioState(active = true, nowPlaying = session(playing = false)), micOff))
        assertFalse(soundPlaying(false, ExternalAudioState(active = false, nowPlaying = session(playing = true)), micOff))
    }

    @Test
    fun theMicDrives() {
        assertTrue(soundPlaying(false, idle, MicState(active = true)))
    }
}
