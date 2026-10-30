package dev.geode.export

import android.content.res.Resources
import androidx.annotation.StringRes
import dev.geode.R
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

    @get:StringRes
    val labelRes: Int
    val summary: String

    /**
     * A target that can actually move the level. Only these carry a [window], so there is no
     * "what is the target loudness of Leave as-is" state to get wrong.
     */
    sealed interface Normalising : LoudnessTarget {
        val window: LoudnessWindow

        /** How the target reads in prose, e.g. "about -14 LUFS". */
        @get:StringRes
        val levelPhraseRes: Int

        /** What the platform does to a file that arrives under the target. */
        @get:StringRes
        val quietNoteRes: Int

        /** What the platform does to a file that arrives over it. */
        @get:StringRes
        val loudNoteRes: Int
    }

    data object YouTube : Normalising {
        override val id: String = "youtube"
        override val labelRes: Int = R.string.export_preset_youtube
        override val summary: String = "About -14 LUFS with 1 dB of true-peak headroom."
        override val window: LoudnessWindow =
            LoudnessWindow(aimLufs = -14.0, lowLufs = -15.0, highLufs = -13.0, ceilingDbtp = -1.0)
        override val levelPhraseRes: Int = R.string.export_loudness_youtube_level
        override val quietNoteRes: Int = R.string.export_loudness_youtube_quiet
        override val loudNoteRes: Int = R.string.export_loudness_youtube_loud
    }

    data object ShortsAndTikTok : Normalising {
        override val id: String = "shorts"
        override val labelRes: Int = R.string.export_loudness_shorts
        override val summary: String = "About -10 to -12 LUFS — phone-speaker loud, the way short-form feeds sit."
        override val window: LoudnessWindow =
            LoudnessWindow(aimLufs = -11.0, lowLufs = -12.0, highLufs = -10.0, ceilingDbtp = -1.0)
        override val levelPhraseRes: Int = R.string.export_loudness_shorts_level
        override val quietNoteRes: Int = R.string.export_loudness_shorts_quiet
        override val loudNoteRes: Int = R.string.export_loudness_shorts_loud
    }

    data object LeaveAsIs : LoudnessTarget {
        override val id: String = "as-is"
        override val labelRes: Int = R.string.export_loudness_as_is
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
 * Both read their wording from the given [Resources]; the numbers in it come from [LoudnessTargets].
 */
sealed interface LoudnessAdvice {
    val gainDb: Double

    fun headline(res: Resources): String

    fun detail(res: Resources): String

    /** [gainDb] as a multiplier, ready for a gain stage. */
    val linearGain: Float
        get() = 10.0.pow(gainDb / 20.0).toFloat()

    data object NothingToMeasure : LoudnessAdvice {
        override val gainDb: Double = 0.0

        override fun headline(res: Resources): String = res.getString(R.string.export_loudness_none_headline)

        override fun detail(res: Resources): String = res.getString(R.string.export_loudness_none_detail)
    }

    data class AsMixed(
        val lufs: Double,
        val truePeakDbtp: Double,
    ) : LoudnessAdvice {
        override val gainDb: Double = 0.0

        override fun headline(res: Resources): String = res.getString(R.string.export_loudness_as_mixed_headline)

        override fun detail(res: Resources): String =
            buildString {
                append(
                    res.getString(
                        R.string.export_loudness_as_mixed_detail,
                        LoudnessTargets.lufsText(lufs),
                        LoudnessTargets.dbtpText(truePeakDbtp),
                    ),
                )
                val underYouTube = LoudnessTarget.YouTube.window.aimLufs - lufs
                if (underYouTube > QUIET_HINT_LU) {
                    append(' ')
                    append(
                        res.getString(
                            R.string.export_loudness_as_mixed_quiet,
                            res.getString(LoudnessTarget.YouTube.quietNoteRes),
                            LoudnessTargets.luText(underYouTube),
                        ),
                    )
                }
                if (truePeakDbtp > YOUTUBE_CEILING_DBTP) {
                    append(' ')
                    append(res.getString(R.string.export_loudness_peak_warning))
                }
            }
    }

    data class OnTarget(
        val target: LoudnessTarget.Normalising,
        val lufs: Double,
        val truePeakDbtp: Double,
    ) : LoudnessAdvice {
        override val gainDb: Double = 0.0

        override fun headline(res: Resources): String =
            res.getString(R.string.export_loudness_on_target_headline, res.getString(target.labelRes))

        override fun detail(res: Resources): String =
            res.getString(
                R.string.export_loudness_on_target_detail,
                LoudnessTargets.lufsText(lufs),
                res.getString(target.levelPhraseRes),
                res.getString(target.labelRes),
                LoudnessTargets.dbtpText(truePeakDbtp),
                LoudnessTargets.dbtpText(target.window.ceilingDbtp),
            )
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

        override fun headline(res: Resources): String {
            val label = res.getString(target.labelRes)
            return when (direction) {
                GainDirection.UP -> res.getString(R.string.export_loudness_up_headline, LoudnessTargets.dbText(gainDb), label)
                GainDirection.DOWN -> res.getString(R.string.export_loudness_down_headline, LoudnessTargets.dbText(gainDb), label)
                GainDirection.UNCHANGED -> res.getString(R.string.export_loudness_on_target_headline, label)
            }
        }

        override fun detail(res: Resources): String =
            when (direction) {
                GainDirection.UP -> raiseDetail(res)
                GainDirection.DOWN -> lowerDetail(res)
                GainDirection.UNCHANGED -> res.getString(R.string.export_loudness_unchanged_detail, LoudnessTargets.lufsText(lufs))
            }

        private fun raiseDetail(res: Resources): String =
            if (ceilingLimited) {
                res.getString(
                    R.string.export_loudness_raise_limited_detail,
                    LoudnessTargets.lufsText(lufs),
                    LoudnessTargets.luText(gainForLevelDb),
                    res.getString(target.levelPhraseRes),
                    res.getString(target.labelRes),
                    res.getString(target.quietNoteRes).replaceFirstChar { it.uppercase() },
                    LoudnessTargets.dbtpText(target.window.ceilingDbtp),
                    LoudnessTargets.dbText(gainDb),
                    LoudnessTargets.lufsText(resultingLufs),
                )
            } else {
                res.getString(
                    R.string.export_loudness_raise_detail,
                    LoudnessTargets.lufsText(lufs),
                    LoudnessTargets.luText(gainForLevelDb),
                    res.getString(target.levelPhraseRes),
                    res.getString(target.labelRes),
                    res.getString(target.quietNoteRes).replaceFirstChar { it.uppercase() },
                    LoudnessTargets.dbText(gainDb),
                    LoudnessTargets.lufsText(resultingLufs),
                    LoudnessTargets.dbtpText(resultingTruePeakDbtp),
                )
            }

        private fun lowerDetail(res: Resources): String {
            val measured = LoudnessTargets.lufsText(lufs)
            val peak = LoudnessTargets.dbtpText(truePeakDbtp)
            val ceiling = LoudnessTargets.dbtpText(target.window.ceilingDbtp)
            val label = res.getString(target.labelRes)
            val level = res.getString(target.levelPhraseRes)
            return when {
                // Quiet, but already clipping: gain cannot help, only a limiter can.
                lufs < target.window.lowLufs ->
                    res.getString(
                        R.string.export_loudness_lower_clipping_detail,
                        measured,
                        peak,
                        level,
                        label,
                        ceiling,
                        LoudnessTargets.dbText(gainDb),
                    )
                // Genuinely louder than the platform plays at.
                lufs > target.window.highLufs && ceilingLimited ->
                    res.getString(
                        R.string.export_loudness_lower_loud_limited_detail,
                        measured,
                        peak,
                        LoudnessTargets.luText(gainForLevelDb),
                        level,
                        label,
                        res.getString(target.loudNoteRes),
                        LoudnessTargets.dbText(gainDb),
                        ceiling,
                        LoudnessTargets.lufsText(resultingLufs),
                        LoudnessTargets.dbtpText(resultingTruePeakDbtp),
                    )
                lufs > target.window.highLufs ->
                    res.getString(
                        R.string.export_loudness_lower_loud_detail,
                        measured,
                        peak,
                        LoudnessTargets.luText(gainForLevelDb),
                        level,
                        label,
                        res.getString(target.loudNoteRes),
                        LoudnessTargets.dbText(gainDb),
                        LoudnessTargets.lufsText(resultingLufs),
                        LoudnessTargets.dbtpText(resultingTruePeakDbtp),
                    )
                // Level is fine; the ceiling is the only reason to touch it.
                else ->
                    res.getString(
                        R.string.export_loudness_lower_peak_detail,
                        measured,
                        peak,
                        label,
                        ceiling,
                        LoudnessTargets.dbText(gainDb),
                        LoudnessTargets.lufsText(resultingLufs),
                    )
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
