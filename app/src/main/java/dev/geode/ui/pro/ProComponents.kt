package dev.geode.ui.pro

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.ui.geodeViewModel

/** The small "PRO" pill: on locked items in pickers, and atop the paywall. */
@Composable
fun ProBadge(modifier: Modifier = Modifier) {
    val description = stringResource(R.string.pro_badge_description)
    Box(
        modifier
            .background(ProColors.ctaBrush, RoundedCornerShape(50))
            .padding(horizontal = 10.dp, vertical = 3.dp)
            .semantics { contentDescription = description },
    ) {
        Text(
            stringResource(R.string.pro_badge),
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.Bold,
            color = Color.White,
        )
    }
}

/**
 * Shows [content] when [feature] is allowed, otherwise a panel that explains it and opens the
 * paywall. For whole destinations (the Studio tab); single controls gate in their ViewModel.
 */
@Composable
fun ProGate(
    feature: ProFeature,
    content: @Composable () -> Unit,
) {
    val viewModel: PaywallViewModel = geodeViewModel()
    val gate by viewModel.gate.collectAsStateWithLifecycle()
    if (gate.allows(feature)) {
        content()
    } else {
        ProLockedPanel(
            title = stringResource(R.string.pro_locked_studio_title),
            body = stringResource(R.string.pro_locked_studio_body),
            onUnlock = { viewModel.open(PaywallTrigger.Feature(feature)) },
        )
    }
}

@Composable
private fun ProLockedPanel(
    title: String,
    body: String,
    onUnlock: () -> Unit,
) {
    Box(Modifier.fillMaxSize().padding(24.dp), contentAlignment = Alignment.Center) {
        Column(
            Modifier
                .widthIn(max = 480.dp)
                .background(ProColors.surface, RoundedCornerShape(24.dp))
                .border(1.dp, ProColors.hairline, RoundedCornerShape(24.dp))
                .padding(24.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            ProBadge()
            Spacer(Modifier.height(12.dp))
            Text(
                title,
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Bold,
                color = Color.White,
                textAlign = TextAlign.Center,
                modifier = Modifier.semantics { heading() },
            )
            Spacer(Modifier.height(8.dp))
            Text(body, style = MaterialTheme.typography.bodyMedium, color = ProColors.muted, textAlign = TextAlign.Center)
            Spacer(Modifier.height(20.dp))
            Button(
                onClick = onUnlock,
                shape = RoundedCornerShape(16.dp),
                colors = ButtonDefaults.buttonColors(containerColor = ProColors.accent, contentColor = Color.White),
                modifier = Modifier.fillMaxWidth().heightIn(min = 52.dp),
            ) { Text(stringResource(R.string.pro_locked_cta)) }
        }
    }
}

/** Settings' permanent entry: Pro status, the way in to the paywall, and restore from there. */
@Composable
fun GoProSettingsRow(modifier: Modifier = Modifier) {
    val viewModel: PaywallViewModel = geodeViewModel()
    val gate by viewModel.gate.collectAsStateWithLifecycle()
    // The whole row opens the paywall: the offer when free, the "You're Pro" screen when owned.
    Row(
        modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp)
            .clip(RoundedCornerShape(16.dp))
            .clickable { viewModel.open(PaywallTrigger.Settings) }
            .background(ProColors.surface, RoundedCornerShape(16.dp))
            .border(1.dp, ProColors.hairline, RoundedCornerShape(16.dp))
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Column(Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    stringResource(R.string.pro_settings_title),
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.SemiBold,
                    color = Color.White,
                )
                ProBadge()
            }
            Text(
                stringResource(if (gate.isPro) R.string.pro_settings_owned else R.string.pro_settings_free),
                style = MaterialTheme.typography.bodySmall,
                color = ProColors.muted,
            )
        }
        // Owned: nothing to offer. Not owned: the paywall, which also holds "Restore purchase".
        if (!gate.isPro) {
            Button(
                onClick = { viewModel.open(PaywallTrigger.Settings) },
                shape = RoundedCornerShape(12.dp),
                colors = ButtonDefaults.buttonColors(containerColor = ProColors.accent, contentColor = Color.White),
                modifier = Modifier.heightIn(min = 48.dp),
            ) { Text(stringResource(R.string.pro_settings_go)) }
        }
    }
}
