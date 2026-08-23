package dev.geode.ui

import androidx.annotation.OptIn
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import dev.geode.analysis.AnalysisEngine
import dev.geode.analysis.PlaybackMath
import dev.geode.audio.AudioFxController
import dev.geode.audio.AudioFxState
import dev.geode.data.PlayerPrefs
import dev.geode.data.PlayerPrefsRepository
import dev.geode.ui.theme.ThemePack
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

@OptIn(UnstableApi::class)
internal class PlayerSettingsController(
    private val userData: UserDataRepository,
    private val playerPrefsRepository: PlayerPrefsRepository,
    private val scope: CoroutineScope,
    private val player: ExoPlayer,
    private val engine: AnalysisEngine,
    private val audioFx: AudioFxController,
    private val host: Host,
) {
    interface Host {
        fun redecideCachedBeats(prefs: GuiPrefs)

        fun refreshUi()
    }

    val theme: StateFlow<ThemePack> = userData.theme

    val guiPrefs: StateFlow<GuiPrefs> = userData.guiPrefs

    val playerPrefs: StateFlow<PlayerPrefs> = playerPrefsRepository.prefs

    suspend fun loadedPlayerPrefs(): PlayerPrefs = playerPrefsRepository.loaded()

    private val _audioFxState = MutableStateFlow(audioFx.snapshot())
    val audioFxState: StateFlow<AudioFxState> = _audioFxState

    fun setGuiPrefs(prefs: GuiPrefs) {
        val previous = guiPrefs.value
        scope.launch { userData.setGuiPrefs(prefs) }
        engine.beatSensitivity = prefs.beatSensitivity
        engine.beatMinIntervalMs = prefs.effectiveBeatMinIntervalMs
        val sensitivityChanged =
            previous.beatSensitivity != prefs.beatSensitivity ||
                previous.effectiveBeatMinIntervalMs != prefs.effectiveBeatMinIntervalMs
        if (sensitivityChanged) host.redecideCachedBeats(prefs)
    }

    fun setTheme(theme: ThemePack) {
        scope.launch { userData.setTheme(theme) }
    }

    fun setPlayerPrefs(prefs: PlayerPrefs) {
        val p = prefs.coerced()
        scope.launch { playerPrefsRepository.update { p } }
        applyPlaybackPrefs(p)
    }

    fun applyPlaybackPrefs(p: PlayerPrefs) {
        player.playbackParameters = PlaybackParameters(p.speed, PlaybackMath.semitonesToRatio(p.pitchSemitones))
        player.skipSilenceEnabled = p.skipSilence
        player.setHandleAudioBecomingNoisy(p.pauseOnNoisy)
    }

    private fun persistPlayerOptions() {
        val shuffle = player.shuffleModeEnabled
        val repeatMode = player.repeatMode
        scope.launch {
            playerPrefsRepository.update { it.copy(shuffle = shuffle, repeatMode = repeatMode) }
        }
    }

    fun toggleShuffle() {
        player.shuffleModeEnabled = !player.shuffleModeEnabled
        persistPlayerOptions()
        host.refreshUi()
    }

    fun cycleRepeatMode() {
        player.repeatMode =
            when (player.repeatMode) {
                Player.REPEAT_MODE_OFF -> Player.REPEAT_MODE_ALL
                Player.REPEAT_MODE_ALL -> Player.REPEAT_MODE_ONE
                else -> Player.REPEAT_MODE_OFF
            }
        persistPlayerOptions()
        host.refreshUi()
    }

    fun refreshAudioFx() {
        _audioFxState.value = audioFx.snapshot()
    }

    fun setAudioFxEnabled(enabled: Boolean) {
        audioFx.setEnabled(enabled)
        refreshAudioFx()
    }

    fun setAudioFxBand(
        band: Int,
        levelMb: Int,
    ) {
        audioFx.setBandLevel(band, levelMb)
        refreshAudioFx()
    }

    fun useAudioFxPreset(index: Int) {
        audioFx.usePreset(index)
        refreshAudioFx()
    }

    fun setAudioFxBassBoost(strength: Int) {
        audioFx.setBassBoost(strength)
        refreshAudioFx()
    }

    fun setAudioFxLoudness(gainMb: Int) {
        audioFx.setLoudness(gainMb)
        refreshAudioFx()
    }
}
