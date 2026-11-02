package dev.geode.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import dev.geode.audio.AudioFxState
import dev.geode.data.GuiPrefs
import dev.geode.data.PlayerPrefs
import dev.geode.data.SettingsReset
import dev.geode.data.StorageBucket
import dev.geode.data.StorageClearResult
import dev.geode.data.StorageRepository
import dev.geode.data.StorageUsage
import dev.geode.data.UserDataRepository
import dev.geode.di.PlayerSessionProvider
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
        private val settingsReset: SettingsReset,
        private val storageRepository: StorageRepository,
    ) : ViewModel() {
        private val session: PlayerSession = sessions.get()
        private val userData: UserDataRepository = session.userDataRepository

        val guiPrefs: StateFlow<GuiPrefs> get() = userData.guiPrefs

        val userDataLoaded: StateFlow<Boolean> get() = userData.loaded

        val playerPrefs: StateFlow<PlayerPrefs> get() = session.playerPrefs

        val audioFx: StateFlow<AudioFxState> get() = session.audioFx

        private val _storage = MutableStateFlow<StorageUsage?>(null)

        /** Settings › Backup & about › Storage: null until [refreshStorage] has measured. */
        val storage: StateFlow<StorageUsage?> = _storage.asStateFlow()

        private val _resetDone = MutableStateFlow<Boolean?>(null)

        /**
         * Reset all's outcome: null before, then whether every file was written. Either way the
         * app must restart before the reset is whole (see [SettingsReset]).
         */
        val resetDone: StateFlow<Boolean?> = _resetDone.asStateFlow()

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

        fun refreshStorage() {
            viewModelScope.launch { _storage.value = storageRepository.usage() }
        }

        /** Empties the [bucket] cache, then measures again. */
        fun clearStorage(
            bucket: StorageBucket,
            onDone: (StorageClearResult) -> Unit = {},
        ) {
            viewModelScope.launch {
                val result = storageRepository.clear(bucket)
                _storage.value = storageRepository.usage()
                onDone(result)
            }
        }

        fun resetAllSettings() {
            viewModelScope.launch { _resetDone.value = settingsReset.resetAll() }
        }
    }
