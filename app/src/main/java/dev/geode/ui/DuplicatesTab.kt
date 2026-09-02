package dev.geode.ui

import android.app.Activity
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import dev.geode.R

/** Same title, artist and length, listed side by side so the spare copy can be played to check and then deleted. */
@Composable
internal fun DuplicatesTab(
    tracks: List<DeviceTrack>,
    playerViewModel: PlayerViewModel,
    onDeleted: () -> Unit,
) {
    val context = LocalContext.current
    val groups = remember(tracks) { LibraryDuplicates.find(tracks) }
    val deleter =
        rememberLauncherForActivityResult(ActivityResultContracts.StartIntentSenderForResult()) { result ->
            if (result.resultCode == Activity.RESULT_OK) onDeleted()
        }
    LazyColumn(Modifier.fillMaxSize()) {
        if (groups.isEmpty()) {
            item { Text(stringResource(R.string.duplicates_none), Modifier.padding(16.dp)) }
        }
        groups.forEach { group ->
            item(key = "${group.title}|${group.artist}") {
                Text(
                    stringResource(R.string.duplicates_group, group.title, group.artist, group.tracks.size),
                    style = MaterialTheme.typography.labelMedium,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 6.dp),
                )
            }
            items(group.tracks.size, key = { group.tracks[it].uri }) { index ->
                val track = group.tracks[index]
                Row(
                    Modifier
                        .fillMaxWidth()
                        .clickable { playerViewModel.playTrack(track.uri) }
                        .padding(horizontal = 16.dp, vertical = 6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    TrackArtwork(track.uri, Modifier.size(40.dp), corner = 8.dp)
                    Spacer(Modifier.width(12.dp))
                    Column(Modifier.weight(1f)) {
                        Text(track.folder.ifBlank { track.uri }, maxLines = 1, overflow = TextOverflow.Ellipsis)
                        Text(
                            listOf(track.album, LibraryBrowse.formatDuration(track.durationMs))
                                .filter { it.isNotBlank() }
                                .joinToString(" · "),
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                        TextButton(onClick = {
                            val request = MediaStore.createDeleteRequest(context.contentResolver, listOf(Uri.parse(track.uri)))
                            deleter.launch(IntentSenderRequest.Builder(request.intentSender).build())
                        }) { Text(stringResource(R.string.action_delete)) }
                    }
                }
            }
        }
        item {
            Text(
                pluralStringResource(R.plurals.duplicates_summary, groups.size, groups.size),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(16.dp),
            )
        }
    }
}
