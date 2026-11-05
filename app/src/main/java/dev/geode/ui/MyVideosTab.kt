package dev.geode.ui

import android.content.Context
import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Build
import android.text.format.DateUtils
import android.util.Size
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
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
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
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
import androidx.compose.runtime.saveable.rememberSaveable
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
import dev.geode.ui.theme.floatingGlass
import dev.geode.util.bestEffort
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Studio › My videos: everything Studio made, newest first, in one grid. A render is a finished
 * file listed by its project ([dev.geode.editor.StudioProject.renders]); a take is a recorded
 * performance, itself a project since part 1D. The chips narrow the grid to one kind.
 *
 * Deleting is never a dialog: the item leaves the grid at once and a snackbar offers Undo; only
 * when the Undo window passes unanswered is the file (or the take) really deleted, by
 * [StudioViewModel.deleteVideoLater], which keeps waiting if the tab is left meanwhile.
 */
@Composable
internal fun MyVideosTab(
    viewModel: StudioViewModel,
    onRenderTake: (String) -> Unit,
) {
    val editor by viewModel.editor.collectAsStateWithLifecycle()
    val takes by viewModel.takeState.collectAsStateWithLifecycle()
    val context = LocalContext.current
    val resources = LocalResources.current
    val scope = rememberCoroutineScope()
    val snackbar = remember { SnackbarHostState() }
    var filter by rememberSaveable { mutableStateOf(VideoFilter.ALL) }
    // Items waiting out their Undo snackbar; hidden from the grid until then.
    val pendingDelete by viewModel.pendingVideoDeletes.collectAsStateWithLifecycle()
    val chooserTitle = stringResource(R.string.studio_share_chooser)
    val undoText = stringResource(R.string.my_videos_undo)

    val renders =
        editor.projects.flatMap { project ->
            project.renders.map { MyVideo.Render(project.id, it.uri, it.label.ifBlank { project.name }, it.createdAtMs, it.durationMs) }
        }
    val takeItems = takes.takes.map { MyVideo.Take(it.projectId, it.name, it.createdAtMs, it.durationMs, it.rendersAgain) }
    val shown =
        when (filter) {
            VideoFilter.ALL -> renders + takeItems
            VideoFilter.RENDERS -> renders
            VideoFilter.TAKES -> takeItems
        }.filterNot { it.key in pendingDelete }
            .sortedByDescending { it.createdAtMs }

    val delete: (MyVideo) -> Unit = { item ->
        viewModel.deleteVideoLater(
            when (item) {
                is MyVideo.Render -> VideoDelete.Render(item.projectId, item.uri)
                is MyVideo.Take -> VideoDelete.Take(item.title)
            },
        )
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
            item(span = { GridItemSpan(maxLineSpan) }) {
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    VideoFilter.entries.forEach { f ->
                        FilterChip(selected = filter == f, onClick = { filter = f }, label = { Text(stringResource(f.labelRes)) })
                    }
                }
            }
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
                    onPlay = { (item as? MyVideo.Render)?.let { context.viewVideo(Uri.parse(it.uri)) } },
                    onShare = { (item as? MyVideo.Render)?.let { context.shareVideo(Uri.parse(it.uri), chooserTitle, it.title) } },
                    onRender = { onRenderTake(item.title) },
                    onDelete = { delete(item) },
                )
            }
        }
        SnackbarHost(snackbar, Modifier.align(Alignment.BottomCenter))
    }
}

private enum class VideoFilter(
    val labelRes: Int,
) {
    ALL(R.string.my_videos_all),
    RENDERS(R.string.my_videos_renders),
    TAKES(R.string.my_videos_takes),
}

/** One grid item: a finished render, or a recorded take that has not been rendered by itself. */
private sealed interface MyVideo {
    val projectId: ProjectId
    val title: String
    val createdAtMs: Long
    val durationMs: Long
    val key: String

    data class Render(
        override val projectId: ProjectId,
        val uri: String,
        override val title: String,
        override val createdAtMs: Long,
        override val durationMs: Long,
    ) : MyVideo {
        override val key: String get() = "render:$uri"
    }

    data class Take(
        override val projectId: ProjectId,
        override val title: String,
        override val createdAtMs: Long,
        override val durationMs: Long,
        /** A track take with choices (#457); a mic or capture take has nothing to render again over. */
        val rendersAgain: Boolean,
    ) : MyVideo {
        override val key: String get() = "take:$title"
    }
}

/**
 * A tap does the obvious thing: a render plays, a take renders again (#457: Render opens with the take
 * chosen, so its choices render offline at whatever format is picked there). A mic or capture take has no
 * song under it, so it offers nothing to render again; its video is the render beside it.
 * The rest sits behind the card's ⋮, as on every row in the app: Share (a render) and Delete, with
 * Undo.
 */
@Composable
private fun MyVideoCard(
    item: MyVideo,
    onPlay: () -> Unit,
    onShare: () -> Unit,
    onRender: () -> Unit,
    onDelete: () -> Unit,
) {
    var menu by remember { mutableStateOf(false) }
    val primaryLabel = stringResource(if (item is MyVideo.Render) R.string.my_videos_play else R.string.my_videos_render)
    val canRender = item is MyVideo.Render || (item is MyVideo.Take && item.rendersAgain)
    Column(
        Modifier
            .fillMaxWidth()
            .floatingGlass()
            .clickable(enabled = canRender, onClickLabel = primaryLabel, onClick = if (item is MyVideo.Render) onPlay else onRender)
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        VideoThumbnail((item as? MyVideo.Render)?.uri)
        Row(verticalAlignment = Alignment.Top) {
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text(item.title, style = MaterialTheme.typography.titleSmall, maxLines = 2, overflow = TextOverflow.Ellipsis)
                Text(
                    listOf(
                        stringResource(if (item is MyVideo.Take) R.string.my_videos_kind_take else R.string.my_videos_kind_render),
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
                    if (item is MyVideo.Render) {
                        DropdownMenuItem(text = { Text(stringResource(R.string.my_videos_share)) }, onClick = pick(onShare))
                    }
                    DropdownMenuItem(text = { Text(stringResource(R.string.my_videos_delete)) }, onClick = pick(onDelete))
                }
            }
        }
    }
}

/**
 * A frame of the video, read off the main thread. A take has no file of its own until it is
 * rendered (part 2D), so it keeps the empty tile, which also stands in while a frame loads or when
 * the file has gone.
 */
@Composable
private fun VideoThumbnail(uri: String?) {
    val context = LocalContext.current
    val frame by produceState<ImageBitmap?>(null, uri) {
        value = uri?.let { withContext(Dispatchers.IO) { context.videoFrame(Uri.parse(it))?.asImageBitmap() } }
    }
    Box(
        Modifier
            .fillMaxWidth()
            .aspectRatio(16f / 9f)
            .clip(RoundedCornerShape(12.dp))
            .background(MaterialTheme.colorScheme.surfaceVariant),
    ) {
        frame?.let { Image(it, contentDescription = null, modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Crop) }
    }
}

/** Android 10+ has a cached thumbnail per media row; older phones decode one frame. Null on any failure. */
private fun Context.videoFrame(uri: Uri): Bitmap? {
    var frame: Bitmap? = null
    bestEffort(TAG, "video thumbnail") {
        frame =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                contentResolver.loadThumbnail(uri, Size(THUMB_WIDTH, THUMB_HEIGHT), null)
            } else {
                val retriever = MediaMetadataRetriever()
                try {
                    retriever.setDataSource(this, uri)
                    retriever.frameAtTime
                } finally {
                    retriever.release()
                }
            }
    }
    return frame
}

private const val TAG = "MyVideosTab"
private const val THUMB_WIDTH = 320
private const val THUMB_HEIGHT = 180
