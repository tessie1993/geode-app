package dev.geode.ui

import android.net.Uri
import android.text.format.DateUtils
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.SnackbarResult
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.editor.ProjectId
import dev.geode.geodeContainer
import dev.geode.ui.theme.floatingGlass
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * Studio › My videos: everything Studio made, newest first, in one grid: each render and each
 * recording, a finished file listed by its project ([dev.geode.editor.StudioProject.renders]).
 *
 * Deleting is never a dialog: the item leaves the grid at once and a snackbar offers Undo; only
 * when the Undo window passes unanswered is the file really deleted, by
 * [StudioViewModel.deleteVideoLater], which keeps waiting if the tab is left meanwhile.
 */
@Composable
internal fun MyVideosTab(viewModel: StudioViewModel) {
    val editor by viewModel.editor.collectAsStateWithLifecycle()
    val context = LocalContext.current
    val resources = LocalResources.current
    val scope = rememberCoroutineScope()
    val snackbar = remember { SnackbarHostState() }
    // Items waiting out their Undo snackbar; hidden from the grid until then.
    val pendingDelete by viewModel.pendingVideoDeletes.collectAsStateWithLifecycle()
    val chooserTitle = stringResource(R.string.studio_share_chooser)
    val undoText = stringResource(R.string.my_videos_undo)

    val shown =
        editor.projects
            .flatMap { project ->
                project.renders.map { MyVideo(project.id, it.uri, it.label.ifBlank { project.name }, it.createdAtMs, it.durationMs) }
            }.filterNot { it.key in pendingDelete }
            .sortedByDescending { it.createdAtMs }

    val delete: (MyVideo) -> Unit = { item ->
        viewModel.deleteVideoLater(VideoDelete(item.projectId, item.uri))
        val deletedText = resources.getString(R.string.my_videos_deleted, item.title)
        // The snackbar only offers Undo; the delete itself waits in the ViewModel and happens even
        // if this tab leaves the screen first (#322).
        scope.launch {
            val result = snackbar.showSnackbar(deletedText, actionLabel = undoText, duration = SnackbarDuration.Short)
            if (result == SnackbarResult.ActionPerformed) viewModel.undoVideoDelete(item.key)
        }
    }

    Box(Modifier.fillMaxSize()) {
        LazyVerticalGrid(
            columns = GridCells.Adaptive(minSize = 160.dp),
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(16.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            if (shown.isEmpty()) {
                item(span = { GridItemSpan(maxLineSpan) }) {
                    Text(
                        stringResource(R.string.my_videos_empty),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(vertical = 24.dp),
                    )
                }
            }
            items(shown, key = { it.key }) { item ->
                MyVideoCard(
                    item = item,
                    onPlay = { context.viewVideo(Uri.parse(item.uri)) },
                    onShare = { context.shareVideo(Uri.parse(item.uri), chooserTitle, item.title) },
                    onDelete = { delete(item) },
                )
            }
        }
        SnackbarHost(snackbar, Modifier.align(Alignment.BottomCenter))
    }
}

/** One grid item: a finished file and the project that lists it. */
private data class MyVideo(
    val projectId: ProjectId,
    val uri: String,
    val title: String,
    val createdAtMs: Long,
    val durationMs: Long,
) {
    val key: String get() = VideoDelete.keyOf(uri)
}

/** A tap plays it; Share and Delete (with Undo) sit behind the card's ⋮, as on every row in the app. */
@Composable
private fun MyVideoCard(
    item: MyVideo,
    onPlay: () -> Unit,
    onShare: () -> Unit,
    onDelete: () -> Unit,
) {
    var menu by remember { mutableStateOf(false) }
    Column(
        Modifier
            .fillMaxWidth()
            .floatingGlass()
            .clickable(onClickLabel = stringResource(R.string.my_videos_play), onClick = onPlay)
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        VideoThumbnail(item.uri)
        Row(verticalAlignment = Alignment.Top) {
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(item.title, style = MaterialTheme.typography.titleSmall, maxLines = 2, overflow = TextOverflow.Ellipsis)
                Text(
                    listOf(
                        formatClock(item.durationMs),
                        DateUtils.getRelativeTimeSpanString(item.createdAtMs).toString(),
                    ).joinToString(" · "),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Box {
                IconButton(onClick = { menu = true }) {
                    Icon(Icons.Filled.MoreVert, contentDescription = stringResource(R.string.edit_home_more_for, item.title))
                }
                DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                    val pick: (() -> Unit) -> () -> Unit = { action ->
                        {
                            menu = false
                            action()
                        }
                    }
                    DropdownMenuItem(text = { Text(stringResource(R.string.my_videos_share)) }, onClick = pick(onShare))
                    DropdownMenuItem(text = { Text(stringResource(R.string.my_videos_delete)) }, onClick = pick(onDelete))
                }
            }
        }
    }
}

/**
 * A frame of the video, read off the main thread. One whose frame cannot be read, after one retry,
 * shows a video icon labelled "Video unavailable" instead of a blank box.
 */
@Composable
private fun VideoThumbnail(uri: String) {
    val mediaStore = LocalContext.current.geodeContainer.mediaStore
    var failed by remember(uri) { mutableStateOf(false) }
    val frame by produceState<ImageBitmap?>(null, uri) {
        val parsed = Uri.parse(uri)
        value = mediaStore.videoFrame(parsed, THUMB_WIDTH, THUMB_HEIGHT)?.asImageBitmap()
        if (value == null) {
            delay(THUMB_RETRY_MS)
            value = mediaStore.videoFrame(parsed, THUMB_WIDTH, THUMB_HEIGHT)?.asImageBitmap()
        }
        failed = value == null
    }
    Box(
        Modifier
            .fillMaxWidth()
            .aspectRatio(16f / 9f)
            .clip(RoundedCornerShape(12.dp))
            .background(MaterialTheme.colorScheme.surfaceVariant),
        contentAlignment = Alignment.Center,
    ) {
        frame?.let { Image(it, contentDescription = null, modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Crop) }
        if (failed) {
            Icon(
                Icons.Filled.Videocam,
                contentDescription = stringResource(R.string.my_videos_thumbnail_unavailable),
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

private const val THUMB_RETRY_MS = 2_000L
private const val THUMB_WIDTH = 320
private const val THUMB_HEIGHT = 180
