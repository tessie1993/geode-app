package dev.geode.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import dev.geode.audio.AudioFxState
import dev.geode.data.AppState
import dev.geode.data.GeneralSettings
import dev.geode.data.LibrarySettings
import dev.geode.data.PlayerPrefs
import dev.geode.data.SoundInputSettings
import dev.geode.data.VisualsSettings
import dev.geode.di.PlayerSessionProvider
import dev.geode.model.WatermarkChoice
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class SettingsViewModel
    @Inject
    constructor(
        private val sessions: PlayerSessionProvider,
    ) : ViewModel() {
        private val session: PlayerSession = sessions.get()
        private val container = session.container

        val general: StateFlow<GeneralSettings> get() = container.general.value

        val soundInput: StateFlow<SoundInputSettings> get() = container.soundInput.value

        val visuals: StateFlow<VisualsSettings> get() = container.visuals.value

        val library: StateFlow<LibrarySettings> get() = container.library.value

        val appState: StateFlow<AppState> get() = container.appState.value

        private val _userDataLoaded = MutableStateFlow(false)

        /** True once what the first frame draws with is read: the look and onboarding's state (the splash waits on it). */
        val userDataLoaded: StateFlow<Boolean> = _userDataLoaded.asStateFlow()

        init {
            viewModelScope.launch {
                container.general.loaded()
                container.appState.loaded()
                _userDataLoaded.value = true
            }
        }

        fun updateGeneral(transform: (GeneralSettings) -> GeneralSettings) {
            session.settings.update(container.general, transform)
        }

        fun updateSoundInput(transform: (SoundInputSettings) -> SoundInputSettings) {
            session.settings.update(container.soundInput, transform)
        }

        fun updateVisuals(transform: (VisualsSettings) -> VisualsSettings) {
            session.settings.update(container.visuals, transform)
        }

        fun updateLibrary(transform: (LibrarySettings) -> LibrarySettings) {
            session.settings.update(container.library, transform)
        }

        fun updateAppState(transform: (AppState) -> AppState) {
            session.settings.update(container.appState, transform)
        }

        val playerPrefs: StateFlow<PlayerPrefs> get() = session.settings.playerPrefs

        val audioFx: StateFlow<AudioFxState> get() = session.settings.audioFxState

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
