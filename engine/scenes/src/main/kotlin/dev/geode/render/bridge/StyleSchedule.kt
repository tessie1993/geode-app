package dev.geode.render.bridge

/**
 * What an export segment hands the native renderer so every style switch renders (Studio 2D):
 * a recorded take's switches as [cues] on render time, and a beat cut through [beatCutIds] every
 * [everyBeats] beats. Values only; when each switch lands is decided natively
 * (`core/viz/StyleSchedule`, ADR 0003), so the beat it counts is the export's own sound engine's.
 *
 * [lockedStyleIds] is the entitlement as a value: the catalogue styles this user may not draw
 * (empty for Pro and the trial). The native schedule drops every cue and cut id among them (#357),
 * so a take recorded during the trial renders only the styles the user owns now.
 */
data class StyleSchedule(
    val cues: List<Cue> = emptyList(),
    val beatCutIds: List<String> = emptyList(),
    val everyBeats: Int = 4,
    val lockedStyleIds: List<String> = emptyList(),
) {
    /** Switch to [sceneId] (and the MilkDrop preset [milkPath], if any) at [atMs] since the segment began. */
    data class Cue(
        val atMs: Long,
        val sceneId: String,
        val milkPath: String? = null,
    )

    val isEmpty: Boolean get() = cues.isEmpty() && beatCutIds.isEmpty()

    companion object {
        val NONE = StyleSchedule()
    }
}
