package dev.geode.ui.opaline

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.focusable
import androidx.compose.foundation.gestures.Orientation
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.draggable
import androidx.compose.foundation.gestures.rememberDraggableState
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.inset
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * B01 elastic slider: a gel bead travelling in a recessed channel, with liquid filling the
 * channel behind it. The bead follows the finger exactly; the liquid follows on the library's
 * value spring and the bead stretches with that motion (`MotionController`, up to 28 %).
 * Semantics, keyboard steps and TalkBack adjustment use the host value, never the animation.
 */
@Composable
fun OpalineSlider(
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
    steps: Int = 0,
    enabled: Boolean = true,
    onValueChangeFinished: (() -> Unit)? = null,
) {
    val palette = Opaline.palette
    val renderer = Opaline.renderer
    val span = valueRange.endInclusive - valueRange.start
    val safe = if (value.isFinite()) value.coerceIn(valueRange) else valueRange.start
    val fraction = if (span > 0f) (safe - valueRange.start) / span else 0f
    val change by rememberUpdatedState(onValueChange)
    val finished by rememberUpdatedState(onValueChangeFinished)
    val interaction = remember { MutableInteractionSource() }
    val focused by interaction.collectIsFocusedAsState()
    var dragging by remember { mutableStateOf(false) }
    var dragX by remember { mutableFloatStateOf(0f) }
    val pressure = rememberOpalineSpring(if (dragging) 1f else 0f)
    val liquid = rememberOpalineSpring(fraction, OpalineMotion.VALUE_FREQUENCY, OpalineMotion.VALUE_DAMPING)
    val rtl = LocalLayoutDirection.current == LayoutDirection.Rtl
    val seed = rememberOpalineSeed()
    var width by remember { mutableFloatStateOf(0f) }
    val beadPx = with(LocalDensity.current) { BEAD_DIAMETER.toPx() }

    fun valueAtX(x: Float): Float = valueRange.start + travelFraction(x, width, beadPx, rtl) * span

    fun snap(raw: Float): Float {
        val clamped = raw.coerceIn(valueRange)
        if (steps <= 0 || span <= 0f) return clamped
        val step = span / (steps + 1)
        return (valueRange.start + ((clamped - valueRange.start) / step).roundToInt() * step).coerceIn(valueRange)
    }

    fun emit(raw: Float) {
        val next = snap(raw)
        if (next != safe) change(next)
    }

    Box(
        modifier
            .defaultMinSize(minWidth = 96.dp)
            .height(48.dp)
            .semantics {
                progressBarRangeInfo = ProgressBarRangeInfo(safe, valueRange, steps)
                if (!enabled) disabled()
                setProgress { target ->
                    if (!enabled) return@setProgress false
                    emit(target)
                    finished?.invoke()
                    true
                }
            }.focusable(enabled, interaction)
            .onPreviewKeyEvent { event ->
                if (!enabled || event.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                val step = if (steps > 0) span / (steps + 1) else span / KEY_STEPS
                val direction =
                    when (event.key) {
                        Key.DirectionRight, Key.DirectionUp -> if (rtl && event.key == Key.DirectionRight) -1 else 1
                        Key.DirectionLeft, Key.DirectionDown -> if (rtl && event.key == Key.DirectionLeft) 1 else -1
                        else -> return@onPreviewKeyEvent false
                    }
                emit(safe + direction * step)
                finished?.invoke()
                true
            }.pointerInput(enabled, valueRange, steps, rtl) {
                if (!enabled) return@pointerInput
                detectTapGestures(
                    onPress = {
                        dragging = true
                        tryAwaitRelease()
                        dragging = false
                    },
                    onTap = { position ->
                        emit(valueAtX(position.x))
                        finished?.invoke()
                    },
                )
            }.draggable(
                state =
                    rememberDraggableState { delta ->
                        dragX += delta
                        emit(valueAtX(dragX))
                    },
                orientation = Orientation.Horizontal,
                enabled = enabled,
                interactionSource = interaction,
                onDragStarted = { start ->
                    dragging = true
                    dragX = start.x
                },
                onDragStopped = {
                    dragging = false
                    finished?.invoke()
                },
            ).onSizeChanged { width = it.width.toFloat() }
            .drawBehind {
                val shown = if (rtl) 1f - fraction else fraction
                val flowing = if (rtl) 1f - liquid.value else liquid.value
                drawChannelWithBead(
                    renderer = renderer,
                    palette = palette,
                    head = shown,
                    fill = flowing,
                    fillFromEnd = rtl,
                    velocity = liquid.velocity,
                    pressure = pressure.value,
                    focused = focused,
                    enabled = enabled,
                    seed = seed,
                )
            },
    )
}

/** B09 capsule toggle: a bead that transfers between two stops of a recessed cradle. */
@Composable
fun OpalineToggle(
    checked: Boolean,
    onCheckedChange: ((Boolean) -> Unit)?,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    val palette = Opaline.palette
    val renderer = Opaline.renderer
    val interaction = remember { MutableInteractionSource() }
    val contact = rememberOpalineContact(interaction, enabled && onCheckedChange != null)
    val position = rememberOpalineSpring(if (checked) 1f else 0f, OpalineMotion.VALUE_FREQUENCY, OpalineMotion.VALUE_DAMPING)
    val rtl = LocalLayoutDirection.current == LayoutDirection.Rtl
    val seed = rememberOpalineSeed()
    Box(
        modifier
            .size(width = 64.dp, height = 48.dp)
            .then(
                if (onCheckedChange != null) {
                    Modifier.toggleable(checked, interaction, null, enabled = enabled, role = Role.Switch, onValueChange = onCheckedChange)
                } else {
                    Modifier
                },
            ).drawBehind {
                drawToggle(
                    renderer,
                    palette,
                    if (rtl) 1f - position.value else position.value,
                    position.value,
                    contact.pressure.value,
                    contact.focused,
                    enabled,
                    seed,
                )
            },
    )
}

/** A settings line with a toggle; the whole line is one switch for touch and assistive technology. */
@Composable
fun OpalineToggleRow(
    title: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
    subtitle: String? = null,
    enabled: Boolean = true,
) {
    val palette = Opaline.palette
    Row(
        modifier
            .fillMaxWidth()
            .defaultMinSize(minHeight = 56.dp)
            .toggleable(checked, enabled = enabled, role = Role.Switch, onValueChange = onCheckedChange)
            .alpha(if (enabled) 1f else DISABLED_ALPHA)
            .padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(title, style = MaterialTheme.typography.bodyLarge, color = palette.text)
            if (!subtitle.isNullOrBlank()) {
                Text(subtitle, style = MaterialTheme.typography.bodySmall, color = palette.textMuted)
            }
        }
        OpalineToggle(checked, null, enabled = enabled)
    }
}

/** A labelled slider with its current value shown as text, so the value never relies on position alone. */
@Composable
fun OpalineLabeledSlider(
    label: String,
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
    steps: Int = 0,
    enabled: Boolean = true,
    valueText: String = "%.2f".format(value),
    trailing: (@Composable () -> Unit)? = null,
    onValueChangeFinished: (() -> Unit)? = null,
) {
    val palette = Opaline.palette
    Column(modifier.fillMaxWidth().alpha(if (enabled) 1f else DISABLED_ALPHA)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(
                label,
                Modifier.weight(1f),
                style = MaterialTheme.typography.labelLarge,
                color = palette.text,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text(valueText, style = MaterialTheme.typography.labelMedium, color = palette.accent)
            trailing?.invoke()
        }
        OpalineSlider(
            value,
            onValueChange,
            Modifier.fillMaxWidth().semantics { stateDescription = valueText },
            valueRange,
            steps,
            enabled,
            onValueChangeFinished,
        )
    }
}

/**
 * B13 domed rotary dial: a gel cap over a collar whose lit arc shows the value. Vertical or
 * horizontal drag turns it; keyboard and accessibility actions adjust it in steps.
 */
@Composable
fun OpalineDial(
    value: Float,
    onValueChange: (Float) -> Unit,
    modifier: Modifier = Modifier,
    valueRange: ClosedFloatingPointRange<Float> = 0f..1f,
    enabled: Boolean = true,
    size: Dp = 76.dp,
    label: String? = null,
) {
    val palette = Opaline.palette
    val renderer = Opaline.renderer
    val span = valueRange.endInclusive - valueRange.start
    val safe = if (value.isFinite()) value.coerceIn(valueRange) else valueRange.start
    val fraction = if (span > 0f) (safe - valueRange.start) / span else 0f
    val change by rememberUpdatedState(onValueChange)
    val latest by rememberUpdatedState(safe)
    val interaction = remember { MutableInteractionSource() }
    val focused by interaction.collectIsFocusedAsState()
    var turning by remember { mutableStateOf(false) }
    val pressure = rememberOpalineSpring(if (turning) 1f else 0f)
    val shown = rememberOpalineSpring(fraction, OpalineMotion.VALUE_FREQUENCY, OpalineMotion.VALUE_DAMPING)
    val seed = rememberOpalineSeed()
    val dragPixels = with(LocalDensity.current) { DIAL_DRAG_SPAN.toPx() }
    Box(
        modifier
            .size(size)
            .semantics {
                progressBarRangeInfo = ProgressBarRangeInfo(safe, valueRange)
                if (!enabled) disabled()
                setProgress { target ->
                    if (!enabled) return@setProgress false
                    change(target.coerceIn(valueRange))
                    true
                }
            }.focusable(enabled, interaction)
            .onPreviewKeyEvent { event ->
                if (!enabled || event.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                val direction =
                    when (event.key) {
                        Key.DirectionRight, Key.DirectionUp -> 1
                        Key.DirectionLeft, Key.DirectionDown -> -1
                        else -> return@onPreviewKeyEvent false
                    }
                change((latest + direction * span / KEY_STEPS).coerceIn(valueRange))
                true
            }.pointerInput(enabled, valueRange) {
                if (!enabled) return@pointerInput
                var accumulated = 0f
                detectDragGestures(
                    onDragStart = {
                        turning = true
                        accumulated = latest
                    },
                    onDragEnd = { turning = false },
                    onDragCancel = { turning = false },
                ) { change, delta ->
                    change.consume()
                    accumulated = (accumulated + (delta.x - delta.y) * span / dragPixels).coerceIn(valueRange)
                    change(accumulated)
                }
            }.drawBehind { drawDial(renderer, palette, shown.value, pressure.value, focused, enabled, seed) },
        contentAlignment = Alignment.Center,
    ) {
        Text(
            label ?: "${(fraction * 100).roundToInt()}",
            Modifier.padding(bottom = OpalineBody.Puck.wall * 0.6f),
            style = MaterialTheme.typography.labelLarge,
            color = palette.onGel,
        )
    }
}

/** Progress as liquid filling a channel; `null` shows an indeterminate travelling body. */
@Composable
fun OpalineProgress(
    progress: Float?,
    modifier: Modifier = Modifier,
) {
    val palette = Opaline.palette
    val renderer = Opaline.renderer
    val reduced = Opaline.reducedMotion
    val travel =
        if (progress == null && !reduced) {
            rememberInfiniteTransition(label = "opalineProgress")
                .animateFloat(0f, 1f, infiniteRepeatable(tween(INDETERMINATE_MS, easing = LinearEasing), RepeatMode.Restart), label = "travel")
        } else {
            null
        }
    Box(
        modifier
            .fillMaxWidth()
            .height(16.dp)
            .semantics { progressBarRangeInfo = if (progress == null) ProgressBarRangeInfo.Indeterminate else ProgressBarRangeInfo(progress, 0f..1f) }
            .drawBehind {
                renderer.draw(this, palette, OpalineBody.Channel, 0f, center, 0f, 0f, 0f, 0f, 0f, 0f)
                val inner = 3.dp.toPx()
                val width = size.width - inner * 2f
                val (start, end) =
                    when {
                        progress != null -> 0f to progress.coerceIn(0f, 1f)
                        travel != null -> (travel.value * 1.3f - 0.3f).coerceIn(0f, 1f) to (travel.value * 1.3f).coerceIn(0f, 1f)
                        else -> 0f to 1f
                    }
                if (end - start > 0.001f) {
                    inset(inner + width * start, inner, inner + width * (1f - end), inner) {
                        renderer.draw(this, palette, liquidBody(palette), 0f, center, 0f, 0f, 0f, 0f, 0f, 0f)
                    }
                }
            },
    )
}

/**
 * B07 meniscus seek channel: liquid fills the channel to the playhead, where a displacement
 * body rides the surface. [waveform] is engraved in the channel floor. While scrubbing the head
 * follows the finger and [onScrub] reports the preview position; [onSeek] commits on release
 * or tap, so playback is not re-seeked on every movement.
 */
@Composable
fun OpalineSeekBar(
    fraction: Float,
    onSeek: (Float) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    waveform: FloatArray? = null,
    onScrub: ((Float?) -> Unit)? = null,
) {
    val palette = Opaline.palette
    val renderer = Opaline.renderer
    val seek by rememberUpdatedState(onSeek)
    val scrub by rememberUpdatedState(onScrub)
    var scrubbing by remember { mutableStateOf<Float?>(null) }
    val shown = (scrubbing ?: fraction).takeIf { it.isFinite() }?.coerceIn(0f, 1f) ?: 0f
    val interaction = remember { MutableInteractionSource() }
    val focused by interaction.collectIsFocusedAsState()
    val pressure = rememberOpalineSpring(if (scrubbing != null) 1f else 0f)
    val liquid = rememberOpalineSpring(shown, OpalineMotion.VALUE_FREQUENCY, OpalineMotion.VALUE_DAMPING)
    val rtl = LocalLayoutDirection.current == LayoutDirection.Rtl
    val seed = rememberOpalineSeed()
    var dragX by remember { mutableFloatStateOf(0f) }
    var width by remember { mutableFloatStateOf(0f) }
    val headPx = with(LocalDensity.current) { SEEK_HEAD.toPx() }

    fun at(x: Float): Float = travelFraction(x, width, headPx, rtl)

    Box(
        modifier
            .fillMaxWidth()
            .height(48.dp)
            .onSizeChanged { width = it.width.toFloat() }
            .semantics {
                progressBarRangeInfo = ProgressBarRangeInfo(shown, 0f..1f)
                if (!enabled) disabled()
                setProgress { target ->
                    if (!enabled) return@setProgress false
                    seek(target.coerceIn(0f, 1f))
                    true
                }
            }.focusable(enabled, interaction)
            .onPreviewKeyEvent { event ->
                if (!enabled || event.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                val direction =
                    when (event.key) {
                        Key.DirectionRight -> if (rtl) -1 else 1
                        Key.DirectionLeft -> if (rtl) 1 else -1
                        else -> return@onPreviewKeyEvent false
                    }
                seek((fraction + direction * SEEK_KEY_STEP).coerceIn(0f, 1f))
                true
            }.pointerInput(enabled, rtl) {
                if (!enabled) return@pointerInput
                detectTapGestures(onTap = { position -> seek(at(position.x)) })
            }.draggable(
                state =
                    rememberDraggableState { delta ->
                        dragX += delta
                        val next = at(dragX)
                        scrubbing = next
                        scrub?.invoke(next)
                    },
                orientation = Orientation.Horizontal,
                enabled = enabled,
                interactionSource = interaction,
                onDragStarted = { start ->
                    dragX = start.x
                    scrubbing = at(start.x)
                    scrub?.invoke(scrubbing)
                },
                onDragStopped = {
                    scrubbing?.let { seek(it) }
                    scrubbing = null
                    scrub?.invoke(null)
                },
            ).drawBehind {
                drawSeekChannel(
                    renderer = renderer,
                    palette = palette,
                    head = if (rtl) 1f - shown else shown,
                    fill = if (rtl) 1f - liquid.value else liquid.value,
                    fillFromEnd = rtl,
                    velocity = liquid.velocity,
                    pressure = pressure.value,
                    focused = focused,
                    enabled = enabled,
                    waveform = waveform,
                    seed = seed,
                )
            },
    )
}

/**
 * Where [x] falls along a track whose travelling body of width [body] stays wholly inside the
 * control: its centre runs from body/2 to width - body/2. Mirrored for right-to-left layouts.
 */
private fun travelFraction(
    x: Float,
    width: Float,
    body: Float,
    rtl: Boolean,
): Float {
    val travel = (width - body).coerceAtLeast(1f)
    val f = ((x - body / 2f) / travel).coerceIn(0f, 1f)
    return if (rtl) 1f - f else f
}

internal fun liquidBody(palette: OpalinePalette): OpalineBody = OpalineBody.Liquid.copy(color = lerp(palette.water, palette.accent, LIQUID_TOWARD_ACCENT))

private fun DrawScope.drawChannelWithBead(
    renderer: OpalineRenderer,
    palette: OpalinePalette,
    head: Float,
    fill: Float,
    fillFromEnd: Boolean,
    velocity: Float,
    pressure: Float,
    focused: Boolean,
    enabled: Boolean,
    seed: Float,
) {
    val bead = BEAD_DIAMETER.toPx()
    val channelHeight = CHANNEL_HEIGHT.toPx()
    val travelStart = bead / 2f
    val travel = size.width - bead
    val cy = size.height / 2f
    val disabled = if (enabled) 0f else 1f
    inset(travelStart - channelHeight / 2f, cy - channelHeight / 2f, travelStart - channelHeight / 2f, size.height - cy - channelHeight / 2f) {
        renderer.draw(this, palette, OpalineBody.Channel, 0f, center, 0f, 0f, 0f, disabled, 0f, seed)
    }
    val fillX = travelStart + travel * fill.coerceIn(0f, 1f)
    val inner = 3.dp.toPx()
    val left = if (fillFromEnd) fillX else travelStart - channelHeight / 2f + inner
    val right = if (fillFromEnd) size.width - travelStart + channelHeight / 2f - inner else fillX
    if (right - left > 1f) {
        inset(left, cy - channelHeight / 2f + inner, size.width - right, size.height - cy - channelHeight / 2f + inner) {
            renderer.draw(this, palette, liquidBody(palette), 0f, center, 0f, 0f, 0f, disabled, 0f, seed)
        }
    }
    val stretch = 1f + minOf(OpalineMotion.THUMB_STRETCH_MAX, abs(velocity) * OpalineMotion.THUMB_STRETCH_PER_VELOCITY)
    val squash = 1f + pressure.coerceIn(0f, 1f) * 0.08f
    val beadW = bead * stretch * squash
    val beadH = bead / sqrt(stretch) / squash + OpalineBody.Bead.wall.toPx()
    val x = travelStart + travel * head.coerceIn(0f, 1f)
    inset(x - beadW / 2f, cy - beadH / 2f, size.width - x - beadW / 2f, size.height - cy - beadH / 2f) {
        renderer.draw(
            this,
            palette,
            OpalineBody.Bead,
            pressure,
            Offset(size.width / 2f, size.height * 0.4f),
            pressure.coerceAtLeast(0f),
            if (pressure > 0.05f) 1f else 0f,
            if (focused) 1f else 0f,
            disabled,
            0f,
            seed,
        )
    }
}

private fun DrawScope.drawToggle(
    renderer: OpalineRenderer,
    palette: OpalinePalette,
    position: Float,
    checked: Float,
    pressure: Float,
    focused: Boolean,
    enabled: Boolean,
    seed: Float,
) {
    val cradle = Size(TOGGLE_WIDTH.toPx(), TOGGLE_HEIGHT.toPx())
    val left = (size.width - cradle.width) / 2f
    val top = (size.height - cradle.height) / 2f
    val disabled = if (enabled) 0f else 1f
    inset(left, top, size.width - left - cradle.width, size.height - top - cradle.height) {
        renderer.draw(this, palette, OpalineBody.Channel, 0f, center, 0f, 0f, if (focused) 1f else 0f, disabled, 0f, seed)
        val inner = 3.dp.toPx()
        if (checked > 0.02f) {
            val reach = (this.size.width - inner * 2f) * checked.coerceIn(0f, 1f)
            val start = if (position >= checked - 0.001f || position > 0.5f) 0f else this.size.width - inner * 2f - reach
            inset(inner + start, inner, this.size.width - inner - start - reach, inner) {
                renderer.draw(this, palette, liquidBody(palette), 0f, center, 0f, 0f, 0f, disabled, 0f, seed)
            }
        }
    }
    val bead = TOGGLE_BEAD.toPx()
    val travelStart = left + TOGGLE_HEIGHT.toPx() / 2f
    val travel = cradle.width - TOGGLE_HEIGHT.toPx()
    val x = travelStart + travel * position.coerceIn(0f, 1f)
    val squash = 1f + pressure.coerceIn(0f, 1f) * 0.14f
    val beadW = bead * squash
    val beadH = bead / squash + OpalineBody.Bead.wall.toPx()
    val cy = size.height / 2f
    val beadBody =
        if (checked > 0.5f) {
            OpalineBody.Bead
        } else {
            OpalineBody.Bead.copy(material = OpalineMaterial.STONE, color = lerp(palette.slab, palette.gel, 0.4f))
        }
    inset(x - beadW / 2f, cy - beadH / 2f, size.width - x - beadW / 2f, size.height - cy - beadH / 2f) {
        renderer.draw(this, palette, beadBody, pressure, Offset(size.width / 2f, size.height * 0.4f), 0f, checked, 0f, disabled, 0f, seed)
    }
}

private fun DrawScope.drawDial(
    renderer: OpalineRenderer,
    palette: OpalinePalette,
    fraction: Float,
    pressure: Float,
    focused: Boolean,
    enabled: Boolean,
    seed: Float,
) {
    val stroke = DIAL_COLLAR.toPx()
    val arcInset = stroke / 2f + 1.dp.toPx()
    val arcSize = Size(size.width - arcInset * 2f, size.height - arcInset * 2f)
    val topLeft = Offset(arcInset, arcInset)
    drawArc(palette.environmentLow, DIAL_START, DIAL_SWEEP, false, topLeft, arcSize, style = Stroke(stroke, cap = StrokeCap.Round))
    drawArc(
        if (enabled) palette.accent else palette.textMuted,
        DIAL_START,
        DIAL_SWEEP * fraction.coerceIn(0f, 1f),
        false,
        topLeft,
        arcSize,
        style = Stroke(stroke * 0.7f, cap = StrokeCap.Round),
    )
    val cap = stroke + 5.dp.toPx()
    inset(cap, cap, cap, cap) {
        renderer.draw(
            this,
            palette,
            OpalineBody.Puck.copy(height = 8.dp, wall = 5.dp, dome = 6.dp),
            pressure,
            center,
            pressure.coerceAtLeast(0f),
            0f,
            if (focused) 1f else 0f,
            if (enabled) 0f else 1f,
            0f,
            seed,
        )
    }
    val angle = Math.toRadians((DIAL_START + DIAL_SWEEP * fraction).toDouble())
    val radius = size.minDimension / 2f - cap - 7.dp.toPx()
    val c = Offset(size.width / 2f, (size.height - 5.dp.toPx()) / 2f)
    drawCircle(
        lerp(palette.onGel, Color.Transparent, 0.2f),
        radius = 3.dp.toPx(),
        center = c + Offset((cos(angle) * radius).toFloat(), (sin(angle) * radius).toFloat()),
    )
}

private fun DrawScope.drawSeekChannel(
    renderer: OpalineRenderer,
    palette: OpalinePalette,
    head: Float,
    fill: Float,
    fillFromEnd: Boolean,
    velocity: Float,
    pressure: Float,
    focused: Boolean,
    enabled: Boolean,
    waveform: FloatArray?,
    seed: Float,
) {
    val channelHeight = SEEK_CHANNEL.toPx()
    val cy = size.height / 2f
    val top = cy - channelHeight / 2f
    val disabled = if (enabled) 0f else 1f
    inset(0f, top, 0f, size.height - top - channelHeight) {
        renderer.draw(this, palette, OpalineBody.Channel, 0f, center, 0f, 0f, if (focused) 1f else 0f, disabled, 0f, seed)
    }
    val inner = 3.dp.toPx()
    val headSize = SEEK_HEAD.toPx()
    val travel = size.width - headSize
    val headX = headSize / 2f + travel * head.coerceIn(0f, 1f)
    val fillX = headSize / 2f + travel * fill.coerceIn(0f, 1f)
    val left = if (fillFromEnd) fillX else inner
    val right = if (fillFromEnd) size.width - inner else fillX
    if (right - left > 1f) {
        inset(left, top + inner, size.width - right, size.height - top - channelHeight + inner) {
            renderer.draw(this, palette, liquidBody(palette), 0f, center, 0f, 0f, 0f, disabled, 0f, seed)
        }
    }
    if (waveform != null && waveform.isNotEmpty()) {
        val bars = WAVEFORM_BARS
        val step = (size.width - inner * 4f) / bars
        val maxBar = channelHeight - inner * 3f
        for (i in 0 until bars) {
            val amplitude = waveform[i * waveform.size / bars].coerceIn(0.08f, 1f)
            val x = inner * 2f + step * (i + 0.5f)
            val played = if (fillFromEnd) x >= headX else x <= headX
            drawLine(
                if (played) palette.ink.copy(alpha = 0.55f) else palette.text.copy(alpha = 0.28f),
                Offset(x, cy - maxBar * amplitude / 2f),
                Offset(x, cy + maxBar * amplitude / 2f),
                strokeWidth = (step * 0.45f).coerceAtMost(2.5f.dp.toPx()),
                cap = StrokeCap.Round,
            )
        }
    }
    val stretch = 1f + minOf(OpalineMotion.THUMB_STRETCH_MAX, abs(velocity) * OpalineMotion.THUMB_STRETCH_PER_VELOCITY)
    val w = headSize * stretch
    val h = headSize / sqrt(stretch) + OpalineBody.Bead.wall.toPx()
    val x = headX.coerceIn(w / 2f, size.width - w / 2f)
    inset(x - w / 2f, cy - h / 2f, size.width - x - w / 2f, size.height - cy - h / 2f) {
        renderer.draw(
            this,
            palette,
            OpalineBody.Bead.copy(material = OpalineMaterial.WATER, color = lerp(palette.gel, Color.White, 0.35f), receiver = 0.5f),
            pressure,
            Offset(size.width / 2f, size.height * 0.4f),
            pressure.coerceAtLeast(0f),
            0f,
            0f,
            disabled,
            0f,
            seed,
        )
    }
}

private val BEAD_DIAMETER = 26.dp
private val CHANNEL_HEIGHT = 14.dp
private val TOGGLE_WIDTH = 56.dp
private val TOGGLE_HEIGHT = 30.dp
private val TOGGLE_BEAD = 24.dp
private val DIAL_COLLAR = 7.dp
private val DIAL_DRAG_SPAN = 220.dp
private val SEEK_CHANNEL = 22.dp
private val SEEK_HEAD = 30.dp
private const val DIAL_START = 135f
private const val DIAL_SWEEP = 270f
private const val KEY_STEPS = 20f
private const val SEEK_KEY_STEP = 0.02f
private const val WAVEFORM_BARS = 64
private const val INDETERMINATE_MS = 1400
private const val LIQUID_TOWARD_ACCENT = 0.55f
