package dev.geode.ui

import android.content.Context
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
    Box(
        modifier.clip(
            androidx.compose.foundation.shape
                .RoundedCornerShape(corner),
        ),
    ) {
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
