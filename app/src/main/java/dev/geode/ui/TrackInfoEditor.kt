package dev.geode.ui

import android.app.Activity
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.data.TagWriteOutcome
import dev.geode.model.LibraryTrack
import kotlinx.coroutines.launch

private val COMMON_GENRES =
    listOf("Electronic", "Rock", "Pop", "Hip-Hop", "Jazz", "Classical", "Ambient", "Other")

/** The saved fields, kept aside so a granted [MediaStore.createWriteRequest] consent can retry the write once. */
private data class PendingTrackEdit(
    val uri: String,
    val title: String,
    val artist: String,
    val album: String,
    val genre: String,
    val year: Int,
    val trackNo: Int,
    val comment: String,
)

private suspend fun LibraryViewModel.writeTrackInfo(edit: PendingTrackEdit): TagWriteOutcome =
    writeTrackInfo(edit.uri, edit.title, edit.artist, edit.album, edit.genre, edit.year, edit.trackNo, edit.comment)

@Composable
fun TrackInfoEditor(
    uri: String,
    viewModel: LibraryViewModel,
    onDismiss: () -> Unit,
) {
    var loaded by remember(uri) { mutableStateOf<LibraryTrack?>(null) }
    LaunchedEffect(uri) { loaded = viewModel.trackInfoFor(uri) }
    val initial = loaded ?: return

    var title by remember(initial) { mutableStateOf(initial.title) }
    var artist by remember(initial) { mutableStateOf(initial.artist) }
    var album by remember(initial) { mutableStateOf(initial.album) }
    var genre by remember(initial) { mutableStateOf(initial.genre) }
    var year by remember(initial) { mutableStateOf(if (initial.year > 0) initial.year.toString() else "") }
    var trackNo by remember(initial) { mutableStateOf(if (initial.trackNo > 0) initial.trackNo.toString() else "") }
    var comment by remember(initial) { mutableStateOf(initial.comment) }
    var writeToFile by remember(initial) { mutableStateOf(false) }
    var writeFailed by remember(initial) { mutableStateOf(false) }
    var pendingEdit by remember(initial) { mutableStateOf<PendingTrackEdit?>(null) }
    val scope = rememberCoroutineScope()
    val context = LocalContext.current

    suspend fun commit(edit: PendingTrackEdit) {
        viewModel.saveTrackInfo(
            uri = edit.uri,
            title = edit.title,
            artist = edit.artist,
            album = edit.album,
            genre = edit.genre,
            year = edit.year,
            trackNo = edit.trackNo,
            comment = edit.comment,
        )
        onDismiss()
    }

    // Fired after MediaStore.createWriteRequest returns; a grant retries the write exactly once,
    // matching what the library screen's own tag-write attempt already tried before asking.
    val consentLauncher =
        rememberLauncherForActivityResult(ActivityResultContracts.StartIntentSenderForResult()) { result ->
            val edit = pendingEdit
            pendingEdit = null
            if (edit == null) return@rememberLauncherForActivityResult
            scope.launch {
                val granted = result.resultCode == Activity.RESULT_OK
                val retried = granted && viewModel.writeTrackInfo(edit) == TagWriteOutcome.Written
                if (retried) commit(edit) else writeFailed = true
            }
        }

    fun save() {
        val savedTitle = title.trim().ifBlank { initial.title }
        val savedYear = year.toIntOrNull() ?: 0
        val savedTrackNo = trackNo.toIntOrNull() ?: 0
        val edit =
            PendingTrackEdit(
                uri = uri,
                title = savedTitle,
                artist = artist.trim(),
                album = album.trim(),
                genre = genre.trim(),
                year = savedYear,
                trackNo = savedTrackNo,
                comment = comment.trim(),
            )
        scope.launch {
            if (writeToFile) {
                when (viewModel.writeTrackInfo(edit)) {
                    TagWriteOutcome.Written -> {}
                    TagWriteOutcome.NeedsConsent -> {
                        val parsed = Uri.parse(uri)
                        val canRequestConsent =
                            Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && parsed.authority == MediaStore.AUTHORITY
                        if (canRequestConsent) {
                            pendingEdit = edit
                            val request = MediaStore.createWriteRequest(context.contentResolver, listOf(parsed))
                            consentLauncher.launch(IntentSenderRequest.Builder(request.intentSender).build())
                        } else {
                            writeFailed = true
                        }
                        return@launch
                    }
                    TagWriteOutcome.Refused, TagWriteOutcome.Unsupported -> {
                        writeFailed = true
                        return@launch
                    }
                }
            }
            commit(edit)
        }
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.track_info_title)) },
        text = {
            Column(
                Modifier.verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                OutlinedTextField(
                    value = title,
                    onValueChange = { title = it },
                    label = { Text(stringResource(R.string.track_info_field_title)) },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = artist,
                    onValueChange = { artist = it },
                    label = { Text(stringResource(R.string.track_info_field_artist)) },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = album,
                    onValueChange = { album = it },
                    label = { Text(stringResource(R.string.track_info_field_album)) },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = genre,
                    onValueChange = { genre = it },
                    label = { Text(stringResource(R.string.track_info_field_genre)) },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                Row(
                    Modifier.horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    COMMON_GENRES.forEach { g ->
                        FilterChip(
                            selected = genre.equals(g, ignoreCase = true),
                            onClick = { genre = g },
                            label = { Text(g) },
                        )
                    }
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(
                        value = year,
                        onValueChange = { v -> year = v.filter { it.isDigit() }.take(4) },
                        label = { Text(stringResource(R.string.track_info_field_year)) },
                        singleLine = true,
                        modifier = Modifier.weight(1f),
                    )
                    OutlinedTextField(
                        value = trackNo,
                        onValueChange = { v -> trackNo = v.filter { it.isDigit() }.take(3) },
                        label = { Text(stringResource(R.string.track_info_field_track_no)) },
                        singleLine = true,
                        modifier = Modifier.weight(1f),
                    )
                }
                OutlinedTextField(
                    value = comment,
                    onValueChange = { comment = it },
                    label = { Text(stringResource(R.string.track_info_field_comment)) },
                    minLines = 3,
                    modifier = Modifier.fillMaxWidth(),
                )
                LabeledSwitch(
                    stringResource(R.string.track_info_write_file),
                    checked = writeToFile,
                    onCheckedChange = {
                        writeToFile = it
                        writeFailed = false
                    },
                )
                Text(
                    stringResource(if (writeFailed) R.string.track_info_write_failed else R.string.track_info_note),
                    style = MaterialTheme.typography.bodySmall,
                    color = if (writeFailed) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        },
        confirmButton = {
            Button(onClick = { save() }) { Text(stringResource(R.string.action_save)) }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) } },
    )
}
