package dev.geode.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.ui.glass.GlassButton
import dev.geode.ui.glass.GlassPalette
import dev.geode.ui.glass.GlassShapes
import dev.geode.ui.glass.GlassToggle
import dev.geode.ui.glass.floatOnWater
import dev.geode.ui.glass.glassSurface

/**
 * The walkthrough, over the live app.
 *
 * Everything behind the card keeps rendering and the scrim is deliberately light, because the
 * whole value of touring the real app is lost if the real app cannot be seen. The card sits at
 * the bottom, clear of the content each step is talking about.
 *
 * Skipping and finishing are the same outcome — the tour is done — which is why there is one
 * [onDismiss] and not two. What differs is [dontShowAgain], and that is the person's call in
 * either case.
 */
@Composable
fun TutorialOverlay(
    steps: List<TutorialStep>,
    dontShowAgain: Boolean,
    onDontShowAgainChange: (Boolean) -> Unit,
    onNavigate: (GeodeDestination) -> Unit,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
) {
    if (steps.isEmpty()) return
    var index by rememberSaveable { mutableIntStateOf(0) }
    val step = steps[index.coerceIn(0, steps.lastIndex)]

    // Navigation follows the step rather than the button, so Back walks the app backwards too.
    LaunchedEffect(step) { onNavigate(step.destination) }

    Box(
        modifier
            .fillMaxSize()
            // Consumes taps so a tour step cannot be dismissed by prodding the app underneath it,
            // and so a stray tap does not start playback behind the card.
            .clickable(enabled = true, onClick = {})
            .background(GlassPalette.base.copy(alpha = 0.35f)),
    ) {
        Column(
            Modifier
                .align(Alignment.BottomCenter)
                .navigationBarsPadding()
                .padding(16.dp)
                .fillMaxWidth()
                .glassSurface(shape = GlassShapes.tile)
                .floatOnWater(strength = 0.2f)
                .padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(
                stringResource(R.string.tutorial_progress, index + 1, steps.size),
                style = MaterialTheme.typography.labelMedium,
                color = GlassPalette.textSecondary,
            )
            Text(
                stringResource(step.titleRes),
                style = MaterialTheme.typography.titleMedium,
                color = GlassPalette.textPrimary,
                modifier = Modifier.semantics { heading() },
            )
            Text(
                stringResource(step.bodyRes),
                style = MaterialTheme.typography.bodySmall,
                color = GlassPalette.textSecondary,
            )

            // Offered on every step, not just the last: someone who is skipping on step one is
            // exactly the person most likely to mean "and don't ask me again".
            Row(verticalAlignment = Alignment.CenterVertically) {
                GlassToggle(checked = dontShowAgain, onCheckedChange = onDontShowAgainChange)
                Spacer(Modifier.width(10.dp))
                Text(
                    stringResource(R.string.tutorial_dont_show),
                    style = MaterialTheme.typography.bodySmall,
                    color = GlassPalette.textSecondary,
                )
            }

            Spacer(Modifier.height(2.dp))
            TutorialActions(
                atFirst = index == 0,
                atLast = index == steps.lastIndex,
                onBack = { if (index > 0) index-- },
                onNext = { if (index < steps.lastIndex) index++ else onDismiss() },
                onSkip = onDismiss,
            )
        }
    }
}

@Composable
private fun TutorialActions(
    atFirst: Boolean,
    atLast: Boolean,
    onBack: () -> Unit,
    onNext: () -> Unit,
    onSkip: () -> Unit,
) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        // Skip stays put at the left on every step, including the last, so it never becomes a
        // moving target and nobody has to hunt for the way out.
        GlassButton(text = stringResource(R.string.tutorial_skip), onClick = onSkip)
        Spacer(Modifier.weight(1f))
        if (!atFirst) {
            GlassButton(text = stringResource(R.string.tutorial_back), onClick = onBack)
            Spacer(Modifier.width(8.dp))
        }
        GlassButton(
            text = stringResource(if (atLast) R.string.tutorial_finish else R.string.tutorial_next),
            onClick = onNext,
        )
    }
}
