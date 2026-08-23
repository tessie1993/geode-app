package dev.geode.publish

import android.content.ContentValues
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Rect
import android.graphics.Shader
import android.graphics.Typeface
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import android.text.TextPaint
import android.text.TextUtils
import dev.geode.util.bestEffort
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlin.math.roundToInt

/** The file a thumbnail is written as. */
enum class ThumbnailFormat(
    val label: String,
    val mimeType: String,
    val extension: String,
) {
    /**
     * The default. A photographic 1280x720 PNG regularly clears 2 MB, which is the size limit
     * YouTube rejects a custom thumbnail over.
     */
    JPEG("JPEG", "image/jpeg", "jpg"),

    /** Lossless — worth it for flat, graphic scenes where JPEG rings around hard edges. */
    PNG("PNG", "image/png", "png"),
    ;

    fun compressFormat(): Bitmap.CompressFormat =
        when (this) {
            JPEG -> Bitmap.CompressFormat.JPEG
            PNG -> Bitmap.CompressFormat.PNG
        }
}

/** Where the text sits over the chosen frame. */
enum class ThumbnailLayout(
    val label: String,
) {
    /** Frame only — for a scene that says everything by itself. */
    PLAIN("Frame only"),

    /** Big title bottom-left over a gradient scrim. Reads at any size; the safest default. */
    LOWER_THIRD("Lower third"),

    /** Title dead centre over a dimmed frame. */
    CENTRED("Centred"),

    /** Title in a solid band across the top, frame underneath. */
    TOP_BAND("Top band"),

    /** Title on a solid panel down the left, frame cropped into the right. */
    SIDE_PANEL("Side panel"),
}

/**
 * What to make: which frame of which video, what to write on it, and how to write it out.
 *
 * [accentColor] is an ARGB int so the caller can hand in the live theme accent. [quality] is the
 * JPEG quality and is ignored when [format] is PNG.
 */
data class ThumbnailSpec(
    val source: Uri,
    val atMs: Long,
    val title: String = "",
    val subtitle: String = "",
    val layout: ThumbnailLayout = ThumbnailLayout.LOWER_THIRD,
    val format: ThumbnailFormat = ThumbnailFormat.JPEG,
    val accentColor: Int = ThumbnailMaker.DEFAULT_ACCENT,
    val quality: Int = ThumbnailMaker.DEFAULT_QUALITY,
)

/** Result of composing a preview. */
sealed interface ThumbnailFrame {
    /** The caller owns [bitmap] and is responsible for recycling it. */
    data class Rendered(
        val bitmap: Bitmap,
    ) : ThumbnailFrame

    /** The video has no decodable frame at that point — usually a time past the end. */
    data object NoFrameThere : ThumbnailFrame

    data class Unreadable(
        val message: String,
    ) : ThumbnailFrame
}

/** Result of writing a thumbnail to the gallery. */
sealed interface ThumbnailSave {
    data class Saved(
        val uri: Uri,
        val displayName: String,
    ) : ThumbnailSave

    data object NoFrameThere : ThumbnailSave

    data class Failed(
        val message: String,
    ) : ThumbnailSave
}

/**
 * Turns any frame of a rendered video into a 1280x720 thumbnail with a title on it.
 *
 * 1280x720 is fixed rather than configurable: it is what YouTube asks for, it is the smallest size
 * that stays sharp on a desktop feed, and every other platform accepts it.
 */
class ThumbnailMaker(
    private val context: Context,
) {
    private val bitmapPaint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)

    /**
     * Renders the thumbnail for preview without writing anything.
     *
     * The returned bitmap belongs to the caller — hold it while the preview is on screen and
     * recycle it when the spec changes, or the previews will pile up in memory.
     */
    suspend fun compose(spec: ThumbnailSpec): ThumbnailFrame =
        withContext(Dispatchers.IO) {
            val frame = grab(spec.source, spec.atMs) ?: return@withContext ThumbnailFrame.NoFrameThere
            // The source frame plus the 1280x720 canvas is the one allocation here big enough to
            // fail on a small device, and a failed thumbnail must not take the app down with it.
            val drawn = runCatching { draw(frame, spec) }
            frame.recycle()
            drawn.fold(
                onSuccess = { ThumbnailFrame.Rendered(it) },
                onFailure = { ThumbnailFrame.Unreadable("That frame could not be turned into a thumbnail: ${it.message}") },
            )
        }

    /** Renders and writes the thumbnail into Pictures/Geode. */
    suspend fun save(
        spec: ThumbnailSpec,
        displayName: String,
    ): ThumbnailSave =
        withContext(Dispatchers.IO) {
            when (val composed = compose(spec)) {
                ThumbnailFrame.NoFrameThere -> ThumbnailSave.NoFrameThere
                is ThumbnailFrame.Unreadable -> ThumbnailSave.Failed(composed.message)
                is ThumbnailFrame.Rendered -> {
                    val name = fileNameFor(displayName, spec.format)
                    try {
                        publish(composed.bitmap, spec, name)
                            ?.let { ThumbnailSave.Saved(it, name) }
                            ?: ThumbnailSave.Failed("The thumbnail could not be saved to Pictures/Geode.")
                    } finally {
                        composed.bitmap.recycle()
                    }
                }
            }
        }

    /**
     * A spread of times worth offering as candidate frames.
     *
     * The very first and last frames are skipped: exports commonly open and close on black, and a
     * black thumbnail is the one nobody wants.
     */
    fun suggestedTimes(
        durationMs: Long,
        count: Int = DEFAULT_SUGGESTIONS,
    ): List<Long> {
        if (durationMs <= 0L || count <= 0) return emptyList()
        val span = durationMs * (1f - 2f * EDGE_FRACTION)
        val start = durationMs * EDGE_FRACTION
        return List(count) { index ->
            val position = if (count == 1) 0.5f else index / (count - 1f)
            (start + span * position).toLong().coerceIn(0L, durationMs)
        }
    }

    private fun grab(
        source: Uri,
        atMs: Long,
    ): Bitmap? =
        runCatching {
            val retriever = MediaMetadataRetriever()
            try {
                retriever.setDataSource(context, source)
                val atUs = (atMs * MICROS_PER_MILLI).coerceAtLeast(0L)
                // OPTION_CLOSEST decodes forward from the preceding sync frame, so the user gets the
                // frame they scrubbed to instead of the keyframe before it. For one still that cost
                // is worth paying; OPTION_CLOSEST_SYNC would quietly pick a different picture.
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
                    retriever.getScaledFrameAtTime(atUs, MediaMetadataRetriever.OPTION_CLOSEST, GRAB_WIDTH, GRAB_HEIGHT)
                } else {
                    retriever.getFrameAtTime(atUs, MediaMetadataRetriever.OPTION_CLOSEST)
                }
            } finally {
                retriever.release()
            }
        }.getOrNull()

    private fun draw(
        frame: Bitmap,
        spec: ThumbnailSpec,
    ): Bitmap {
        val canvasBitmap = Bitmap.createBitmap(WIDTH, HEIGHT, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(canvasBitmap)
        canvas.drawColor(BACKDROP)
        // With nothing to write, every layout collapses to the frame on its own — an empty caption
        // panel is worse than no panel.
        val hasText = spec.title.isNotBlank() || spec.subtitle.isNotBlank()
        val panelled = hasText && spec.layout == ThumbnailLayout.SIDE_PANEL
        drawCover(canvas, frame, if (panelled) Rect(PANEL_WIDTH, 0, WIDTH, HEIGHT) else Rect(0, 0, WIDTH, HEIGHT))
        if (!hasText) return canvasBitmap
        when (spec.layout) {
            ThumbnailLayout.PLAIN -> Unit
            ThumbnailLayout.LOWER_THIRD -> drawLowerThird(canvas, spec)
            ThumbnailLayout.CENTRED -> drawCentred(canvas, spec)
            ThumbnailLayout.TOP_BAND -> drawTopBand(canvas, spec)
            ThumbnailLayout.SIDE_PANEL -> drawSidePanel(canvas, spec)
        }
        return canvasBitmap
    }

    private fun drawCover(
        canvas: Canvas,
        frame: Bitmap,
        dest: Rect,
    ) {
        if (frame.width <= 0 || frame.height <= 0) return
        val scale = maxOf(dest.width().toFloat() / frame.width, dest.height().toFloat() / frame.height)
        val sourceWidth = (dest.width() / scale).roundToInt().coerceIn(1, frame.width)
        val sourceHeight = (dest.height() / scale).roundToInt().coerceIn(1, frame.height)
        val left = (frame.width - sourceWidth) / 2
        val top = (frame.height - sourceHeight) / 2
        canvas.drawBitmap(frame, Rect(left, top, left + sourceWidth, top + sourceHeight), dest, bitmapPaint)
    }

    private fun drawLowerThird(
        canvas: Canvas,
        spec: ThumbnailSpec,
    ) {
        val scrimTop = HEIGHT * LOWER_SCRIM_TOP
        canvas.drawRect(0f, scrimTop, WIDTH.toFloat(), HEIGHT.toFloat(), verticalScrim(scrimTop, HEIGHT.toFloat()))
        val title = titlePaint(TITLE_SIZE, Paint.Align.LEFT)
        val subtitle = subtitlePaint(SUBTITLE_SIZE, Paint.Align.LEFT)
        val maxWidth = WIDTH - 2f * MARGIN
        val titleLines = fitLines(title, spec.title, maxWidth, TITLE_LINES)
        val subtitleLines = fitLines(subtitle, spec.subtitle, maxWidth, 1)
        val subtitleHeight = blockHeight(subtitle, subtitleLines.size)
        val titleHeight = blockHeight(title, titleLines.size)
        val titleTop = HEIGHT - MARGIN - subtitleHeight - titleHeight
        drawRule(canvas, spec.accentColor, MARGIN, titleTop - RULE_GAP - RULE_HEIGHT)
        val afterTitle = drawLines(canvas, titleLines, title, MARGIN, titleTop)
        drawLines(canvas, subtitleLines, subtitle, MARGIN, afterTitle)
    }

    private fun drawCentred(
        canvas: Canvas,
        spec: ThumbnailSpec,
    ) {
        canvas.drawColor(CENTRE_SCRIM)
        val centreX = WIDTH / 2f
        val title = titlePaint(TITLE_SIZE_LARGE, Paint.Align.CENTER)
        val subtitle = subtitlePaint(SUBTITLE_SIZE, Paint.Align.CENTER)
        val maxWidth = WIDTH - 4f * MARGIN
        val titleLines = fitLines(title, spec.title, maxWidth, TITLE_LINES)
        val subtitleLines = fitLines(subtitle, spec.subtitle, maxWidth, 1)
        val titleHeight = blockHeight(title, titleLines.size)
        val subtitleHeight = blockHeight(subtitle, subtitleLines.size)
        val top = (HEIGHT - titleHeight - subtitleHeight) / 2f
        drawRule(canvas, spec.accentColor, centreX - RULE_WIDTH / 2f, top - RULE_GAP - RULE_HEIGHT)
        val afterTitle = drawLines(canvas, titleLines, title, centreX, top)
        drawLines(canvas, subtitleLines, subtitle, centreX, afterTitle)
    }

    private fun drawTopBand(
        canvas: Canvas,
        spec: ThumbnailSpec,
    ) {
        canvas.drawRect(0f, 0f, WIDTH.toFloat(), BAND_HEIGHT, fillPaint(SCRIM_SOLID))
        canvas.drawRect(0f, BAND_HEIGHT, WIDTH.toFloat(), BAND_HEIGHT + RULE_HEIGHT, fillPaint(spec.accentColor))
        val title = titlePaint(TITLE_SIZE_SMALL, Paint.Align.LEFT)
        val subtitle = subtitlePaint(SUBTITLE_SIZE_SMALL, Paint.Align.LEFT)
        val maxWidth = WIDTH - 2f * MARGIN
        val titleLines = fitLines(title, spec.title, maxWidth, 1)
        val subtitleLines = fitLines(subtitle, spec.subtitle, maxWidth, 1)
        val top = (BAND_HEIGHT - blockHeight(title, titleLines.size) - blockHeight(subtitle, subtitleLines.size)) / 2f
        val afterTitle = drawLines(canvas, titleLines, title, MARGIN, top)
        drawLines(canvas, subtitleLines, subtitle, MARGIN, afterTitle)
    }

    private fun drawSidePanel(
        canvas: Canvas,
        spec: ThumbnailSpec,
    ) {
        val panelRight = PANEL_WIDTH.toFloat()
        canvas.drawRect(0f, 0f, panelRight, HEIGHT.toFloat(), fillPaint(PANEL_FILL))
        // Feather the panel into the frame so the join reads as a design, not a seam.
        val feather =
            Paint().apply {
                shader =
                    LinearGradient(
                        panelRight,
                        0f,
                        panelRight + PANEL_FEATHER,
                        0f,
                        PANEL_FILL,
                        Color.TRANSPARENT,
                        Shader.TileMode.CLAMP,
                    )
            }
        canvas.drawRect(panelRight, 0f, panelRight + PANEL_FEATHER, HEIGHT.toFloat(), feather)
        val title = titlePaint(TITLE_SIZE_SMALL, Paint.Align.LEFT)
        val subtitle = subtitlePaint(SUBTITLE_SIZE_SMALL, Paint.Align.LEFT)
        val maxWidth = panelRight - 2f * PANEL_MARGIN
        val titleLines = fitLines(title, spec.title, maxWidth, PANEL_TITLE_LINES)
        val subtitleLines = fitLines(subtitle, spec.subtitle, maxWidth, 2)
        val titleHeight = blockHeight(title, titleLines.size)
        val subtitleHeight = blockHeight(subtitle, subtitleLines.size)
        val top = (HEIGHT - titleHeight - subtitleHeight) / 2f
        drawRule(canvas, spec.accentColor, PANEL_MARGIN, top - RULE_GAP - RULE_HEIGHT)
        val afterTitle = drawLines(canvas, titleLines, title, PANEL_MARGIN, top)
        drawLines(canvas, subtitleLines, subtitle, PANEL_MARGIN, afterTitle)
    }

    private fun drawRule(
        canvas: Canvas,
        color: Int,
        left: Float,
        top: Float,
    ) {
        canvas.drawRect(left, top, left + RULE_WIDTH, top + RULE_HEIGHT, fillPaint(color))
    }

    /**
     * Lays [text] out at the largest size that fits [maxWidth] in at most [maxLines] lines,
     * shrinking the type before it will ellipsize — a thumbnail title that trails off in "…" has
     * already failed at the only job it has.
     *
     * [paint] is left at the size that was chosen, so measuring and drawing the returned lines with
     * the same paint agrees with the layout.
     */
    @Suppress("ReturnCount")
    private fun fitLines(
        paint: TextPaint,
        text: String,
        maxWidth: Float,
        maxLines: Int,
    ): List<String> {
        val trimmed = text.trim()
        if (trimmed.isEmpty()) return emptyList()
        var size = paint.textSize
        while (size > MIN_TEXT_SIZE) {
            paint.textSize = size
            val lines = wrap(paint, trimmed, maxWidth)
            if (lines.size <= maxLines && lines.all { paint.measureText(it) <= maxWidth }) return lines
            size -= TEXT_SIZE_STEP
        }
        paint.textSize = MIN_TEXT_SIZE
        return wrap(paint, trimmed, maxWidth).take(maxLines).mapIndexed { index, line ->
            if (index == maxLines - 1) {
                TextUtils.ellipsize(line, paint, maxWidth, TextUtils.TruncateAt.END).toString()
            } else {
                line
            }
        }
    }

    private fun wrap(
        paint: TextPaint,
        text: String,
        maxWidth: Float,
    ): List<String> {
        val words = text.split(WHITESPACE).filter { it.isNotEmpty() }
        if (words.isEmpty()) return emptyList()
        val lines = mutableListOf<String>()
        var line = words.first()
        for (word in words.drop(1)) {
            val candidate = "$line $word"
            if (paint.measureText(candidate) <= maxWidth) {
                line = candidate
            } else {
                lines += line
                line = word
            }
        }
        lines += line
        return lines
    }

    private fun drawLines(
        canvas: Canvas,
        lines: List<String>,
        paint: TextPaint,
        x: Float,
        top: Float,
    ): Float {
        if (lines.isEmpty()) return top
        val metrics = paint.fontMetrics
        val lineHeight = (metrics.descent - metrics.ascent) * LINE_SPACING
        var baseline = top - metrics.ascent
        for (line in lines) {
            canvas.drawText(line, x, baseline, paint)
            baseline += lineHeight
        }
        return top + lineHeight * lines.size
    }

    private fun blockHeight(
        paint: TextPaint,
        lineCount: Int,
    ): Float {
        if (lineCount == 0) return 0f
        val metrics = paint.fontMetrics
        return (metrics.descent - metrics.ascent) * LINE_SPACING * lineCount
    }

    private fun titlePaint(
        sizePx: Float,
        align: Paint.Align,
    ): TextPaint =
        TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
            typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
            color = TITLE_COLOR
            textSize = sizePx
            textAlign = align
            // A drop shadow is what keeps white type legible over a bright frame without a scrim
            // heavy enough to hide the scene.
            setShadowLayer(SHADOW_RADIUS, 0f, SHADOW_DY, SHADOW_COLOR)
        }

    private fun subtitlePaint(
        sizePx: Float,
        align: Paint.Align,
    ): TextPaint =
        TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
            typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
            color = SUBTITLE_COLOR
            textSize = sizePx
            textAlign = align
            setShadowLayer(SHADOW_RADIUS / 2f, 0f, SHADOW_DY / 2f, SHADOW_COLOR)
        }

    private fun fillPaint(argb: Int): Paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = argb }

    private fun verticalScrim(
        top: Float,
        bottom: Float,
    ): Paint =
        Paint().apply {
            shader = LinearGradient(0f, top, 0f, bottom, Color.TRANSPARENT, SCRIM_SOLID, Shader.TileMode.CLAMP)
        }

    /**
     * Only a real-looking extension is stripped: a title like "Vol. 2 Mix" must not lose its tail
     * to a naive `substringBeforeLast('.')`.
     */
    private fun stripExtension(name: String): String {
        val dot = name.lastIndexOf('.')
        if (dot <= 0) return name
        val suffix = name.substring(dot + 1)
        return if (suffix.length in 2..4 && suffix.all { it.isLetterOrDigit() }) name.substring(0, dot) else name
    }

    private fun fileNameFor(
        displayName: String,
        format: ThumbnailFormat,
    ): String {
        val stem =
            stripExtension(displayName)
                .replace(UNSAFE_FOR_FILENAME, " ")
                .replace(WHITESPACE, " ")
                .trim()
                .trim('.')
                .take(MAX_STEM_CHARS)
                .trim()
                .ifBlank { FALLBACK_NAME }
        return "$stem.${format.extension}"
    }

    private fun publish(
        bitmap: Bitmap,
        spec: ThumbnailSpec,
        displayName: String,
    ): Uri? =
        runCatching {
            // Below Q this insert needs WRITE_EXTERNAL_STORAGE; returning early keeps the
            // failed write from leaving an empty row in the user's Pictures library.
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return null
            val resolver = context.contentResolver
            val values =
                ContentValues().apply {
                    put(MediaStore.Images.Media.DISPLAY_NAME, displayName)
                    put(MediaStore.Images.Media.MIME_TYPE, spec.format.mimeType)
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                        put(MediaStore.Images.Media.RELATIVE_PATH, PICTURES_PATH)
                        put(MediaStore.Images.Media.IS_PENDING, 1)
                    }
                }
            val uri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values) ?: return null
            val written =
                resolver.openOutputStream(uri)?.use { stream ->
                    bitmap.compress(spec.format.compressFormat(), spec.quality.coerceIn(1, 100), stream)
                } ?: false
            if (!written) {
                bestEffort(TAG, "resolver.delete(uri)") { resolver.delete(uri, null, null) }
                return null
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                resolver.update(uri, ContentValues().apply { put(MediaStore.Images.Media.IS_PENDING, 0) }, null, null)
            }
            uri
        }.getOrNull()

    companion object {
        const val WIDTH: Int = 1280
        const val HEIGHT: Int = 720

        /** Theme accent from the default Geode pack; callers should pass the live one. */
        const val DEFAULT_ACCENT: Int = 0xFF9BB9FF.toInt()

        /** High enough that JPEG artefacts do not show on flat gradients, low enough to stay under 2 MB. */
        const val DEFAULT_QUALITY: Int = 92

        private const val DEFAULT_SUGGESTIONS = 8
        private const val EDGE_FRACTION = 0.04f
        private const val MICROS_PER_MILLI = 1000L

        /**
         * Frames are grabbed at up to twice the output size: enough that a centre crop still has
         * pixels to spare, without holding a full 4K frame in memory on a small device.
         */
        private const val GRAB_WIDTH = WIDTH * 2
        private const val GRAB_HEIGHT = HEIGHT * 2

        private const val BACKDROP = 0xFF07080D.toInt()
        private const val SCRIM_SOLID = 0xF2000000.toInt()
        private const val CENTRE_SCRIM = 0x99000000.toInt()
        private const val PANEL_FILL = 0xF20A0C14.toInt()
        private const val TITLE_COLOR = 0xFFFFFFFF.toInt()
        private const val SUBTITLE_COLOR = 0xFFD5DCEC.toInt()
        private const val SHADOW_COLOR = 0xB3000000.toInt()

        private const val MARGIN = 64f
        private const val PANEL_WIDTH = 538
        private const val PANEL_MARGIN = 56f
        private const val PANEL_FEATHER = 96f
        private const val BAND_HEIGHT = 190f
        private const val RULE_WIDTH = 96f
        private const val RULE_HEIGHT = 8f
        private const val RULE_GAP = 24f

        private const val TITLE_SIZE = 96f
        private const val TITLE_SIZE_LARGE = 108f
        private const val TITLE_SIZE_SMALL = 72f
        private const val SUBTITLE_SIZE = 42f
        private const val SUBTITLE_SIZE_SMALL = 34f
        private const val TITLE_LINES = 2
        private const val PANEL_TITLE_LINES = 3
        private const val MIN_TEXT_SIZE = 28f
        private const val TEXT_SIZE_STEP = 4f
        private const val LINE_SPACING = 1.08f
        private const val LOWER_SCRIM_TOP = 0.42f

        private const val SHADOW_RADIUS = 18f
        private const val SHADOW_DY = 6f

        private const val PICTURES_PATH = "Pictures/Geode"
        private const val FALLBACK_NAME = "Geode thumbnail"
        private const val MAX_STEM_CHARS = 100

        private val UNSAFE_FOR_FILENAME = Regex("[\\\\/:*?\"<>|\\x00-\\x1F]")
        private val WHITESPACE = Regex("\\s+")
    }
}

private const val TAG = "ThumbnailMaker"
