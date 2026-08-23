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
import androidx.compose.material3.Checkbox
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import dev.geode.R

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
            .background(Color.Black.copy(alpha = 0.45f)),
    ) {
        Column(
            Modifier
                .align(Alignment.BottomCenter)
                .navigationBarsPadding()
                .padding(16.dp)
                .fillMaxWidth()
                .crystalPanel(
                    0.92f,
                    MaterialTheme.colorScheme.surface,
                    MaterialTheme.colorScheme.primary,
                    corner = 20.dp,
                    glowStrength = 0.7f,
                ).padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            CrystalOverline(stringResource(R.string.tutorial_progress, index + 1, steps.size))
            Text(
                stringResource(step.titleRes),
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.semantics { heading() },
            )
            Text(
                stringResource(step.bodyRes),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            // Offered on every step, not just the last: someone who is skipping on step one is
            // exactly the person most likely to mean "and don't ask me again".
            Row(verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = dontShowAgain, onCheckedChange = onDontShowAgainChange)
                Text(
                    stringResource(R.string.tutorial_dont_show),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
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
        CrystalButton(onClick = onSkip) { Text(stringResource(R.string.tutorial_skip)) }
        Spacer(Modifier.weight(1f))
        if (!atFirst) {
            CrystalButton(onClick = onBack) { Text(stringResource(R.string.tutorial_back)) }
            Spacer(Modifier.width(8.dp))
        }
        CrystalButton(onClick = onNext) {
            Text(stringResource(if (atLast) R.string.tutorial_finish else R.string.tutorial_next))
        }
    }
}
