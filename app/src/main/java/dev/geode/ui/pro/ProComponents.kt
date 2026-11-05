package dev.geode.ui.pro

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
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
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.pro.PaywallTrigger
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
