package dev.geode.ui.glass

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.BlendMode
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.ContentDrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.nestedscroll.NestedScrollConnection
import androidx.compose.ui.input.nestedscroll.NestedScrollSource
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.layout.positionInRoot
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.unit.dp
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.performStoneHaptic
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlin.math.abs
import kotlin.math.hypot

/** One expanding ripple ring, alive from press or release until its animation finishes. */
private class GlassRipple(
    val origin: Offset,
) {
    val progress = Animatable(0f)
}

/** All the animated state a [waterTouch] instance owns, remembered once so the draw phase never
 * allocates. */
private class GlassTouchState {
    val pressGlow = Animatable(0f)
    val ripples = mutableStateListOf<GlassRipple>()
    val inkProgress = Animatable(0f)
    var inkOrigin: Offset = Offset.Zero
    val dragScaleX = Animatable(1f)
    val dragScaleY = Animatable(1f)
}

private val inkArcColors =
    listOf(GlassPalette.mint, GlassPalette.lavender, GlassPalette.peach, GlassPalette.pink, GlassPalette.sky)
private val dyeCycle = inkArcColors

/**
 * The four liquid-glass touch behaviours (docs/design/liquid-glass/README.md "Touch"): press =
 * `tap` ripple + local glow bloom, hold >= 350 ms = a rainbow ink swirl and, while a shared
 * [WaterField] is available via [LocalWaterField], a continuing dye `splat`; drag (when [drag] is
 * true) = the element stretches toward the finger and, with a field, splats ink along the path;
 * release = one last ripple / a half-strength `tap`. Honours
 * [dev.geode.ui.GuiPrefs.reducedMotion] via [LocalGlass]: ripple only, no ink, no float, no drag
 * stretch, no field coupling.
 */
fun Modifier.waterTouch(
    enabled: Boolean = true,
    onClick: (() -> Unit)? = null,
    onLongPress: (() -> Unit)? = null,
    drag: Boolean = false,
): Modifier =
    composed {
        if (!enabled) return@composed this
        val state = remember { GlassTouchState() }
        val scope = rememberCoroutineScope()
        val view = LocalView.current
        val reducedMotion = LocalGlass.current.reducedMotion
        val field = LocalWaterField.current
        val currentOnClick = rememberUpdatedState(onClick)
        val currentOnLongPress = rememberUpdatedState(onLongPress)
        var coords by remember { mutableStateOf<LayoutCoordinates?>(null) }

        this
            .onGloballyPositioned { coords = it }
            .pointerInput(drag, reducedMotion) {
                awaitEachGesture {
                    val down = awaitFirstDown(requireUnconsumed = false)
                    view.performStoneHaptic(StoneHapticCue.TAP)
                    val rootDown = coords?.localToRoot(down.position) ?: down.position
                    startPress(scope, state, down.position, reducedMotion)
                    field?.tap(rootDown.x, rootDown.y, TAP_STRENGTH)
                    val holdJob =
                        if (!reducedMotion && field != null) {
                            scope.launch { splatWhileHeld(field, coords, down.position) }
                        } else {
                            null
                        }
                    val outcome =
                        trackGesture(down.id, down.position, drag && !reducedMotion, state, scope, field, coords)
                    holdJob?.cancel()
                    finishPress(scope, state, outcome.lastPosition, drag && !reducedMotion)
                    val rootUp = coords?.localToRoot(outcome.lastPosition) ?: outcome.lastPosition
                    field?.tap(rootUp.x, rootUp.y, TAP_STRENGTH * RELEASE_STRENGTH_FACTOR)
                    if (outcome.longPressed) {
                        currentOnLongPress.value?.invoke()
                    } else if (outcome.released) {
                        currentOnClick.value?.invoke()
                    }
                }
            }.graphicsLayer {
                scaleX = state.dragScaleX.value
                scaleY = state.dragScaleY.value
            }.drawWithContent { drawGlassTouch(state) }
    }

/** Kept as the name screens call: identical to [waterTouch], the design the addendum supersedes. */
fun Modifier.glassTouch(
    enabled: Boolean = true,
    onClick: (() -> Unit)? = null,
    onLongPress: (() -> Unit)? = null,
    drag: Boolean = false,
): Modifier = waterTouch(enabled, onClick, onLongPress, drag)

private suspend fun splatWhileHeld(
    field: WaterField,
    coordsProvider: LayoutCoordinates?,
    localOrigin: Offset,
) {
    delay(GlassMotion.HOLD_THRESHOLD_MS)
    var i = 0
    while (true) {
        val root = coordsProvider?.localToRoot(localOrigin) ?: localOrigin
        field.splat(root.x, root.y, 0f, -HOLD_SPLAT_LIFT_PX, dyeCycle[i % dyeCycle.size])
        i++
        delay(HOLD_SPLAT_INTERVAL_MS)
    }
}

private fun startPress(
    scope: CoroutineScope,
    state: GlassTouchState,
    origin: Offset,
    reducedMotion: Boolean,
) {
    scope.launch { state.pressGlow.animateTo(1f, tween(120)) }
    addRipple(scope, state, origin)
    if (!reducedMotion) {
        scope.launch {
            delay(GlassMotion.HOLD_THRESHOLD_MS)
            state.inkOrigin = origin
            state.inkProgress.snapTo(0f)
            state.inkProgress.animateTo(1f, tween(GlassMotion.INK_DURATION_MS))
        }
    }
}

private fun addRipple(
    scope: CoroutineScope,
    state: GlassTouchState,
    origin: Offset,
) {
    val ripple = GlassRipple(origin)
    state.ripples.add(ripple)
    scope.launch {
        ripple.progress.animateTo(1f, tween(GlassMotion.RIPPLE_DURATION_MS))
        state.ripples.remove(ripple)
    }
}

private fun finishPress(
    scope: CoroutineScope,
    state: GlassTouchState,
    lastPosition: Offset,
    stretching: Boolean,
) {
    addRipple(scope, state, lastPosition)
    scope.launch { state.pressGlow.animateTo(0f, tween(220)) }
    if (stretching) {
        val wobble = spring<Float>(dampingRatio = Spring.DampingRatioMediumBouncy, stiffness = GlassMotion.SPRING_STIFFNESS)
        scope.launch { state.dragScaleX.animateTo(1f, wobble) }
        scope.launch { state.dragScaleY.animateTo(1f, wobble) }
    }
}

private class GestureOutcome(
    val released: Boolean,
    val longPressed: Boolean,
    val lastPosition: Offset,
)

/** Tracks one pointer from down to up/cancel, applying the liquid-drop stretch and splatting ink
 * along the path while dragging. An extension on [AwaitPointerEventScope] (rather than taking one
 * as a plain parameter) because that scope's suspend functions are restricted: only its own
 * members/extensions may be called from within [awaitEachGesture]. */
private suspend fun AwaitPointerEventScope.trackGesture(
    pointerId: PointerId,
    origin: Offset,
    stretching: Boolean,
    state: GlassTouchState,
    scope: CoroutineScope,
    field: WaterField?,
    coordsProvider: LayoutCoordinates?,
): GestureOutcome {
    var lastPosition = origin
    var longPressed = false
    var dyeIndex = 0
    val holdDeadline = System.currentTimeMillis() + GlassMotion.HOLD_THRESHOLD_MS
    while (true) {
        val event = awaitPointerEvent()
        val change = event.changes.firstOrNull { it.id == pointerId } ?: return GestureOutcome(false, longPressed, lastPosition)
        if (!change.pressed) {
            return GestureOutcome(true, longPressed, change.position)
        }
        val delta = change.position - lastPosition
        lastPosition = change.position
        if (System.currentTimeMillis() >= holdDeadline) longPressed = true
        if (stretching) {
            applyDragStretch(scope, state, change.position - origin)
            if (field != null && delta.getDistance() > 0.5f) {
                val root = coordsProvider?.localToRoot(change.position) ?: change.position
                field.splat(root.x, root.y, delta.x, delta.y, dyeCycle[dyeIndex % dyeCycle.size])
                dyeIndex++
            }
        }
        change.consume()
    }
}

private fun applyDragStretch(
    scope: CoroutineScope,
    state: GlassTouchState,
    delta: Offset,
) {
    val dx = abs(delta.x)
    val dy = abs(delta.y)
    val axisTotal = dx + dy
    val distanceT = (hypot(delta.x, delta.y) / STRETCH_DISTANCE_PX).coerceIn(0f, 1f) * (GlassMotion.DROP_STRETCH_MAX - 1f)
    val scaleX = 1f + distanceT * if (axisTotal > 0f) dx / axisTotal else 0.5f
    val scaleY = 1f + distanceT * if (axisTotal > 0f) dy / axisTotal else 0.5f
    scope.launch { if (scope.isActive) state.dragScaleX.snapTo(scaleX) }
    scope.launch { if (scope.isActive) state.dragScaleY.snapTo(scaleY) }
}

/**
 * Floats an element on the shared [WaterField]: a small translation/rotation/scale sampled from
 * the height field's gradient/value at the element's centre, every frame. A no-op where no field
 * is provided (see [LocalWaterField]) or under reduced motion.
 */
fun Modifier.floatOnWater(strength: Float = 1f): Modifier =
    composed {
        val field = LocalWaterField.current
        val reducedMotion = LocalGlass.current.reducedMotion
        if (field == null || reducedMotion || strength <= 0f) return@composed this
        val density = LocalDensity.current
        val translatePx = with(density) { FLOAT_TRANSLATE_DP.dp.toPx() }
        var center by remember { mutableStateOf(Offset.Zero) }
        var tx by remember { mutableFloatStateOf(0f) }
        var ty by remember { mutableFloatStateOf(0f) }
        var rot by remember { mutableFloatStateOf(0f) }
        var scl by remember { mutableFloatStateOf(1f) }
        LaunchedEffect(field, strength) {
            while (isActive) {
                withFrameNanos {
                    val c = center
                    val grad = field.gradientAt(c.x, c.y)
                    val h = field.heightAt(c.x, c.y)
                    tx = grad.x * translatePx * strength
                    ty = grad.y * translatePx * strength
                    rot = (grad.x - grad.y) * FLOAT_ROTATE_DEG * strength
                    scl = 1f + FLOAT_SCALE_GAIN * h * strength
                }
            }
        }
        this
            .onGloballyPositioned { c -> center = c.positionInRoot() + Offset(c.size.width / 2f, c.size.height / 2f) }
            .graphicsLayer {
                translationX = tx
                translationY = ty
                rotationZ = rot
                scaleX = scl
                scaleY = scl
            }
    }

/** Injects a wave into the shared [WaterField] along the scroll direction as a list scrolls. */
fun Modifier.waterScroll(): Modifier =
    composed {
        val field = LocalWaterField.current ?: return@composed this
        val reducedMotion = LocalGlass.current.reducedMotion
        if (reducedMotion) return@composed this
        var center by remember { mutableStateOf(Offset.Zero) }
        val connection =
            remember(field) {
                object : NestedScrollConnection {
                    override fun onPreScroll(
                        available: Offset,
                        source: NestedScrollSource,
                    ): Offset {
                        if (available.getDistance() > 0.5f) {
                            field.splat(
                                center.x,
                                center.y,
                                available.x * SCROLL_SPLAT_GAIN,
                                available.y * SCROLL_SPLAT_GAIN,
                                GlassPalette.sky,
                            )
                        }
                        return Offset.Zero
                    }
                }
            }
        this
            .onGloballyPositioned { c -> center = c.positionInRoot() + Offset(c.size.width / 2f, c.size.height / 2f) }
            .nestedScroll(connection)
    }

private fun ContentDrawScope.drawGlassTouch(state: GlassTouchState) {
    drawContent()
    drawPressGlow(state)
    state.ripples.forEach { drawRippleRings(it) }
    if (state.inkProgress.value in 0f..0.999f) drawInkSwirl(state)
}

private fun ContentDrawScope.drawPressGlow(state: GlassTouchState) {
    val alpha = state.pressGlow.value
    if (alpha < 0.01f) return
    val radius = size.minDimension * 0.75f
    drawCircle(
        brush = Brush.radialGradient(listOf(Color.White.copy(alpha = 0.35f * alpha), Color.Transparent), center, radius),
        radius = radius,
        center = center,
        blendMode = BlendMode.Plus,
    )
}

private fun ContentDrawScope.drawRippleRings(ripple: GlassRipple) {
    val p = ripple.progress.value
    val maxRadius = size.minDimension * 0.9f
    val ringCount = 3
    for (i in 0 until ringCount) {
        val lag = i * 0.12f
        val t = (p - lag).coerceIn(0f, 1f)
        if (t <= 0f) continue
        val radius = (maxRadius * t).coerceAtLeast(1f)
        val alpha = (1f - t) * 0.35f
        drawCircle(
            color = GlassPalette.glassRim.copy(alpha = alpha),
            radius = radius,
            center = ripple.origin,
            style = Stroke(width = 1.5.dp.toPx()),
        )
    }
}

private fun ContentDrawScope.drawInkSwirl(state: GlassTouchState) {
    val p = state.inkProgress.value
    val fade = (1f - p).coerceIn(0f, 1f)
    val maxRadius = size.minDimension * 0.9f
    inkArcColors.forEachIndexed { i, color ->
        val radius = maxRadius * (0.25f + 0.18f * i) * (0.4f + 0.6f * p)
        val rect = Rect(center = state.inkOrigin, radius = radius)
        drawArc(
            color = color.copy(alpha = 0.32f * fade),
            startAngle = i * 72f + p * 220f,
            sweepAngle = 46f,
            useCenter = false,
            topLeft = rect.topLeft,
            size = rect.size,
            style = Stroke(width = 3.dp.toPx()),
            blendMode = BlendMode.Plus,
        )
    }
}

private const val STRETCH_DISTANCE_PX = 220f
private const val TAP_STRENGTH = 6f
private const val RELEASE_STRENGTH_FACTOR = 0.5f
private const val HOLD_SPLAT_INTERVAL_MS = 90L
private const val HOLD_SPLAT_LIFT_PX = 6f
private const val FLOAT_TRANSLATE_DP = 6f
private const val FLOAT_ROTATE_DEG = 2f
private const val FLOAT_SCALE_GAIN = 0.02f
private const val SCROLL_SPLAT_GAIN = 0.5f
