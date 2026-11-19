package dev.geode.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
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
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.engine.bridge.PaletteNative
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
    // Another stop or a hex code changed the colour: start from its own HSV.
    if (PaletteNative.colourFromHsv(hsv[0], hsv[1], hsv[2]) != rgb) hsv = hsvOf(rgb)
    val emit = { h: Float, s: Float, v: Float ->
        hsv = floatArrayOf(h, s.coerceIn(0f, 1f), v.coerceIn(0f, 1f))
        onChange(PaletteNative.colourFromHsv(h, s, v))
    }
    Column {
        SaturationValueSquare(hsv, emit)
        HueStrip(hsv[0]) { h -> emit(h, hsv[1], hsv[2]) }
        HexField(rgb, onChange)
    }
}

private fun hsvOf(rgb: Int): FloatArray = FloatArray(3).also { PaletteNative.colourToHsv(rgb, it) }

private const val A11Y_STEP = 0.1f
private const val PERCENT = 100f

@Composable
private fun SaturationValueSquare(
    hsv: FloatArray,
    emit: (Float, Float, Float) -> Unit,
) {
    val latest by rememberUpdatedState(hsv)
    val set by rememberUpdatedState(emit)
    val pure = packedColour(PaletteNative.colourFromHsv(hsv[0], 1f, 1f))
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
    val colours = remember { List(HUE_STOPS) { packedColour(PaletteNative.colourFromHsv(it / (HUE_STOPS - 1f), 1f, 1f)) } }
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
    var text by remember(rgb) { mutableStateOf(PaletteNative.colourHex(rgb)) }
    val parsed = PaletteNative.colourParseHex(text)
    OutlinedTextField(
        value = text,
        onValueChange = { typed ->
            text = typed
            val colour = PaletteNative.colourParseHex(typed)
            if (colour >= 0 && colour != rgb) onChange(colour)
        },
        singleLine = true,
        isError = parsed < 0,
        label = { Text(stringResource(R.string.palette_hex_label)) },
        supportingText = { if (parsed < 0) Text(stringResource(R.string.palette_hex_invalid)) },
        keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Characters, imeAction = ImeAction.Done),
        // Narrower than the sheet, so a finger beside it still scrolls the sheet past the field.
        modifier = Modifier.width(HEX_FIELD_WIDTH).padding(top = 4.dp),
    )
}

private val HEX_FIELD_WIDTH = 160.dp
