package dev.geode.ui

import android.content.Context
import android.net.Uri
import dev.geode.ExportTimeLimit
import dev.geode.R

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

/**
 * How a render that ended cancelled is shown: nothing for the user's own Cancel, and the reason
 * when Android stopped it at the foreground-service time limit ([ExportTimeLimit]), which would
 * otherwise look the same.
 */
fun stoppedPhase(context: Context): ExportPhase =
    if (ExportTimeLimit.justReached()) ExportPhase.Failed(context.getString(R.string.export_stopped_time_limit)) else ExportPhase.Idle

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
