package dev.geode.ui

import android.app.Activity
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.ui.glass.GlassButton
import dev.geode.ui.glass.GlassIcons
import dev.geode.ui.glass.GlassListRow
import dev.geode.ui.glass.GlassPalette
import dev.geode.ui.glass.waterScroll

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
    LazyColumn(
        Modifier.fillMaxSize().waterScroll(),
        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        if (groups.isEmpty()) {
            item { Text(stringResource(R.string.duplicates_none), color = GlassPalette.textSecondary) }
        }
        groups.forEach { group ->
            item(key = "${group.title}|${group.artist}") {
                Text(
                    stringResource(R.string.duplicates_group, group.title, group.artist, group.tracks.size),
                    style = MaterialTheme.typography.labelMedium,
                    color = GlassPalette.textPrimary,
                )
            }
            items(group.tracks.size, key = { group.tracks[it].uri }) { index ->
                val track = group.tracks[index]
                GlassListRow(
                    title = track.folder.ifBlank { track.uri },
                    subtitle =
                        listOf(track.album, LibraryBrowse.formatDuration(track.durationMs))
                            .filter { it.isNotBlank() }
                            .joinToString(" · "),
                    leading = { TrackArtwork(track.uri, Modifier.size(40.dp)) },
                    trailing = {
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                            GlassButton(
                                text = stringResource(R.string.action_delete),
                                icon = GlassIcons.Close,
                                onClick = {
                                    val request =
                                        MediaStore.createDeleteRequest(context.contentResolver, listOf(Uri.parse(track.uri)))
                                    deleter.launch(IntentSenderRequest.Builder(request.intentSender).build())
                                },
                            )
                        }
                    },
                    onClick = { playerViewModel.playTrack(track.uri) },
                )
            }
        }
        item {
            Text(
                pluralStringResource(R.plurals.duplicates_summary, groups.size, groups.size),
                style = MaterialTheme.typography.bodySmall,
                color = GlassPalette.textSecondary,
            )
        }
    }
}
