package dev.geode.ui

import android.content.res.Resources
import androidx.annotation.StringRes
import dev.geode.R
import dev.geode.model.LoudnessAdvice
import dev.geode.model.LoudnessTarget
import java.util.Locale
import kotlin.math.abs

// The wording of a loudness advice. Which sentence, and every number in it, is native
// (core/output/LoudnessAdvice `Sentence`); the numbers below are that enum's.
private const val SAY_AS_MIXED = 1
private const val SAY_ON_TARGET = 2
private const val SAY_UNCHANGED = 3
private const val SAY_RAISE = 4
private const val SAY_RAISE_LIMITED = 5
private const val SAY_LOWER_CLIPPING = 6
private const val SAY_LOWER_LOUD_LIMITED = 7
private const val SAY_LOWER_LOUD = 8
private const val SAY_LOWER_PEAK = 9

@get:StringRes
internal val LoudnessTarget.labelRes: Int
    get() =
        when (this) {
            LoudnessTarget.YouTube -> R.string.export_preset_youtube
            LoudnessTarget.ShortsAndTikTok -> R.string.export_loudness_shorts
            LoudnessTarget.LeaveAsIs -> R.string.export_loudness_as_is
        }

/** How the target reads in prose, e.g. "about -14 LUFS"; Leave as-is never reaches a sentence that reads it. */
@StringRes
private fun LoudnessTarget.levelPhraseRes(): Int =
    if (this == LoudnessTarget.ShortsAndTikTok) R.string.export_loudness_shorts_level else R.string.export_loudness_youtube_level

/** What the platform does to a file that arrives under the target. */
@StringRes
private fun LoudnessTarget.quietNoteRes(): Int =
    if (this == LoudnessTarget.ShortsAndTikTok) R.string.export_loudness_shorts_quiet else R.string.export_loudness_youtube_quiet

/** What the platform does to a file that arrives over it. */
@StringRes
private fun LoudnessTarget.loudNoteRes(): Int =
    if (this == LoudnessTarget.ShortsAndTikTok) R.string.export_loudness_shorts_loud else R.string.export_loudness_youtube_loud

// Numbers use a fixed locale on purpose: a technical readout meant to line up with the platform documentation.
private fun lufsText(value: Double): String = String.format(Locale.US, "%.1f LUFS", value)

private fun dbtpText(value: Double): String = String.format(Locale.US, "%.1f dBTP", value)

/** Unsigned, for prose that already says "add" or "take off". */
private fun dbText(db: Double): String = String.format(Locale.US, "%.1f dB", abs(db))

private fun luText(lu: Double): String = String.format(Locale.US, "%.1f LU", abs(lu))

/** The headline and the paragraph under it, for the finished-render row. */
internal fun LoudnessAdvice.text(res: Resources): String = headline(res) + " " + detail(res)

private fun LoudnessAdvice.headline(res: Resources): String {
    val label = res.getString(target.labelRes)
    return when (sentence) {
        SAY_AS_MIXED -> res.getString(R.string.export_loudness_as_mixed_headline)
        SAY_ON_TARGET, SAY_UNCHANGED -> res.getString(R.string.export_loudness_on_target_headline, label)
        SAY_RAISE, SAY_RAISE_LIMITED -> res.getString(R.string.export_loudness_up_headline, dbText(gainDb), label)
        SAY_LOWER_CLIPPING, SAY_LOWER_LOUD_LIMITED, SAY_LOWER_LOUD, SAY_LOWER_PEAK ->
            res.getString(R.string.export_loudness_down_headline, dbText(gainDb), label)
        else -> res.getString(R.string.export_loudness_none_headline)
    }
}

@Suppress("LongMethod", "CyclomaticComplexMethod")
private fun LoudnessAdvice.detail(res: Resources): String {
    val measured = lufsText(lufs)
    val peak = dbtpText(truePeakDbtp)
    val ceiling = dbtpText(ceilingDbtp)
    val label = res.getString(target.labelRes)
    val level = res.getString(target.levelPhraseRes())
    val quietNote = res.getString(target.quietNoteRes()).replaceFirstChar { it.uppercase() }
    return when (sentence) {
        SAY_AS_MIXED -> asMixedDetail(res)
        SAY_ON_TARGET -> res.getString(R.string.export_loudness_on_target_detail, measured, level, label, peak, ceiling)
        SAY_UNCHANGED -> res.getString(R.string.export_loudness_unchanged_detail, measured)
        SAY_RAISE ->
            res.getString(
                R.string.export_loudness_raise_detail,
                measured,
                luText(gainForLevelDb),
                level,
                label,
                quietNote,
                dbText(gainDb),
                lufsText(resultingLufs),
                dbtpText(resultingTruePeakDbtp),
            )
        SAY_RAISE_LIMITED ->
            res.getString(
                R.string.export_loudness_raise_limited_detail,
                measured,
                luText(gainForLevelDb),
                level,
                label,
                quietNote,
                ceiling,
                dbText(gainDb),
                lufsText(resultingLufs),
            )
        SAY_LOWER_CLIPPING ->
            res.getString(R.string.export_loudness_lower_clipping_detail, measured, peak, level, label, ceiling, dbText(gainDb))
        SAY_LOWER_LOUD_LIMITED ->
            res.getString(
                R.string.export_loudness_lower_loud_limited_detail,
                measured,
                peak,
                luText(gainForLevelDb),
                level,
                label,
                res.getString(target.loudNoteRes()),
                dbText(gainDb),
                ceiling,
                lufsText(resultingLufs),
                dbtpText(resultingTruePeakDbtp),
            )
        SAY_LOWER_LOUD ->
            res.getString(
                R.string.export_loudness_lower_loud_detail,
                measured,
                peak,
                luText(gainForLevelDb),
                level,
                label,
                res.getString(target.loudNoteRes()),
                dbText(gainDb),
                lufsText(resultingLufs),
                dbtpText(resultingTruePeakDbtp),
            )
        SAY_LOWER_PEAK ->
            res.getString(
                R.string.export_loudness_lower_peak_detail,
                measured,
                peak,
                label,
                ceiling,
                dbText(gainDb),
                lufsText(resultingLufs),
            )
        else -> res.getString(R.string.export_loudness_none_detail)
    }
}

private fun LoudnessAdvice.asMixedDetail(res: Resources): String =
    buildString {
        append(res.getString(R.string.export_loudness_as_mixed_detail, lufsText(lufs), dbtpText(truePeakDbtp)))
        if (quietLu > 0.0) {
            append(' ')
            append(
                res.getString(
                    R.string.export_loudness_as_mixed_quiet,
                    res.getString(LoudnessTarget.YouTube.quietNoteRes()),
                    luText(quietLu),
                ),
            )
        }
        if (peakWarning) {
            append(' ')
            append(res.getString(R.string.export_loudness_peak_warning))
        }
    }
