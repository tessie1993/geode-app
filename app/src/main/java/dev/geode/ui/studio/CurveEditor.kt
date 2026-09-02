package dev.geode.ui.studio

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import dev.geode.R
import dev.geode.editor.AnimatableParam
import dev.geode.editor.BezierCurve
import dev.geode.editor.EaseShape
import dev.geode.editor.Interpolation
import dev.geode.editor.Keyframe
import dev.geode.editor.ParamValue
import dev.geode.ui.CrystalSlider

/** Interpolation, curve handles and the value of one key. Every change is a new [Keyframe]. */
@Composable
fun KeyEditor(
    key: Keyframe,
    param: AnimatableParam?,
    onChange: (Keyframe) -> Unit,
) {
    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(
            stringResource(R.string.curve_key_at, paramLabel(param), clockLabel(key.atMs)),
            style = MaterialTheme.typography.labelMedium,
        )
        InterpolationChips(key.interpolation) { onChange(key.copy(interpolation = it)) }
        (key.interpolation as? Interpolation.Custom)?.let { custom ->
            BezierHandles(custom.curve) { onChange(key.copy(interpolation = Interpolation.Custom(it))) }
        }
        ValueEditor(key.value, param) { onChange(key.copy(value = it)) }
    }
}

@Composable
private fun InterpolationChips(
    current: Interpolation,
    onPick: (Interpolation) -> Unit,
) {
    val custom = (current as? Interpolation.Custom)?.curve ?: (current as? Interpolation.Ease)?.shape?.curve ?: EaseShape.IN_OUT.curve
    val choices: List<Pair<Int, Interpolation>> =
        listOf(
            R.string.curve_hold to Interpolation.Hold,
            R.string.curve_linear to Interpolation.Linear,
            R.string.curve_ease_in to Interpolation.Ease(EaseShape.IN),
            R.string.curve_ease_out to Interpolation.Ease(EaseShape.OUT),
            R.string.curve_ease_in_out to Interpolation.Ease(EaseShape.IN_OUT),
            R.string.curve_smooth to Interpolation.Ease(EaseShape.SMOOTH),
            R.string.curve_custom to Interpolation.Custom(custom),
        )
    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        choices.forEach { (label, interpolation) ->
            val selected =
                if (interpolation is Interpolation.Custom) current is Interpolation.Custom else interpolation == current
            FilterChip(
                selected = selected,
                onClick = { onPick(interpolation) },
                label = { Text(stringResource(label), style = MaterialTheme.typography.labelSmall) },
            )
        }
    }
}

/** The bezier's two handles, draggable; the curve is drawn from the same [BezierCurve.eval] the export uses. */
@Composable
private fun BezierHandles(
    curve: BezierCurve,
    onCurve: (BezierCurve) -> Unit,
) {
    val line = MaterialTheme.colorScheme.primary
    val grid = MaterialTheme.colorScheme.onSurfaceVariant
    val handle = MaterialTheme.colorScheme.secondary
    Canvas(
        Modifier
            .size(CURVE_SIZE)
            .padding(4.dp)
            .pointerInput(curve) {
                var dragging = -1
                detectDragGestures(
                    onDragStart = { offset ->
                        val w = size.width.toFloat()
                        val h = size.height.toFloat()
                        val first = Offset(curve.c1x * w, (1f - curve.c1y) * h)
                        val second = Offset(curve.c2x * w, (1f - curve.c2y) * h)
                        dragging = if ((offset - first).getDistance() <= (offset - second).getDistance()) 0 else 1
                    },
                    onDrag = { change, _ ->
                        change.consume()
                        val x = (change.position.x / size.width).coerceIn(0f, 1f)
                        val y = (1f - change.position.y / size.height).coerceIn(-0.5f, 1.5f)
                        onCurve(if (dragging == 0) curve.copy(c1x = x, c1y = y) else curve.copy(c2x = x, c2y = y))
                    },
                )
            },
    ) {
        val w = size.width
        val h = size.height
        drawLine(grid.copy(alpha = 0.5f), Offset(0f, h), Offset(w, 0f), strokeWidth = 1f)
        val path =
            Path().apply {
                moveTo(0f, h)
                var i = 1
                while (i <= CURVE_SAMPLES) {
                    val x = i / CURVE_SAMPLES.toFloat()
                    lineTo(x * w, (1f - curve.eval(x)) * h)
                    i++
                }
            }
        drawPath(path, line, style = Stroke(3f))
        val first = Offset(curve.c1x * w, (1f - curve.c1y) * h)
        val second = Offset(curve.c2x * w, (1f - curve.c2y) * h)
        drawLine(handle, Offset(0f, h), first, strokeWidth = 2f)
        drawLine(handle, Offset(w, 0f), second, strokeWidth = 2f)
        drawCircle(handle, radius = 8f, center = first)
        drawCircle(handle, radius = 8f, center = second)
    }
}

@Composable
private fun ValueEditor(
    value: ParamValue,
    param: AnimatableParam?,
    onChange: (ParamValue) -> Unit,
) {
    when (value) {
        is ParamValue.Scalar -> {
            val min = param?.min ?: minOf(0f, value.value)
            val max = param?.max ?: maxOf(1f, value.value)
            LabeledValueSlider(stringResource(R.string.curve_value, "%.2f".format(value.value)), value.value, min, max) {
                onChange(ParamValue.Scalar(it))
            }
        }
        is ParamValue.Vector2 -> {
            LabeledValueSlider("X  %.2f".format(value.x), value.x, -VECTOR_RANGE, VECTOR_RANGE) { onChange(value.copy(x = it)) }
            LabeledValueSlider("Y  %.2f".format(value.y), value.y, -VECTOR_RANGE, VECTOR_RANGE) { onChange(value.copy(y = it)) }
        }
        is ParamValue.Colour -> {
            LabeledValueSlider("R  %.2f".format(value.r), value.r, 0f, 1f) { onChange(value.copy(r = it)) }
            LabeledValueSlider("G  %.2f".format(value.g), value.g, 0f, 1f) { onChange(value.copy(g = it)) }
            LabeledValueSlider("B  %.2f".format(value.b), value.b, 0f, 1f) { onChange(value.copy(b = it)) }
            LabeledValueSlider("A  %.2f".format(value.a), value.a, 0f, 1f) { onChange(value.copy(a = it)) }
        }
        is ParamValue.Toggle ->
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.SpaceBetween) {
                Text(stringResource(R.string.curve_toggle), style = MaterialTheme.typography.bodyMedium)
                Switch(checked = value.on, onCheckedChange = { onChange(ParamValue.Toggle(it)) })
            }
        is ParamValue.Choice -> {
            val labels = param?.choices.orEmpty()
            Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                val count = if (labels.isEmpty()) value.index + 2 else labels.size
                repeat(count) { index ->
                    FilterChip(
                        selected = value.index == index,
                        onClick = { onChange(ParamValue.Choice(index)) },
                        label = { Text(labels.getOrNull(index) ?: index.toString(), style = MaterialTheme.typography.labelSmall) },
                    )
                }
            }
        }
    }
}

@Composable
private fun LabeledValueSlider(
    label: String,
    value: Float,
    min: Float,
    max: Float,
    onChange: (Float) -> Unit,
) {
    Column {
        Text(label, style = MaterialTheme.typography.labelMedium)
        CrystalSlider(value = value.coerceIn(min, max), onValueChange = onChange, valueRange = min..max)
    }
}

/** Picks the parameter a new track will animate. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AddTrackSheet(
    params: List<AnimatableParam>,
    onPick: (AnimatableParam) -> Unit,
    onDismiss: () -> Unit,
) {
    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(Modifier.padding(horizontal = 20.dp, vertical = 8.dp)) {
            Text(stringResource(R.string.curve_add_track), style = MaterialTheme.typography.titleMedium)
            LazyColumn {
                items(params, key = { it.id.value }) { param ->
                    TextButton(onClick = { onPick(param) }, modifier = Modifier.fillMaxWidth()) {
                        Text(paramLabel(param), modifier = Modifier.fillMaxWidth())
                    }
                }
            }
        }
    }
}

@Composable
fun paramLabel(param: AnimatableParam?): String =
    when {
        param == null -> stringResource(R.string.curve_unknown_param)
        param.labelRes != 0 -> stringResource(param.labelRes)
        else -> param.label
    }

private val CURVE_SIZE = 180.dp
private const val CURVE_SAMPLES = 48
private const val VECTOR_RANGE = 2f
