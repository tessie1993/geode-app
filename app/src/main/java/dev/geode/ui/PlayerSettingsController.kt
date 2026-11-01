package dev.geode.ui

import androidx.media3.common.Player
import dev.geode.audio.AudioFxController
import dev.geode.audio.AudioFxState
import dev.geode.data.GuiPrefs
import dev.geode.data.PlayerPrefs
import dev.geode.data.PlayerPrefsRepository
import dev.geode.data.UserDataRepository
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

internal class PlayerSettingsController(
    private val userData: UserDataRepository,
    private val playerPrefsRepository: PlayerPrefsRepository,
    private val scope: CoroutineScope,
    private val player: Player,
    private val audioFx: AudioFxController,
    private val host: Host,
) {
    interface Host {
        fun refreshUi()

        /** Hands the prefs to the playback session, which owns the player, ReplayGain and the USB route. */
        fun applyPlaybackPrefs(prefs: PlayerPrefs)
    }

    val guiPrefs: StateFlow<GuiPrefs> = userData.guiPrefs

    val playerPrefs: StateFlow<PlayerPrefs> = playerPrefsRepository.prefs

    suspend fun loadedPlayerPrefs(): PlayerPrefs = playerPrefsRepository.loaded()

    private val _audioFxState = MutableStateFlow(audioFx.snapshot())
    val audioFxState: StateFlow<AudioFxState> = _audioFxState

    fun setGuiPrefs(prefs: GuiPrefs) {
        scope.launch { userData.setGuiPrefs(prefs) }
    }

    fun setPlayerPrefs(prefs: PlayerPrefs) {
        val p = prefs.coerced()
        scope.launch { playerPrefsRepository.update { p } }
        applyPlaybackPrefs(p)
    }

    fun applyPlaybackPrefs(p: PlayerPrefs) = host.applyPlaybackPrefs(p)

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
