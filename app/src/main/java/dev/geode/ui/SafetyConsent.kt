package dev.geode.ui

import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.FlashOn
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.ui.theme.GeodeSpacing

/**
 * The first-run photosensitivity notice.
 *
 * It takes an acknowledgement and nothing else. The flash clamp is unconditional, so there is no
 * safety level to pick here — telling someone the visuals are intense and that the rate is capped
 * in the engine is information they are owed, not a decision they are being asked to make.
 */
@Composable
fun SafetyConsent(
    onAcknowledge: () -> Unit,
    modifier: Modifier = Modifier,
) {
    // Mandatory: there is no skip. The notice is one screen and one button.
    OnboardingScaffold(
        step = 0,
        stepCount = ONBOARDING_STEPS,
        icon = Icons.Rounded.FlashOn,
        title = stringResource(R.string.safety_title),
        body = stringResource(R.string.safety_warning),
        modifier = modifier,
        extra = {
            Spacer(Modifier.height(GeodeSpacing.M))
            Text(
                stringResource(R.string.safety_prompt),
                Modifier
                    .crystalPanel(
                        0.6f,
                        MaterialTheme.colorScheme.surfaceContainer,
                        MaterialTheme.colorScheme.primary,
                        corner = 16.dp,
                    ).padding(GeodeSpacing.L),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurface,
            )
        },
    ) {
        CrystalButton(onClick = onAcknowledge, modifier = Modifier.fillMaxWidth()) {
            Text(stringResource(R.string.safety_acknowledge))
        }
    }
}
