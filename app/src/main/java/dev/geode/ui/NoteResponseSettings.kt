package dev.geode.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.NoteResponseStore

/**
 * Settings › Behaviour › Note & key reactions: one switch, free and on by default. The store saves it and
 * hands it to the engine, so the screen, exports, the wallpaper and the dream all follow it at once. How
 * strongly each part answers is per style, in Customize (Advanced › Note & key, and Look's note blooms).
 */
@Composable
internal fun NoteResponseGroup() {
    val context = LocalContext.current
    val store = remember { NoteResponseStore.get(context) }
    val enabled by store.enabled.collectAsStateWithLifecycle()
    Text(
        stringResource(R.string.notes_body),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    LabeledSwitch(stringResource(R.string.notes_switch), checked = enabled, onCheckedChange = store::setEnabled)
}
