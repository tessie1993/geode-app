package dev.geode.ui

import android.net.Uri
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import dev.geode.RingLog
import dev.geode.data.backup.BackupException
import dev.geode.data.backup.BackupPreview
import dev.geode.data.backup.BackupProblem
import dev.geode.data.backup.BackupRepository
import dev.geode.data.backup.BackupSection
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

/** Settings › Backup, one value at a time: the screen draws whichever this is. */
sealed interface BackupUiState {
    data object Idle : BackupUiState

    data object Working : BackupUiState

    data object Exported : BackupUiState

    /** A backup passed every check and sits in staging; the user picks what to restore. */
    data class Choosing(
        val preview: BackupPreview,
        val selected: Set<BackupSection>,
    ) : BackupUiState

    /** Data is on disk but the running app still holds the old copy in memory. */
    data object Restored : BackupUiState

    data class Failed(
        val problem: BackupProblem,
    ) : BackupUiState
}

@HiltViewModel
class BackupViewModel
    @Inject
    constructor(
        private val repository: BackupRepository,
    ) : ViewModel() {
        private val state = MutableStateFlow<BackupUiState>(BackupUiState.Idle)
        val uiState: StateFlow<BackupUiState> = state.asStateFlow()

        fun export(destination: Uri) =
            launchStep {
                repository.export(destination)
                BackupUiState.Exported
            }

        fun inspect(source: Uri) =
            launchStep {
                val preview = repository.inspect(source)
                BackupUiState.Choosing(preview, preview.sections)
            }

        fun toggle(section: BackupSection) {
            val choosing = state.value as? BackupUiState.Choosing ?: return
            val selected = if (section in choosing.selected) choosing.selected - section else choosing.selected + section
            state.value = choosing.copy(selected = selected)
        }

        fun restore() {
            val choosing = state.value as? BackupUiState.Choosing ?: return
            if (choosing.selected.isEmpty()) return
            launchStep {
                repository.restore(choosing.selected)
                BackupUiState.Restored
            }
        }

        fun cancelRestore() {
            if (state.value !is BackupUiState.Choosing) return
            state.value = BackupUiState.Idle
            viewModelScope.launch { repository.discard() }
        }

        /** Clears a finished export or a failure message. */
        fun dismiss() {
            val current = state.value
            if (current is BackupUiState.Exported || current is BackupUiState.Failed) state.value = BackupUiState.Idle
        }

        private fun launchStep(block: suspend () -> BackupUiState) {
            if (state.value == BackupUiState.Working || state.value == BackupUiState.Restored) return
            state.value = BackupUiState.Working
            viewModelScope.launch {
                state.value =
                    try {
                        block()
                    } catch (e: BackupException) {
                        RingLog.note("Backup", "${e.problem}: ${e.message}")
                        BackupUiState.Failed(e.problem)
                    }
            }
        }
    }
