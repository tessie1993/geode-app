package dev.geode.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.billing.ProSnapshot
import dev.geode.data.NoteResponseStore
import dev.geode.pro.NoteResponsePolicy
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.ui.pro.PaywallViewModel
import dev.geode.ui.pro.ProBadge

/**
 * Settings › Behaviour › Note & key reactions: one switch, Pro (the trial included). The entitlement is
 * handed to the store whenever it changes, so buying Pro here turns the layer on at once if the switch
 * was already set. A free user who flips the switch gets the paywall instead, and the switch stays off:
 * the setting never claims something the engine is not doing.
 */
@Composable
internal fun NoteResponseGroup() {
    val context = LocalContext.current
    val store = remember { NoteResponseStore.get(context) }
    val paywall: PaywallViewModel = geodeViewModel()
    // The live gate says when the entitlement changed (a purchase, the trial starting or ending); the
    // snapshot is what it is then. The live flow starts at free before billing answers, and reading it
    // directly would switch a Pro user's layer off and on again each time this screen opens.
    val liveGate by paywall.gate.collectAsStateWithLifecycle()
    val gate = remember(liveGate) { ProSnapshot.gate(context) }
    val enabled by store.enabled.collectAsStateWithLifecycle()
    LaunchedEffect(gate) { store.onGate(gate) }
    val locked = NoteResponsePolicy.isLocked(gate)
    Text(
        stringResource(R.string.notes_body),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    Column {
        LabeledSwitch(
            stringResource(R.string.notes_switch),
            checked = enabled && !locked,
            onCheckedChange = { on ->
                if (on && locked) {
                    paywall.open(PaywallTrigger.Feature(ProFeature.NOTE_RESPONSE))
                } else {
                    store.setEnabled(on)
                }
            },
        )
        if (locked) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                ProBadge()
                Text(
                    stringResource(R.string.notes_locked),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}
