package dev.geode.ui

import android.net.Uri
import dev.geode.export.ExportAspect
import dev.geode.export.ExportCodec
import dev.geode.export.TimeOfDayDrift

/**
 * Everything [LoopRenderSheet] hands the controller: the render's quality (taken from the same
 * export defaults the standard export uses) and the loop-specific controls the sheet owns.
 */
data class LoopRenderRequest(
    val aspect: ExportAspect,
    val codec: ExportCodec,
    val fps: Int,
    val loopMs: Long,
    val crossfadeMs: Long,
    val drift: TimeOfDayDrift,
    val audioClips: List<Uri>,
)
