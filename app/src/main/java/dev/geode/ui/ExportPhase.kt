package dev.geode.ui

import android.net.Uri

sealed interface ExportPhase {
    data object Idle : ExportPhase

    data object Loading : ExportPhase

    data class Running(
        val progress: Float,
    ) : ExportPhase

    data class Done(
        val resultUri: Uri,
    ) : ExportPhase

    data class Failed(
        val message: String,
    ) : ExportPhase
}

val ExportPhase.isBusy: Boolean
    get() =
        when (this) {
            ExportPhase.Loading, is ExportPhase.Running -> true
            ExportPhase.Idle, is ExportPhase.Done, is ExportPhase.Failed -> false
        }

val ExportPhase.isRunning: Boolean
    get() =
        when (this) {
            is ExportPhase.Running -> true
            ExportPhase.Idle, ExportPhase.Loading, is ExportPhase.Done, is ExportPhase.Failed -> false
        }

val ExportPhase.progress: Float
    get() =
        when (this) {
            is ExportPhase.Running -> progress
            is ExportPhase.Done -> 1f
            ExportPhase.Idle, ExportPhase.Loading, is ExportPhase.Failed -> 0f
        }
