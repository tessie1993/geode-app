package dev.geode.export

import java.util.Locale
import kotlin.math.abs
import kotlin.math.pow

/** Under this much, "quieter than YouTube" is noise rather than information. */
private const val QUIET_HINT_LU = 1.0

private const val YOUTUBE_CEILING_DBTP = -1.0

/**
 * The level a platform plays back at, and the headroom it wants left underneath.
 *
 * [lowLufs]..[highLufs] is the band that counts as "already right" — nobody should be told to
 * re-export over a difference they cannot hear. [aimLufs] is where a normalising gain lands.
 */
data class LoudnessWindow(
    val aimLufs: Double,
    val lowLufs: Double,
    val highLufs: Double,
    val ceilingDbtp: Double,
)

/**
 * Where the export's loudness should end up. Three choices, deliberately — a numeric LUFS field
 * would ask the user to know something the platforms already decided for them.
 */
sealed interface LoudnessTarget {
    val id: String
    val label: String
    val summary: String

    /**
     * A target that can actually move the level. Only these carry a [window], so there is no
     * "what is the target loudness of Leave as-is" state to get wrong.
     */
    sealed interface Normalising : LoudnessTarget {
        val window: LoudnessWindow

        /** How the target reads in prose, e.g. "about -14 LUFS". */
        val levelPhrase: String

        /** What the platform does to a file that arrives under the target. */
        val quietNote: String

        /** What the platform does to a file that arrives over it. */
        val loudNote: String
    }

    data object YouTube : Normalising {
        override val id: String = "youtube"
        override val label: String = "YouTube"
        override val summary: String = "About -14 LUFS with 1 dB of true-peak headroom."
        override val window: LoudnessWindow =
            LoudnessWindow(aimLufs = -14.0, lowLufs = -15.0, highLufs = -13.0, ceilingDbtp = -1.0)
        override val levelPhrase: String = "about -14 LUFS"
        override val quietNote: String =
            "YouTube only ever turns audio down, never up, so a quiet upload simply plays quiet next to everything else"
        override val loudNote: String =
            "YouTube will pull it back to about -14 LUFS on playback whatever you do, so nothing here is broken"
    }

    data object ShortsAndTikTok : Normalising {
        override val id: String = "shorts"
        override val label: String = "Shorts & TikTok"
        override val summary: String = "About -10 to -12 LUFS — phone-speaker loud, the way short-form feeds sit."
        override val window: LoudnessWindow =
            LoudnessWindow(aimLufs = -11.0, lowLufs = -12.0, highLufs = -10.0, ceilingDbtp = -1.0)
        override val levelPhrase: String = "-10 to -12 LUFS"
        override val quietNote: String =
            "short-form feeds normalise loosely and phone speakers are heard in noisy rooms, so a quiet upload gets lost"
        override val loudNote: String =
            "pushed much past this the feeds pull it back anyway, and over-loud short-form audio is what sounds crushed"
    }

    data object LeaveAsIs : LoudnessTarget {
        override val id: String = "as-is"
        override val label: String = "Leave as-is"
        override val summary: String = "Ship the level you mixed. Geode still measures the file and says what a platform will do."
    }

    companion object {
        /** The whole menu. Exactly three, in the order they should be offered. */
        val ALL: List<LoudnessTarget> = listOf(YouTube, ShortsAndTikTok, LeaveAsIs)

        /** Reads a persisted [id] back. An id from an older build falls back to the safest choice. */
        fun byId(id: String?): LoudnessTarget = ALL.firstOrNull { it.id == id } ?: LeaveAsIs
    }
}

/** Which way a normalising gain moves the file. */
enum class GainDirection {
    UP,
    DOWN,
    UNCHANGED,
}

/**
 * What to do about a measured file, and how to say it to someone who does not know what a LUFS is.
 *
 * [gainDb] is always the gain to apply on export, so a caller that only wants the number never has
 * to unpack the variant. [headline] fits a row or a button; [detail] is the paragraph underneath.
 */
sealed interface LoudnessAdvice {
    val gainDb: Double
    val headline: String
    val detail: String

    /** [gainDb] as a multiplier, ready for a gain stage. */
    val linearGain: Float
        get() = 10.0.pow(gainDb / 20.0).toFloat()

    data object NothingToMeasure : LoudnessAdvice {
        override val gainDb: Double = 0.0
        override val headline: String = "No audio to measure"
        override val detail: String =
            "Nothing in this file rises above the -70 LUFS gate, so there is no loudness to report. If the export was " +
                "meant to have sound, it did not capture any."
    }

    data class AsMixed(
        val lufs: Double,
        val truePeakDbtp: Double,
    ) : LoudnessAdvice {
        override val gainDb: Double = 0.0
        override val headline: String = "Leaving the level alone"

        override val detail: String
            get() =
                buildString {
                    append("Measured ${LoudnessTargets.lufsText(lufs)}, true peak ${LoudnessTargets.dbtpText(truePeakDbtp)}. ")
                    append("Geode applies no gain, so the file goes out exactly as mixed.")
                    val underYouTube = LoudnessTarget.YouTube.window.aimLufs - lufs
                    if (underYouTube > QUIET_HINT_LU) {
                        append(" For reference: ${LoudnessTarget.YouTube.quietNote}, and this sits ")
                        append("${LoudnessTargets.luText(underYouTube)} under it.")
                    }
                    if (truePeakDbtp > YOUTUBE_CEILING_DBTP) {
                        append(" The true peak is above -1 dBTP, which is where lossy encoders start to distort.")
                    }
                }
    }

    data class OnTarget(
        val target: LoudnessTarget.Normalising,
        val lufs: Double,
        val truePeakDbtp: Double,
    ) : LoudnessAdvice {
        override val gainDb: Double = 0.0
        override val headline: String = "Already at the ${target.label} level"

        override val detail: String
            get() =
                "Measured ${LoudnessTargets.lufsText(lufs)}, inside the ${target.levelPhrase} ${target.label} plays at, " +
                    "and the true peak of ${LoudnessTargets.dbtpText(truePeakDbtp)} clears the " +
                    "${LoudnessTargets.dbtpText(target.window.ceilingDbtp)} ceiling. Nothing to apply."
    }

    /**
     * The file needs a gain change. [gainDb] is what will actually be applied; [gainForLevelDb] is
     * what hitting the target level alone would have needed, and the two differ whenever the
     * true-peak ceiling got in the way.
     */
    data class Normalise(
        val target: LoudnessTarget.Normalising,
        val lufs: Double,
        val truePeakDbtp: Double,
        override val gainDb: Double,
        val gainForLevelDb: Double,
        val resultingLufs: Double,
        val resultingTruePeakDbtp: Double,
    ) : LoudnessAdvice {
        val direction: GainDirection
            get() =
                when {
                    gainDb > LoudnessTargets.GAIN_TOLERANCE_DB -> GainDirection.UP
                    gainDb < -LoudnessTargets.GAIN_TOLERANCE_DB -> GainDirection.DOWN
                    else -> GainDirection.UNCHANGED
                }

        /** True when the -1 dBTP ceiling stopped the gain short of the target level. */
        val ceilingLimited: Boolean
            get() = gainDb < gainForLevelDb - LoudnessTargets.GAIN_TOLERANCE_DB

        override val headline: String
            get() =
                when (direction) {
                    GainDirection.UP -> "Turn it up ${LoudnessTargets.dbText(gainDb)} for ${target.label}"
                    GainDirection.DOWN -> "Turn it down ${LoudnessTargets.dbText(gainDb)} for ${target.label}"
                    GainDirection.UNCHANGED -> "Already at the ${target.label} level"
                }

        override val detail: String
            get() =
                when (direction) {
                    GainDirection.UP -> raiseDetail()
                    GainDirection.DOWN -> lowerDetail()
                    GainDirection.UNCHANGED -> "Measured ${LoudnessTargets.lufsText(lufs)}. Close enough to leave alone."
                }

        private fun raiseDetail(): String =
            buildString {
                append("Measured ${LoudnessTargets.lufsText(lufs)}, ")
                append("${LoudnessTargets.luText(gainForLevelDb)} under the ${target.levelPhrase} ${target.label} plays at. ")
                append("${target.quietNote.replaceFirstChar { it.uppercase() }}. ")
                if (ceilingLimited) {
                    append("Going the whole way would push the true peak past the ")
                    append("${LoudnessTargets.dbtpText(target.window.ceilingDbtp)} ceiling, so Geode will add only ")
                    append("${LoudnessTargets.dbText(gainDb)}, landing at ${LoudnessTargets.lufsText(resultingLufs)}. ")
                    append("Closing the rest of the gap needs a limiter on the mix, not more gain.")
                } else {
                    append("Geode will add ${LoudnessTargets.dbText(gainDb)} on export, landing at ")
                    append("${LoudnessTargets.lufsText(resultingLufs)} with a true peak of ")
                    append("${LoudnessTargets.dbtpText(resultingTruePeakDbtp)}.")
                }
            }

        private fun lowerDetail(): String =
            buildString {
                append("Measured ${LoudnessTargets.lufsText(lufs)}, true peak ${LoudnessTargets.dbtpText(truePeakDbtp)}. ")
                when {
                    // Quiet, but already clipping: gain cannot help, only a limiter can.
                    lufs < target.window.lowLufs -> {
                        append("That is under the ${target.levelPhrase} ${target.label} plays at, but the peak is already ")
                        append("over the ${LoudnessTargets.dbtpText(target.window.ceilingDbtp)} ceiling, and gain alone can ")
                        append("only make that worse. Geode will take off ${LoudnessTargets.dbText(gainDb)} to get the ")
                        append("peak legal; getting louder as well needs a limiter on the mix.")
                    }
                    // Genuinely louder than the platform plays at.
                    lufs > target.window.highLufs -> {
                        append("That is ${LoudnessTargets.luText(gainForLevelDb)} over the ${target.levelPhrase} ")
                        append("${target.label} plays at — ${target.loudNote}. Geode will take off ")
                        append("${LoudnessTargets.dbText(gainDb)}")
                        if (ceilingLimited) {
                            append(" — a shade more than the level alone needs, so the true peak clears the ")
                            append("${LoudnessTargets.dbtpText(target.window.ceilingDbtp)} ceiling too")
                        }
                        append(", landing at ${LoudnessTargets.lufsText(resultingLufs)} with a true peak of ")
                        append("${LoudnessTargets.dbtpText(resultingTruePeakDbtp)}.")
                    }
                    // Level is fine; the ceiling is the only reason to touch it.
                    else -> {
                        append("The level is where ${target.label} wants it, but the peak is over the ")
                        append("${LoudnessTargets.dbtpText(target.window.ceilingDbtp)} ceiling, where lossy encoders start ")
                        append("to distort. Geode will take off ${LoudnessTargets.dbText(gainDb)}, landing at ")
                        append("${LoudnessTargets.lufsText(resultingLufs)}.")
                    }
                }
            }
    }
}

/** Turns a [LoudnessReport] into a gain and a sentence. */
object LoudnessTargets {
    /** A gain smaller than this is inaudible; treating it as "on target" saves a pointless re-export. */
    const val GAIN_TOLERANCE_DB: Double = 0.1

    /** A peak this far over the ceiling is rounding, not clipping. */
    private const val PEAK_TOLERANCE_DB = 0.05

    fun advise(
        report: LoudnessReport,
        target: LoudnessTarget,
    ): LoudnessAdvice {
        val lufs =
            when (val integrated = report.integrated) {
                is IntegratedLoudness.Lufs -> integrated.value
                IntegratedLoudness.BelowGate -> return LoudnessAdvice.NothingToMeasure
            }
        return when (target) {
            LoudnessTarget.LeaveAsIs -> LoudnessAdvice.AsMixed(lufs, report.truePeakDbtp)
            is LoudnessTarget.Normalising -> normalise(target, lufs, report.truePeakDbtp)
        }
    }

    /** The gain to apply on export, in dB. Zero means the file already ships as it is. */
    fun gainDb(
        report: LoudnessReport,
        target: LoudnessTarget,
    ): Double = advise(report, target).gainDb

    /** The same gain as a multiplier, for an audio stage that wants linear. */
    fun linearGain(
        report: LoudnessReport,
        target: LoudnessTarget,
    ): Float = advise(report, target).linearGain

    // Numbers are formatted against a fixed locale on purpose: they are a technical readout meant to
    // line up with the platform documentation the user will compare them against.
    fun lufsText(value: Double): String = String.format(Locale.US, "%.1f LUFS", value)

    fun dbtpText(value: Double): String = String.format(Locale.US, "%.1f dBTP", value)

    /** Signed, for a readout where the direction of the gain is the point. */
    fun gainText(db: Double): String = String.format(Locale.US, "%+.1f dB", db)

    /** Unsigned, for prose that already says "add" or "take off". */
    fun dbText(db: Double): String = String.format(Locale.US, "%.1f dB", abs(db))

    fun luText(lu: Double): String = String.format(Locale.US, "%.1f LU", abs(lu))

    @Suppress("ReturnCount")
    private fun normalise(
        target: LoudnessTarget.Normalising,
        lufs: Double,
        truePeakDbtp: Double,
    ): LoudnessAdvice {
        val window = target.window
        val gainForLevel = window.aimLufs - lufs
        // Never raise the peak past the ceiling, and pull it down even when the level is fine.
        val gainForCeiling = window.ceilingDbtp - truePeakDbtp
        val gain = minOf(gainForLevel, gainForCeiling)
        val inBand = lufs >= window.lowLufs && lufs <= window.highLufs
        val peakSafe = truePeakDbtp <= window.ceilingDbtp + PEAK_TOLERANCE_DB
        if (inBand && peakSafe) return LoudnessAdvice.OnTarget(target, lufs, truePeakDbtp)
        if (abs(gain) < GAIN_TOLERANCE_DB) return LoudnessAdvice.OnTarget(target, lufs, truePeakDbtp)
        return LoudnessAdvice.Normalise(
            target = target,
            lufs = lufs,
            truePeakDbtp = truePeakDbtp,
            gainDb = gain,
            gainForLevelDb = gainForLevel,
            // A flat gain shifts every gated block by the same amount, so it shifts the integrated
            // figure by exactly that much — the only wobble is a block sitting on the -70 LUFS gate.
            resultingLufs = lufs + gain,
            resultingTruePeakDbtp = truePeakDbtp + gain,
        )
    }
}
