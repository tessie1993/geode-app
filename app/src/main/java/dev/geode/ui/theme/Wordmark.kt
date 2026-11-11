package dev.geode.ui.theme

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import dev.geode.R

/** Below this share of its asked size, one line gives way to the narrower stacked lockup. */
private const val ONE_LINE_MIN_FIT = 0.75f

/** "3D" in the stacked lockup, against the name's size. */
private const val STACKED_3D_SIZE = 0.62f

/**
 * The BeatRealms 3D logo as live text in the theme's sans: "Beat" bold, "Realms" light, so the
 * weight change marks the word break with no space, and "3D" bold in the accent. Sized by [style]
 * (font scale and the app's text size carry through) and shrunk to the width it is given, never
 * clipped or wrapped. With no [lockup] it is one line where that fits, stacked where it does not.
 * res/drawable/splash_wordmark.xml is the same one-line lockup outlined, for the system splash.
 */
@Composable
fun Wordmark(
    modifier: Modifier = Modifier,
    lockup: WordmarkLockup? = null,
    style: TextStyle = MaterialTheme.typography.displaySmall,
    color: Color = MaterialTheme.colorScheme.onBackground,
    accent: Color = MaterialTheme.colorScheme.primary,
) {
    val name = stringResource(R.string.app_name)
    val beat = stringResource(R.string.wordmark_beat)
    val realms = stringResource(R.string.wordmark_realms)
    val threeD = stringResource(R.string.wordmark_3d)
    val base = style.copy(color = color, letterSpacing = 0.em, lineHeight = 1.2.em)
    val accentSpan = SpanStyle(color = accent, fontWeight = FontWeight.Bold)

    fun line(withThreeD: Boolean) =
        buildAnnotatedString {
            withStyle(SpanStyle(fontWeight = FontWeight.Bold)) { append(beat) }
            withStyle(SpanStyle(fontWeight = FontWeight.Light)) { append(realms) }
            if (withThreeD) {
                append(' ')
                withStyle(accentSpan.copy(letterSpacing = 0.02.em)) { append(threeD) }
            }
        }

    val measurer = rememberTextMeasurer()
    BoxWithConstraints(
        modifier.clearAndSetSemantics { contentDescription = name },
        contentAlignment = Alignment.Center,
    ) {
        fun fit(withThreeD: Boolean): Float {
            if (!constraints.hasBoundedWidth) return 1f
            val natural = measurer.measure(line(withThreeD), base, softWrap = false, maxLines = 1).size.width
            return (constraints.maxWidth / (natural + 1f)).coerceAtMost(1f)
        }

        val stacked =
            lockup == WordmarkLockup.Stacked || (lockup == null && fit(withThreeD = true) < ONE_LINE_MIN_FIT)
        val fitted = base.copy(fontSize = base.fontSize * fit(withThreeD = !stacked))
        Column(Modifier.width(IntrinsicSize.Max), horizontalAlignment = Alignment.CenterHorizontally) {
            Text(line(withThreeD = !stacked), style = fitted, softWrap = false, maxLines = 1)
            if (stacked) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.M),
                ) {
                    val rule = Modifier.weight(1f).height(1.dp).background(accent.copy(alpha = 0.55f))
                    Box(rule)
                    val small = fitted.copy(fontSize = fitted.fontSize * STACKED_3D_SIZE, letterSpacing = 0.08.em)
                    Text(threeD, style = small.copy(color = accent, fontWeight = FontWeight.Bold), maxLines = 1)
                    Box(rule)
                }
            }
        }
    }
}
