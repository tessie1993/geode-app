package dev.geode.ui

import android.content.Context
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
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalInspectionMode
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import dev.geode.data.ByteBudgetCache
import dev.geode.playback.MediaArtwork
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlin.math.ceil

object ArtworkCache {
    /** Full-size art for the big views. Rows pass their own, smaller size. */
    const val LARGE_PX = 384

    private const val MAX_MISSES = 4000

    /** At most two cover decodes at once, so a fling can't open a retriever per visible row. */
    private val decodeDispatcher = Dispatchers.IO.limitedParallelism(2)

    // An eighth of the app's heap, as the Android bitmap-cache guide suggests, counted in bytes.
    private val cache =
        ByteBudgetCache<String, ImageBitmap>(Runtime.getRuntime().maxMemory() / 8, MAX_MISSES) {
            it.width.toLong() * it.height * 4
        }

    private fun key(
        uri: String,
        px: Int,
    ) = "$uri@$px"

    suspend fun load(
        context: Context,
        uri: String,
        px: Int = LARGE_PX,
    ): ImageBitmap? =
        cache.get(key(uri, px)) {
            withContext(decodeDispatcher) { MediaArtwork.decodeEmbedded(context, uri, px)?.asImageBitmap() }
        }

    fun peek(
        uri: String,
        px: Int = LARGE_PX,
    ): ImageBitmap? = cache.peek(key(uri, px))
}

@Composable
fun TrackArtwork(
    uri: String?,
    modifier: Modifier = Modifier,
    corner: Dp = 14.dp,
    decodeSize: Dp? = null,
) {
    val context = LocalContext.current
    val density = LocalDensity.current
    val px = decodeSize?.let { with(density) { ceil(it.toPx()).toInt() } } ?: ArtworkCache.LARGE_PX
    val inspecting = LocalInspectionMode.current
    var art by remember(uri, px) { mutableStateOf(uri?.let { ArtworkCache.peek(it, px) }) }
    LaunchedEffect(uri, px) {
        if (uri != null && art == null && !inspecting) art = ArtworkCache.load(context, uri, px)
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
