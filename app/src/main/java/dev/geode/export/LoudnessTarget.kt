package dev.geode.export

import android.content.res.Resources
import androidx.annotation.StringRes
import dev.geode.R
import dev.geode.engine.bridge.GeodeNative
import java.util.Locale
import kotlin.math.abs

/**
 * Where the export's loudness should end up. Three choices, deliberately — a numeric LUFS field
 * would ask the user to know something the platforms already decided for them.
 *
 * Each platform's window (the level it plays at, the band that counts as already right, its
 * true-peak ceiling) is native (`core/audio/dsp/LoudnessTarget`), keyed by [nativeId]; this side
 * only names and describes the choice.
 */
sealed interface LoudnessTarget {
    val id: String

    /** GeodeNative.LOUDNESS_TARGET_*. */
    val nativeId: Int

    @get:StringRes
    val labelRes: Int
    val summary: String

    /** A target that can actually move the level, and the words its advice needs. */
    sealed interface Normalising : LoudnessTarget {
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
        override val nativeId: Int = GeodeNative.LOUDNESS_TARGET_YOUTUBE
        override val labelRes: Int = R.string.export_preset_youtube
        override val summary: String = "About -14 LUFS with 1 dB of true-peak headroom."
        override val levelPhraseRes: Int = R.string.export_loudness_youtube_level
        override val quietNoteRes: Int = R.string.export_loudness_youtube_quiet
        override val loudNoteRes: Int = R.string.export_loudness_youtube_loud
    }

    data object ShortsAndTikTok : Normalising {
        override val id: String = "shorts"
        override val nativeId: Int = GeodeNative.LOUDNESS_TARGET_SHORTS
        override val labelRes: Int = R.string.export_loudness_shorts
        override val summary: String = "About -10 to -12 LUFS — phone-speaker loud, the way short-form feeds sit."
        override val levelPhraseRes: Int = R.string.export_loudness_shorts_level
        override val quietNoteRes: Int = R.string.export_loudness_shorts_quiet
        override val loudNoteRes: Int = R.string.export_loudness_shorts_loud
    }

    data object LeaveAsIs : LoudnessTarget {
        override val id: String = "as-is"
        override val nativeId: Int = GeodeNative.LOUDNESS_TARGET_AS_IS
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

/**
 * What to do about a measured file, and how to say it to someone who does not know what a LUFS is.
 *
 * Which advice applies, the gain and every number quoted are native
 * (`core/audio/dsp/LoudnessTarget`, pinned by `core/tests/loudness_test`): [kind] is a
 * GeodeNative.ADVICE_* kind and [numbers] its ADVICE_* figures. This class only picks the
 * sentences. [gainDb] is always the gain to apply on export; [headline] fits a row or a button,
 * [detail] is the paragraph underneath.
 */
class LoudnessAdvice internal constructor(
    private val kind: Int,
    private val target: LoudnessTarget,
    private val numbers: DoubleArray,
) {
    val gainDb: Double get() = numbers[GeodeNative.ADVICE_GAIN_DB]

    private val lufs: String get() = LoudnessTargets.lufsText(numbers[GeodeNative.ADVICE_LUFS])
    private val peak: String get() = LoudnessTargets.dbtpText(numbers[GeodeNative.ADVICE_TRUE_PEAK_DBTP])
    private val ceiling: String get() = LoudnessTargets.dbtpText(numbers[GeodeNative.ADVICE_CEILING_DBTP])
    private val gain: String get() = LoudnessTargets.dbText(gainDb)
    private val levelGain: String get() = LoudnessTargets.luText(numbers[GeodeNative.ADVICE_GAIN_FOR_LEVEL_DB])
    private val resultingLufs: String get() = LoudnessTargets.lufsText(numbers[GeodeNative.ADVICE_RESULTING_LUFS])
    private val resultingPeak: String get() = LoudnessTargets.dbtpText(numbers[GeodeNative.ADVICE_RESULTING_TRUE_PEAK_DBTP])

    /** Only the kinds past as-mixed carry a normalising target. */
    private val platform: LoudnessTarget.Normalising get() = target as LoudnessTarget.Normalising

    fun headline(res: Resources): String =
        when (kind) {
            GeodeNative.ADVICE_NOTHING_TO_MEASURE -> res.getString(R.string.export_loudness_none_headline)
            GeodeNative.ADVICE_AS_MIXED -> res.getString(R.string.export_loudness_as_mixed_headline)
            GeodeNative.ADVICE_RAISE, GeodeNative.ADVICE_RAISE_LIMITED ->
                res.getString(R.string.export_loudness_up_headline, gain, res.getString(target.labelRes))
            GeodeNative.ADVICE_ON_TARGET, GeodeNative.ADVICE_UNCHANGED ->
                res.getString(R.string.export_loudness_on_target_headline, res.getString(target.labelRes))
            else -> res.getString(R.string.export_loudness_down_headline, gain, res.getString(target.labelRes))
        }

    fun detail(res: Resources): String =
        when (kind) {
            GeodeNative.ADVICE_NOTHING_TO_MEASURE -> res.getString(R.string.export_loudness_none_detail)
            GeodeNative.ADVICE_AS_MIXED -> asMixedDetail(res)
            GeodeNative.ADVICE_ON_TARGET ->
                res.getString(
                    R.string.export_loudness_on_target_detail,
                    lufs,
                    res.getString(platform.levelPhraseRes),
                    res.getString(target.labelRes),
                    peak,
                    ceiling,
                )
            GeodeNative.ADVICE_UNCHANGED -> res.getString(R.string.export_loudness_unchanged_detail, lufs)
            GeodeNative.ADVICE_RAISE, GeodeNative.ADVICE_RAISE_LIMITED -> raiseDetail(res)
            else -> lowerDetail(res)
        }

    private fun asMixedDetail(res: Resources): String =
        buildString {
            append(res.getString(R.string.export_loudness_as_mixed_detail, lufs, peak))
            val underYouTube = numbers[GeodeNative.ADVICE_QUIET_UNDER_YOUTUBE_LU]
            if (underYouTube > 0.0) {
                append(' ')
                append(
                    res.getString(
                        R.string.export_loudness_as_mixed_quiet,
                        res.getString(LoudnessTarget.YouTube.quietNoteRes),
                        LoudnessTargets.luText(underYouTube),
                    ),
                )
            }
            if (numbers[GeodeNative.ADVICE_PEAK_WARNING] != 0.0) {
                append(' ')
                append(res.getString(R.string.export_loudness_peak_warning))
            }
        }

    private fun raiseDetail(res: Resources): String {
        val quietNote = res.getString(platform.quietNoteRes).replaceFirstChar { it.uppercase() }
        val level = res.getString(platform.levelPhraseRes)
        val label = res.getString(target.labelRes)
        return if (kind == GeodeNative.ADVICE_RAISE_LIMITED) {
            res.getString(
                R.string.export_loudness_raise_limited_detail,
                lufs,
                levelGain,
                level,
                label,
                quietNote,
                ceiling,
                gain,
                resultingLufs,
            )
        } else {
            res.getString(
                R.string.export_loudness_raise_detail,
                lufs,
                levelGain,
                level,
                label,
                quietNote,
                gain,
                resultingLufs,
                resultingPeak,
            )
        }
    }

    private fun lowerDetail(res: Resources): String {
        val label = res.getString(target.labelRes)
        val level = res.getString(platform.levelPhraseRes)
        val loudNote = res.getString(platform.loudNoteRes)
        return when (kind) {
            GeodeNative.ADVICE_LOWER_CLIPPING ->
                res.getString(R.string.export_loudness_lower_clipping_detail, lufs, peak, level, label, ceiling, gain)
            GeodeNative.ADVICE_LOWER_LOUD_LIMITED ->
                res.getString(
                    R.string.export_loudness_lower_loud_limited_detail,
                    lufs,
                    peak,
                    levelGain,
                    level,
                    label,
                    loudNote,
                    gain,
                    ceiling,
                    resultingLufs,
                    resultingPeak,
                )
            GeodeNative.ADVICE_LOWER_LOUD ->
                res.getString(
                    R.string.export_loudness_lower_loud_detail,
                    lufs,
                    peak,
                    levelGain,
                    level,
                    label,
                    loudNote,
                    gain,
                    resultingLufs,
                    resultingPeak,
                )
            else -> res.getString(R.string.export_loudness_lower_peak_detail, lufs, peak, label, ceiling, gain, resultingLufs)
        }
    }
}

/** Turns a [LoudnessReport] into advice through the native decision, and formats its numbers. */
object LoudnessTargets {
    fun advise(
        report: LoudnessReport,
        target: LoudnessTarget,
    ): LoudnessAdvice {
        val numbers = DoubleArray(GeodeNative.ADVICE_DOUBLES)
        val integrated = report.integrated as? IntegratedLoudness.Lufs
        val kind = GeodeNative.loudnessAdvise(integrated != null, integrated?.value ?: 0.0, report.truePeakDbtp, target.nativeId, numbers)
        return LoudnessAdvice(kind, target, numbers)
    }

    // Numbers are formatted against a fixed locale on purpose: they are a technical readout meant to
    // line up with the platform documentation the user will compare them against.
    fun lufsText(value: Double): String = String.format(Locale.US, "%.1f LUFS", value)

    fun dbtpText(value: Double): String = String.format(Locale.US, "%.1f dBTP", value)

    /** Unsigned, for prose that already says "add" or "take off". */
    fun dbText(db: Double): String = String.format(Locale.US, "%.1f dB", abs(db))

    fun luText(lu: Double): String = String.format(Locale.US, "%.1f LU", abs(lu))
}
