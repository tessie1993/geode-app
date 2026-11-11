package dev.geode.ui.theme

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.widthIn
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import dev.geode.R

/** res/drawable/splash_wordmark.xml is 200x80dp: the system splash stretches it to that box. */
private const val WORDMARK_ASPECT = 200f / 80f

/** Under the boot intro's eye, about the old one-line wordmark's width plus its "3D". */
private val WORDMARK_MAX_WIDTH = 280.dp

/**
 * The BeatRealms 3D logo: "BeatRealms" in brush script, glassy cyan to violet, over a chrome
 * squared "3d". It is one vector, res/drawable/splash_wordmark.xml, outlined from two open fonts
 * by tools/wordmark.py, the same image the system splash shows as its branding image, so the
 * splash and the app draw one wordmark and no font ships. As a picture it ignores the font scale;
 * TalkBack reads the app's name. It takes the width it is given, up to [maxWidth], and keeps its
 * shape; the lockup sits centred with a margin inside its box, as the splash needs.
 */
@Composable
fun Wordmark(
    modifier: Modifier = Modifier,
    maxWidth: Dp = WORDMARK_MAX_WIDTH,
) {
    Image(
        painterResource(R.drawable.splash_wordmark),
        contentDescription = stringResource(R.string.app_name),
        modifier = modifier.widthIn(max = maxWidth).fillMaxWidth().aspectRatio(WORDMARK_ASPECT),
    )
}
