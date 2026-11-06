package dev.geode.ui

import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.TakeRule
import dev.geode.data.TakeRuleStore

/**
 * Settings › Behaviour › Record: what pause, seek and a track change do during a take (#459). Three choices,
 * the owner's default first; the line under them says what the chosen one does. The store keeps it and the
 * engine decides (`core/output/TakeRules`); it applies from the next take.
 */
@Composable
internal fun RecordGroup() {
    val context = LocalContext.current
    val store = remember { TakeRuleStore.get(context) }
    val rule by store.rule.collectAsStateWithLifecycle()
    Text(
        stringResource(R.string.take_rule_body),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    CrystalSegmented(
        options = TakeRule.entries.map { stringResource(it.labelRes) },
        selected = TakeRule.entries.indexOf(rule),
        onSelect = { store.set(TakeRule.entries[it]) },
        modifier = Modifier.padding(top = 4.dp),
    )
    Text(
        stringResource(rule.detailRes),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}
