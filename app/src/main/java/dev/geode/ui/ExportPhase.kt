package dev.geode.ui

import android.net.Uri
import dev.geode.export.StudioExporter
import dev.geode.export.VideoExporter

sealed interface ExportPhase {
    data object Idle : ExportPhase

    data object Loading : ExportPhase

    data class Running(val progress: Float) : ExportPhase

    data class Done(val resultUri: Uri) : ExportPhase

    data class Failed(val message: String) : ExportPhase
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

val ExportPhase.resultUriOrNull: Uri?
    get() =
        when (this) {
            is ExportPhase.Done -> resultUri
            ExportPhase.Idle, ExportPhase.Loading, is ExportPhase.Running, is ExportPhase.Failed -> null
        }

val ExportPhase.errorOrNull: String?
    get() =
        when (this) {
            is ExportPhase.Failed -> message
            ExportPhase.Idle, ExportPhase.Loading, is ExportPhase.Running, is ExportPhase.Done -> null
        }

fun ExportPhase.withProgress(next: Float): ExportPhase = ExportPhase.Running(next.coerceIn(0f, 1f))

fun VideoExporter.Result.toPhase(): ExportPhase =
    when (this) {
        is VideoExporter.Result.Saved -> ExportPhase.Done(uri)
        is VideoExporter.Result.Failed -> ExportPhase.Failed(message)
        VideoExporter.Result.Cancelled -> ExportPhase.Idle
    }

// StudioExporter renders through Media3 Transformer, which is still @UnstableApi.
@androidx.annotation.OptIn(androidx.media3.common.util.UnstableApi::class)
fun StudioExporter.Result.toPhase(): ExportPhase =
    when (this) {
        is StudioExporter.Result.Saved -> ExportPhase.Done(uri)
        is StudioExporter.Result.Failed -> ExportPhase.Failed(message)
        StudioExporter.Result.Cancelled -> ExportPhase.Idle
    }
