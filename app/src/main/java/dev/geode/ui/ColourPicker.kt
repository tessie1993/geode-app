package dev.geode.ui

import android.graphics.Bitmap
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import dev.geode.R
import dev.geode.engine.bridge.GeodeNative
import dev.geode.ui.theme.GeodeSpacing
import kotlin.math.roundToInt

/**
 * The Photoshop picker for one colour: a saturation and brightness square under a hue strip, and a hex
 * field. [rgb] is a packed 0xRRGGBB; every conversion is native (core/viz/Colour.hpp). The hue is kept
 * here between changes, so dragging to grey and back does not lose it.
 */
@Composable
internal fun ColourPicker(
    rgb: Int,
    onChange: (Int) -> Unit,
) {
    var hsv by remember { mutableStateOf(hsvOf(rgb)) }
    // Another stop, a hex code or the eyedropper changed the colour: start from its own HSV.
    if (GeodeNative.colourFromHsv(hsv[0], hsv[1], hsv[2]) != rgb) hsv = hsvOf(rgb)
    val emit = { h: Float, s: Float, v: Float ->
        hsv = floatArrayOf(h, s.coerceIn(0f, 1f), v.coerceIn(0f, 1f))
        onChange(GeodeNative.colourFromHsv(h, s, v))
    }
    Column {
        SaturationValueSquare(hsv, emit)
        HueStrip(hsv[0]) { h -> emit(h, hsv[1], hsv[2]) }
        HexField(rgb, onChange)
    }
}

private fun hsvOf(rgb: Int): FloatArray = FloatArray(3).also { GeodeNative.colourToHsv(rgb, it) }

private const val A11Y_STEP = 0.1f
private const val PERCENT = 100f

@Composable
private fun SaturationValueSquare(
    hsv: FloatArray,
    emit: (Float, Float, Float) -> Unit,
) {
    val latest by rememberUpdatedState(hsv)
    val set by rememberUpdatedState(emit)
    val pure = packedColour(GeodeNative.colourFromHsv(hsv[0], 1f, 1f))
    val label = stringResource(R.string.palette_sv_square)
    val state = stringResource(R.string.palette_sv_state, (hsv[1] * PERCENT).roundToInt(), (hsv[2] * PERCENT).roundToInt())
    val actions =
        listOf(
            R.string.palette_more_saturated to Pair(A11Y_STEP, 0f),
            R.string.palette_less_saturated to Pair(-A11Y_STEP, 0f),
            R.string.palette_brighter to Pair(0f, A11Y_STEP),
            R.string.palette_darker to Pair(0f, -A11Y_STEP),
        ).map { (name, d) ->
            CustomAccessibilityAction(stringResource(name)) {
                set(latest[0], latest[1] + d.first, latest[2] + d.second)
                true
            }
        }
    val at = { o: Offset, size: IntSize -> set(latest[0], o.x / size.width, 1f - o.y / size.height) }
    Canvas(
        Modifier
            .fillMaxWidth()
            .height(SQUARE_HEIGHT)
            .clip(RoundedCornerShape(6.dp))
            .pointerInput(Unit) { detectTapGestures { at(it, size) } }
            .pointerInput(Unit) { detectDragGestures { change, _ -> at(change.position, size) } }
            .semantics {
                contentDescription = label
                stateDescription = state
                customActions = actions
            },
    ) {
        drawRect(Brush.horizontalGradient(listOf(Color.White, pure)))
        drawRect(Brush.verticalGradient(listOf(Color.Transparent, Color.Black)))
        val thumb = Offset(hsv[1] * size.width, (1f - hsv[2]) * size.height)
        drawCircle(Color.Black, radius = THUMB_RADIUS.toPx() + 1.dp.toPx(), center = thumb, style = Stroke(1.dp.toPx()))
        drawCircle(Color.White, radius = THUMB_RADIUS.toPx(), center = thumb, style = Stroke(2.dp.toPx()))
    }
}

private val SQUARE_HEIGHT = 160.dp
private val THUMB_RADIUS = 9.dp
private const val HUE_STOPS = 13

@Composable
private fun HueStrip(
    hue: Float,
    onHue: (Float) -> Unit,
) {
    val set by rememberUpdatedState(onHue)
    val colours = remember { List(HUE_STOPS) { packedColour(GeodeNative.colourFromHsv(it / (HUE_STOPS - 1f), 1f, 1f)) } }
    val label = stringResource(R.string.palette_hue_strip)
    val at = { x: Float, width: Int -> set((x / width).coerceIn(0f, 1f)) }
    Canvas(
        Modifier
            .fillMaxWidth()
            .height(GeodeSpacing.TouchTarget)
            .pointerInput(Unit) { detectTapGestures { at(it.x, size.width) } }
            .pointerInput(Unit) { detectDragGestures { change, _ -> at(change.position.x, size.width) } }
            .semantics {
                contentDescription = label
                progressBarRangeInfo = ProgressBarRangeInfo(hue, 0f..1f, steps = 0)
                setProgress { value ->
                    set(value.coerceIn(0f, 1f))
                    true
                }
            },
    ) {
        val band = size.height / 2f
        val top = (size.height - band) / 2f
        drawRoundRect(
            Brush.horizontalGradient(colours),
            topLeft = Offset(0f, top),
            size = size.copy(height = band),
            cornerRadius = CornerRadius(band / 2f),
        )
        drawCircle(Color.White, radius = band * 0.6f, center = Offset(hue * size.width, size.height / 2f), style = Stroke(2.dp.toPx()))
    }
}

@Composable
private fun HexField(
    rgb: Int,
    onChange: (Int) -> Unit,
) {
    var text by remember(rgb) { mutableStateOf(GeodeNative.colourHex(rgb)) }
    val parsed = GeodeNative.colourParseHex(text)
    OutlinedTextField(
        value = text,
        onValueChange = { typed ->
            text = typed
            val colour = GeodeNative.colourParseHex(typed)
            if (colour >= 0 && colour != rgb) onChange(colour)
        },
        singleLine = true,
        isError = parsed < 0,
        label = { Text(stringResource(R.string.palette_hex_label)) },
        supportingText = { if (parsed < 0) Text(stringResource(R.string.palette_hex_invalid)) },
        keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Characters, imeAction = ImeAction.Done),
        modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
    )
}

/**
 * The eyedropper: [image] (a still of the visual, `geode_viz_still_request`, or the track's artwork)
 * over the whole screen, so the colour stays put while the user aims. Dragging shows the colour under
 * the finger in a loupe above it; letting go takes it. The middle's colour is one button away for
 * TalkBack and for anyone who would rather not drag.
 */
@Composable
internal fun EyedropperDialog(
    image: Bitmap,
    onPick: (Int) -> Unit,
    onDismiss: () -> Unit,
) {
    var box by remember { mutableStateOf(IntSize.Zero) }
    var finger by remember { mutableStateOf<Offset?>(null) }
    var colour by remember { mutableIntStateOf(-1) }
    val picture = remember(image) { image.asImageBitmap() }
    val sample = { o: Offset -> sampleAt(image, box, o) }
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Box(Modifier.fillMaxSize().background(Color.Black)) {
            Image(
                picture,
                contentDescription = stringResource(R.string.palette_pick_image),
                contentScale = ContentScale.Fit,
                modifier =
                    Modifier
                        .fillMaxSize()
                        .onSizeChanged { box = it }
                        .pointerInput(image) {
                            detectTapGestures { o ->
                                val c = sample(o)
                                if (c >= 0) onPick(c)
                            }
                        }.pointerInput(image) {
                            detectDragGestures(
                                onDragStart = { o ->
                                    finger = o
                                    colour = sample(o)
                                },
                                onDragEnd = {
                                    finger = null
                                    if (colour >= 0) onPick(colour)
                                },
                                onDragCancel = { finger = null },
                            ) { change, _ ->
                                finger = change.position
                                colour = sample(change.position)
                            }
                        },
            )
            finger?.let { at -> Loupe(at, colour) }
            Row(
                Modifier.fillMaxWidth().safeDrawingPadding().padding(horizontal = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(stringResource(R.string.palette_pick_hint), color = Color.White, modifier = Modifier.weight(1f))
                IconButton(onClick = onDismiss) {
                    Icon(Icons.Filled.Close, contentDescription = stringResource(R.string.palette_pick_cancel), tint = Color.White)
                }
            }
            FilledTonalButton(
                onClick = { sampleAt(image, box, Offset(box.width / 2f, box.height / 2f)).takeIf { it >= 0 }?.let(onPick) },
                modifier = Modifier.align(Alignment.BottomCenter).safeDrawingPadding().padding(16.dp),
            ) { Text(stringResource(R.string.palette_pick_centre)) }
        }
    }
}

private val LOUPE_SIZE = 64.dp
private val LOUPE_LIFT = 96.dp

@Composable
private fun Loupe(
    at: Offset,
    rgb: Int,
) {
    Column(
        Modifier.offset {
            IntOffset((at.x - (LOUPE_SIZE / 2).toPx()).roundToInt(), (at.y - LOUPE_LIFT.toPx()).roundToInt())
        },
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            Modifier
                .size(LOUPE_SIZE)
                .border(3.dp, Color.White, CircleShape)
                .clip(CircleShape)
                .background(if (rgb >= 0) packedColour(rgb) else Color.Transparent),
        )
        Spacer(Modifier.height(4.dp))
        if (rgb >= 0) Text(GeodeNative.colourHex(rgb), color = Color.White, style = MaterialTheme.typography.labelSmall)
    }
}

private const val SAMPLE_RADIUS = 1

/**
 * The colour under [o] in [box], where [image] is drawn fitted and centred (ContentScale.Fit): the mean
 * of the 3×3 pixels there (native), or -1 off the picture.
 */
private fun sampleAt(
    image: Bitmap,
    box: IntSize,
    o: Offset,
): Int {
    if (box.width == 0 || box.height == 0) return -1
    val scale = minOf(box.width / image.width.toFloat(), box.height / image.height.toFloat())
    val x = ((o.x - (box.width - image.width * scale) / 2f) / scale).toInt()
    val y = ((o.y - (box.height - image.height * scale) / 2f) / scale).toInt()
    if (x !in 0 until image.width || y !in 0 until image.height) return -1
    val left = (x - SAMPLE_RADIUS).coerceAtLeast(0)
    val top = (y - SAMPLE_RADIUS).coerceAtLeast(0)
    val w = (x + SAMPLE_RADIUS).coerceAtMost(image.width - 1) - left + 1
    val h = (y + SAMPLE_RADIUS).coerceAtMost(image.height - 1) - top + 1
    val pixels = IntArray(w * h)
    image.getPixels(pixels, 0, w, left, top, w, h)
    return GeodeNative.colourAverageArgb(pixels)
}
