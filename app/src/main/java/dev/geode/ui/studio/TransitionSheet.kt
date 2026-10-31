package dev.geode.ui.studio

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.editor.ClipTransition
import dev.geode.render.TransitionCatalog
import dev.geode.ui.CrystalButton

/** Picks the GL Transition a clip opens with, and how long it runs; "None" clears it. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TransitionSheet(
    current: ClipTransition?,
    onPick: (ClipTransition?) -> Unit,
    onDismiss: () -> Unit,
) {
    val context = LocalContext.current
    val library = remember { TransitionCatalog.library(context) }
    var durationMs by remember { mutableStateOf(current?.boundedDurationMs ?: ClipTransition.DEFAULT_TRANSITION_MS) }
    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(Modifier.padding(horizontal = 20.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(stringResource(R.string.editor_transition), style = MaterialTheme.typography.titleMedium)
            Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                ClipTransition.DURATION_CHOICES_MS.forEach { choice ->
                    CrystalButton(compact = true, filled = choice == durationMs, onClick = { durationMs = choice }) {
                        Text(stringResource(R.string.editor_transition_seconds, choice / 1000f))
                    }
                }
            }
            TextButton(onClick = { onPick(null) }, modifier = Modifier.fillMaxWidth()) {
                Text(stringResource(R.string.editor_transition_none), modifier = Modifier.fillMaxWidth())
            }
            LazyColumn {
                items(library, key = { it.name }) { def ->
                    TextButton(onClick = { onPick(ClipTransition(def.name, durationMs)) }, modifier = Modifier.fillMaxWidth()) {
                        Text(
                            if (def.name == current?.id) stringResource(R.string.editor_transition_current, def.name) else def.name,
                            modifier = Modifier.fillMaxWidth(),
                        )
                    }
                }
            }
        }
    }
}
