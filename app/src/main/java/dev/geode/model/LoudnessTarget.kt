package dev.geode.model

/**
 * Where an export's loudness should end up. Three choices, deliberately: a numeric LUFS field would ask the
 * user to know something the platforms already decided for them. [id] is persisted; [nativeIndex] is
 * `core/output/LoudnessAdvice`'s `Target`, which holds every number and decision.
 */
enum class LoudnessTarget(
    val id: String,
    val nativeIndex: Int,
) {
    YouTube("youtube", 1),
    ShortsAndTikTok("shorts", 2),
    LeaveAsIs("as-is", 0),
    ;

    companion object {
        /** Reads a persisted [id] back. An id from an older build falls back to the safest choice. */
        fun byId(id: String?): LoudnessTarget = entries.firstOrNull { it.id == id } ?: LeaveAsIs
    }
}

/**
 * What native made of a finished render's loudness for [target] (`GeodeNative.exportLoudnessMeasure`):
 * [sentence] is `core/output/LoudnessAdvice`'s `Sentence`, and the numbers are the ones its wording reads.
 * [gainDb] is advice: nothing applies it yet.
 */
data class LoudnessAdvice(
    val target: LoudnessTarget,
    val sentence: Int,
    val lufs: Double,
    val truePeakDbtp: Double,
    val gainDb: Double,
    val gainForLevelDb: Double,
    val resultingLufs: Double,
    val resultingTruePeakDbtp: Double,
    val ceilingDbtp: Double,
    val quietLu: Double,
    val peakWarning: Boolean,
)
