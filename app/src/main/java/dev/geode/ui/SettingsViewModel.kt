package dev.geode.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import dev.geode.audio.AudioFxState
import dev.geode.data.GuiPrefs
import dev.geode.data.PlayerPrefs
import dev.geode.data.UserDataRepository
import dev.geode.di.PlayerSessionProvider
import dev.geode.model.WatermarkChoice
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class SettingsViewModel
    @Inject
    constructor(
        private val sessions: PlayerSessionProvider,
    ) : ViewModel() {
        private val session: PlayerSession = sessions.get()
        private val userData: UserDataRepository = session.userDataRepository

        val guiPrefs: StateFlow<GuiPrefs> get() = userData.guiPrefs

        val userDataLoaded: StateFlow<Boolean> get() = userData.loaded

        val playerPrefs: StateFlow<PlayerPrefs> get() = session.settings.playerPrefs

        val audioFx: StateFlow<AudioFxState> get() = session.settings.audioFxState

        fun setGuiPrefs(prefs: GuiPrefs) = session.setGuiPrefs(prefs)

        fun setPlayerPrefs(prefs: PlayerPrefs) = session.setPlayerPrefs(prefs)

        val watermark: StateFlow<WatermarkChoice> get() = session.watermarkPrefs.value

        fun updateWatermark(transform: (WatermarkChoice) -> WatermarkChoice) {
            viewModelScope.launch { session.watermarkPrefs.update(transform) }
        }

        fun setAudioFxEnabled(enabled: Boolean) = session.settings.setAudioFxEnabled(enabled)

        fun setAudioFxBand(
            band: Int,
            levelMb: Int,
        ) = session.settings.setAudioFxBand(band, levelMb)

        fun useAudioFxPreset(index: Int) = session.settings.useAudioFxPreset(index)

        fun setAudioFxBassBoost(strength: Int) = session.settings.setAudioFxBassBoost(strength)

        fun setAudioFxLoudness(gainMb: Int) = session.settings.setAudioFxLoudness(gainMb)
    }
