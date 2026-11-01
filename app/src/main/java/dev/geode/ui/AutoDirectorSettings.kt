package dev.geode.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.heightIn
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.AutoDirectorStore
import dev.geode.pro.AutoDirectorPolicy
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.pro.ProBadge

/**
 * Settings › Behaviour › Auto-director: one switch. The feeling it reads is never named on screen, by
 * design: the user sees it, not a label. Free users may switch it on and get the first minute of each
 * song (the taste, [AutoDirectorPolicy]); the Pro badge and the line under the switch say so, and tapping
 * them opens the paywall on this feature. The entitlement is the paywall's live gate, so the line goes
 * the moment a purchase or the trial lands.
 */
@Composable
internal fun AutoDirectorGroup() {
    val context = LocalContext.current
    val store = remember { AutoDirectorStore.get(context) }
    val enabled by store.enabled.collectAsStateWithLifecycle()
    val paywall: PaywallViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    val taste = AutoDirectorPolicy.isTaste(gate)
    Text(
        stringResource(R.string.director_body),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    Column {
        LabeledSwitch(
            stringResource(R.string.director_switch),
            checked = enabled,
            onCheckedChange = store::setEnabled,
        )
        if (taste) {
            Row(
                modifier =
                    Modifier
                        .heightIn(min = 48.dp)
                        .clickable { paywall.open(PaywallTrigger.Feature(ProFeature.AUTO_DIRECTOR)) },
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                ProBadge()
                Text(
                    stringResource(R.string.director_taste),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}
