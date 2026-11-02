package dev.geode.render.bridge

/**
 * What an export segment hands the native renderer so every style switch renders (Studio 2D):
 * a recorded take's switches as [cues] on render time, and a beat cut through [beatCutIds] every
 * [everyBeats] beats. Values only; when each switch lands is decided natively
 * (`core/viz/StyleSchedule`, ADR 0003), so the beat it counts is the export's own sound engine's.
 */
data class StyleSchedule(
    val cues: List<Cue> = emptyList(),
    val beatCutIds: List<String> = emptyList(),
    val everyBeats: Int = 4,
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
