package dev.geode.ui

import android.net.Uri
import androidx.lifecycle.ViewModel
import dagger.hilt.android.lifecycle.HiltViewModel
import dev.geode.di.PlayerSessionProvider
import dev.geode.export.ClipEdit
import dev.geode.export.StudioClip
import kotlinx.coroutines.flow.StateFlow
import javax.inject.Inject

@HiltViewModel
class StudioViewModel
    @Inject
    constructor(
        private val sessions: PlayerSessionProvider,
    ) : ViewModel() {
        private val session: PlayerSession = sessions.get()
        val studio: StateFlow<StudioUiState> get() = session.studio

        val exportState: StateFlow<ExportUiState> get() = session.exportState

        val takeState: StateFlow<TakeUiState> get() = session.takeState

        fun startRecording() = session.startRecording()

        fun stopRecording(name: String? = null) = session.stopRecording(name)

        fun playTake(name: String) = session.playTake(name)

        fun stopReplay() = session.stopReplay()

        fun deleteTake(name: String) = session.deleteTake(name)

        fun renameTake(
            from: String,
            to: String,
        ) = session.renameTake(from, to)

        fun setExportTake(name: String?) = session.setExportTake(name)

        fun cancelExport() = session.cancelExport()

        fun resetExportState() = session.resetExportState()

        fun refreshStudioClips() = session.refreshStudioClips()

        fun describeStudioClip(
            uri: Uri,
            onReady: (StudioClip) -> Unit,
        ) = session.describeStudioClip(uri, onReady)

        fun renameStudioClip(
            uri: String,
            name: String,
            onResult: (Boolean) -> Unit,
        ) = session.renameStudioClip(uri, name, onResult)

        fun deleteStudioClip(
            uri: String,
            onResult: (Boolean) -> Unit,
        ) = session.deleteStudioClip(uri, onResult)

        fun startStudioExport(
            clip: StudioClip,
            edit: ClipEdit,
        ) = session.startStudioExport(clip, edit)

        fun cancelStudioExport() = session.cancelStudioExport()

        fun clearStudioResult() = session.clearStudioResult()
    }
