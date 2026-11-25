package dev.geode.ui.pro

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp

/**
 * A small notice where a free limit was reached without a tap (Auto mode's free minute), instead of
 * the limit passing silently. Not a paywall: it covers nothing, fades on its own, and only a tap on it
 * opens the paywall ([onClick]). Polite to TalkBack, which reads it once as it appears.
 */
@Composable
fun ProNoticeChip(
    visible: Boolean,
    text: String,
    onClick: () -> Unit,
) {
    Box(Modifier.fillMaxSize().statusBarsPadding().padding(top = 72.dp), contentAlignment = Alignment.TopCenter) {
        AnimatedVisibility(visible = visible, enter = fadeIn(), exit = fadeOut()) {
            Text(
                text,
                style = MaterialTheme.typography.labelLarge,
                color = Color.White,
                modifier =
                    Modifier
                        .heightIn(min = 48.dp)
                        .background(ProColors.background.copy(alpha = 0.85f), RoundedCornerShape(24.dp))
                        .border(1.dp, ProColors.accent, RoundedCornerShape(24.dp))
                        .clickable(role = Role.Button, onClick = onClick)
                        .semantics { liveRegion = LiveRegionMode.Polite }
                        .padding(horizontal = 18.dp, vertical = 14.dp),
            )
        }
    }
}
