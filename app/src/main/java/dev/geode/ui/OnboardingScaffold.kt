package dev.geode.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.ui.theme.GeodeSpacing

/**
 * The frame every first-run screen shares: a step indicator, an icon badge, a heading and body,
 * then the actions pinned under them. Content is capped at a readable width so it sits centred on
 * tablets and in landscape, and the column scrolls when a large font pushes it past the screen.
 */
@Composable
internal fun OnboardingScaffold(
    step: Int,
    stepCount: Int,
    icon: ImageVector,
    title: String,
    body: String,
    modifier: Modifier = Modifier,
    extra: @Composable ColumnScope.() -> Unit = {},
    actions: @Composable ColumnScope.() -> Unit,
) {
    Box(
        modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .safeDrawingPadding(),
        contentAlignment = Alignment.Center,
    ) {
        Column(
            Modifier
                .widthIn(max = 480.dp)
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = GeodeSpacing.XL, vertical = GeodeSpacing.XXL),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            StepDots(step, stepCount)
            Spacer(Modifier.height(GeodeSpacing.XXL))
            Box(
                Modifier
                    .size(72.dp)
                    .clip(CircleShape)
                    .background(MaterialTheme.colorScheme.primaryContainer),
                contentAlignment = Alignment.Center,
            ) {
                Icon(icon, null, Modifier.size(36.dp), tint = MaterialTheme.colorScheme.onPrimaryContainer)
            }
            Spacer(Modifier.height(GeodeSpacing.XL))
            Text(
                title,
                style = MaterialTheme.typography.headlineSmall,
                color = MaterialTheme.colorScheme.onBackground,
                textAlign = TextAlign.Center,
                modifier = Modifier.semantics { heading() },
            )
            Spacer(Modifier.height(GeodeSpacing.M))
            Text(
                body,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
            )
            extra()
            Spacer(Modifier.height(GeodeSpacing.XXL))
            Column(
                Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(GeodeSpacing.S + GeodeSpacing.XXS),
                content = actions,
            )
        }
    }
}

@Composable
private fun StepDots(
    step: Int,
    count: Int,
) {
    val description = stringResource(R.string.onboarding_step, step + 1, count)
    Row(
        Modifier.semantics { contentDescription = description },
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        repeat(count) { i ->
            val active = i == step
            Box(
                Modifier
                    .size(width = if (active) 20.dp else 6.dp, height = 6.dp)
                    .clip(CircleShape)
                    .background(
                        if (active) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outline,
                    ),
            )
        }
    }
}

/** The number of first-run screens the step indicator counts: the notice, music access, folders. */
internal const val ONBOARDING_STEPS = 3
