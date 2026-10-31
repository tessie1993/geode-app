package dev.geode.ui

import dev.geode.export.ExportAspect
import dev.geode.export.ExportCodec
import dev.geode.export.ExportRange

/**
 * One visualizer export of the current track, as the Render tab asks for it. Flat primitives so it
 * survives a configuration change while the folder picker is open (see `rememberVisualizerExport`).
 */
internal data class PendingExport(
    val aspect: ExportAspect,
    val fps: Int,
    val sceneId: String,
    val rangeStartMs: Long,
    val rangeDurationMs: Long,
    val codec: ExportCodec,
) {
    val range: ExportRange? get() = if (rangeDurationMs > 0) ExportRange(rangeStartMs, rangeDurationMs) else null
}
