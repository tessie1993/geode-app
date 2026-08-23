package dev.geode.ui

import androidx.lifecycle.ViewModel
import dagger.hilt.android.lifecycle.HiltViewModel
import dev.geode.audio.AudioFxState
import dev.geode.data.PlayerPrefs
import dev.geode.di.PlayerSessionProvider
import dev.geode.ui.theme.ThemePack
import kotlinx.coroutines.flow.StateFlow
import javax.inject.Inject

@HiltViewModel
class SettingsViewModel
    @Inject
    constructor(
        private val sessions: PlayerSessionProvider,
    ) : ViewModel() {
        private val session: PlayerSession = sessions.get()
        private val userData: UserDataRepository = session.userDataRepository

        val theme: StateFlow<ThemePack> get() = userData.theme

        val guiPrefs: StateFlow<GuiPrefs> get() = userData.guiPrefs

        val userDataLoaded: StateFlow<Boolean> get() = userData.loaded

        val playerPrefs: StateFlow<PlayerPrefs> get() = session.playerPrefs

        val audioFx: StateFlow<AudioFxState> get() = session.audioFx

        fun setTheme(theme: ThemePack) = session.setTheme(theme)

        fun setGuiPrefs(prefs: GuiPrefs) = session.setGuiPrefs(prefs)

        fun setPlayerPrefs(prefs: PlayerPrefs) = session.setPlayerPrefs(prefs)

        fun setAudioFxEnabled(enabled: Boolean) = session.setAudioFxEnabled(enabled)

        fun setAudioFxBand(
            band: Int,
            levelMb: Int,
        ) = session.setAudioFxBand(band, levelMb)

        fun useAudioFxPreset(index: Int) = session.useAudioFxPreset(index)

        fun setAudioFxBassBoost(strength: Int) = session.setAudioFxBassBoost(strength)

        fun setAudioFxLoudness(gainMb: Int) = session.setAudioFxLoudness(gainMb)
    }
