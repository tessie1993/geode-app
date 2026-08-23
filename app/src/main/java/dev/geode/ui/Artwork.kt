package dev.geode.ui

import android.content.Context
import android.graphics.Bitmap
import android.net.Uri
import android.os.Build
import android.util.LruCache
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.MusicNote
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalInspectionMode
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import dev.geode.playback.MediaArtwork
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

object ArtworkCache {
    private val NONE = Any()

    private val cache = LruCache<String, Any>(40)

    private const val ART_PX = 384

    suspend fun load(
        context: Context,
        uri: String,
    ): ImageBitmap? {
        cache.get(uri)?.let { return if (it === NONE) null else it as ImageBitmap }
        val decoded = withContext(Dispatchers.IO) { decode(context, uri) }
        cache.put(uri, decoded ?: NONE)
        return decoded
    }

    fun peek(uri: String): ImageBitmap? = cache.get(uri)?.takeIf { it !== NONE } as? ImageBitmap

    private fun decode(
        context: Context,
        uri: String,
    ): ImageBitmap? = MediaArtwork.decodeEmbedded(context, uri, ART_PX)?.asImageBitmap()
}

object VideoFrameCache {
    private val NONE = Any()

    /**
     * Frames here are thumbnails - a filmstrip cell is 56dp tall and a library row's is 96x56dp -
     * so they are decoded to fit this box rather than at the video's own size. The clips are the
     * app's own exports, and a 3840x2160 frame is 33 MB of ARGB_8888: six of those for one
     * filmstrip is an OutOfMemoryError reachable by scrolling Studio.
     */
    private const val FRAME_PX = 384

    /** Bounded in bytes, because bounding it in entries is what made the size unbounded. */
    private const val CACHE_BYTES = 24 * 1024 * 1024

    private val cache =
        object : LruCache<String, Any>(CACHE_BYTES) {
            override fun sizeOf(
                key: String,
                value: Any,
            ): Int = (value as? ImageBitmap)?.asAndroidBitmap()?.allocationByteCount ?: 1
        }

    suspend fun frame(
        context: Context,
        uri: String,
        atMs: Long,
    ): ImageBitmap? {
        val key = "$uri@$atMs"
        cache.get(key)?.let { return if (it === NONE) null else it as ImageBitmap }
        val decoded = withContext(Dispatchers.IO) { extract(context, uri, atMs) }
        cache.put(key, decoded ?: NONE)
        return decoded
    }

    fun peek(
        uri: String,
        atMs: Long,
    ): ImageBitmap? = cache.get("$uri@$atMs")?.takeIf { it !== NONE } as? ImageBitmap

    private fun extract(
        context: Context,
        uri: String,
        atMs: Long,
    ): ImageBitmap? =
        runCatching {
            val retriever = android.media.MediaMetadataRetriever()
            try {
                retriever.setDataSource(context, Uri.parse(uri))
                val atUs = (atMs * 1000L).coerceAtLeast(0L)
                val option = android.media.MediaMetadataRetriever.OPTION_CLOSEST_SYNC
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
                    retriever.getScaledFrameAtTime(atUs, option, FRAME_PX, FRAME_PX)
                } else {
                    retriever.getFrameAtTime(atUs, option)?.let(::downscaled)
                }?.asImageBitmap()
            } finally {
                retriever.release()
            }
        }.getOrNull()

    /**
     * API 26 has no scaled variant, so the full frame is decoded once and reduced immediately;
     * the original is recycled rather than left for the collector to find.
     */
    private fun downscaled(source: Bitmap): Bitmap {
        val longest = maxOf(source.width, source.height)
        if (longest <= FRAME_PX) return source
        val scale = FRAME_PX.toFloat() / longest
        val scaled =
            Bitmap.createScaledBitmap(
                source,
                (source.width * scale).toInt().coerceAtLeast(1),
                (source.height * scale).toInt().coerceAtLeast(1),
                true,
            )
        if (scaled !== source) source.recycle()
        return scaled
    }
}

@Composable
fun VideoFrame(
    uri: String?,
    atMs: Long,
    modifier: Modifier = Modifier,
    corner: Dp = 12.dp,
) {
    val context = LocalContext.current
    val inspecting = LocalInspectionMode.current
    var frame by remember(uri, atMs) { mutableStateOf(uri?.let { VideoFrameCache.peek(it, atMs) }) }
    LaunchedEffect(uri, atMs) {
        if (uri != null && frame == null && !inspecting) frame = VideoFrameCache.frame(context, uri, atMs)
    }
    Box(modifier.clip(androidx.compose.foundation.shape.RoundedCornerShape(corner))) {
        val bitmap = frame
        if (bitmap != null) {
            Image(
                bitmap = bitmap,
                contentDescription = null,
                modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.Crop,
            )
        } else {
            Box(Modifier.fillMaxSize().background(placeholderBrush(uri)))
        }
    }
}

@Composable
fun TrackArtwork(
    uri: String?,
    modifier: Modifier = Modifier,
    corner: Dp = 14.dp,
) {
    val context = LocalContext.current
    val inspecting = LocalInspectionMode.current
    var art by remember(uri) { mutableStateOf(uri?.let(ArtworkCache::peek)) }
    LaunchedEffect(uri) {
        if (uri != null && art == null && !inspecting) art = ArtworkCache.load(context, uri)
    }
    Box(modifier.clip(androidx.compose.foundation.shape.RoundedCornerShape(corner))) {
        val bitmap = art
        if (bitmap != null) {
            Image(
                bitmap = bitmap,
                contentDescription = null,
                modifier = Modifier.fillMaxSize(),
                contentScale = ContentScale.Crop,
            )
        } else {
            Box(Modifier.fillMaxSize().background(placeholderBrush(uri))) {
                Icon(
                    Icons.Filled.MusicNote,
                    null,
                    Modifier.align(Alignment.Center),
                    tint = lerp(MaterialTheme.colorScheme.primary, Color.White, 0.75f).copy(alpha = 0.6f),
                )
            }
        }
    }
}

fun placeholderBrush(uri: String?): Brush {
    val hash = (uri ?: "").hashCode()
    val hue = ((hash ushr 8) % 360 + 360) % 360
    return Brush.linearGradient(
        listOf(
            Color.hsv(hue.toFloat(), 0.55f, 0.42f),
            Color.hsv(((hue + 58) % 360).toFloat(), 0.62f, 0.22f),
        ),
    )
}
