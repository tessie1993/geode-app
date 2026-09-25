package dev.geode.ui.opaline

import android.graphics.Bitmap
import android.graphics.BitmapShader
import android.graphics.RuntimeShader
import android.graphics.Shader
import android.os.Build
import androidx.annotation.RequiresApi
import androidx.compose.foundation.interaction.FocusInteraction
import androidx.compose.foundation.interaction.InteractionSource
import androidx.compose.foundation.interaction.PressInteraction
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.Stable
import androidx.compose.runtime.currentCompositeKeyHash
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.geometry.isSpecified
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.ImageShader
import androidx.compose.ui.graphics.ShaderBrush
import androidx.compose.ui.graphics.TileMode
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * The physical description of one body: material family, footprint rounding, shoulder height,
 * how much sidewall shows below the top face, crown dome, and whether it is a raised body or a
 * recessed well. [color] defaults to the palette's colour for [material]; [receiver] scales the
 * contact shadow on the receiving plane.
 */
@Immutable
data class OpalineBody(
    val material: OpalineMaterial = OpalineMaterial.GEL,
    val color: Color = Color.Unspecified,
    val radius: Dp = Dp.Infinity,
    val height: Dp = 6.dp,
    val wall: Dp = 3.dp,
    val dome: Dp = 0.dp,
    val recessed: Boolean = false,
    val receiver: Float = 0.55f,
) {
    companion object {
        /** A01/A05: gel pebble or capsule for text actions. */
        val Action = OpalineBody(OpalineMaterial.GEL, height = 7.dp, wall = 4.dp, dome = 3.dp, receiver = 0.6f)

        /** A03: round lens for icon actions. */
        val Lens = OpalineBody(OpalineMaterial.GEL, height = 7.dp, wall = 3.dp, dome = 5.dp, receiver = 0.55f)

        /** A22: the soft puck, thick sidewall and domed cap; the primary play action. */
        val Puck = OpalineBody(OpalineMaterial.GEL, height = 12.dp, wall = 8.dp, dome = 7.dp, receiver = 0.8f)

        /** C01/C02: floating wide slab for list rows and cards. */
        val Slab = OpalineBody(OpalineMaterial.STONE, radius = 22.dp, height = 5.dp, wall = 4.dp, receiver = 0.5f)

        /** C03: deeper portrait slab for panels, sheets and dialogs. */
        val Panel = OpalineBody(OpalineMaterial.STONE, radius = 28.dp, height = 7.dp, wall = 5.dp, receiver = 0.55f)

        /** C13: an inset bed with a quiet front, for editable text. */
        val Well = OpalineBody(OpalineMaterial.STONE, radius = 18.dp, height = 5.dp, wall = 0.dp, recessed = true, receiver = 0f)

        /** B01 guide: the recessed channel a thumb or liquid travels in. */
        val Channel = OpalineBody(OpalineMaterial.STONE, height = 4.dp, wall = 0.dp, recessed = true, receiver = 0f)

        /** B09 bead and B01 thumb: a pearl body that travels between stops. */
        val Bead = OpalineBody(OpalineMaterial.NACRE, height = 8.dp, wall = 2.dp, dome = 6.dp, receiver = 0.7f)

        /** B07 liquid: the water body filling a channel. */
        val Liquid = OpalineBody(OpalineMaterial.WATER, height = 3.dp, wall = 0.dp, dome = 1.dp, receiver = 0f)

        /** D03: the crescent dock's shared support. */
        val Dock = OpalineBody(OpalineMaterial.STONE, radius = 32.dp, height = 8.dp, wall = 6.dp, receiver = 0.7f)

        /** C20: the thick circular frame around artwork. */
        val Ring = OpalineBody(OpalineMaterial.BLUE, height = 18.dp, wall = 6.dp, dome = 4.dp, receiver = 0.75f)
    }
}

/**
 * Pointer contact for one body, taken from the host control's [InteractionSource]: the control
 * keeps its own gesture handling and semantics, and the body only observes press position,
 * release and cancel. Pressure follows `MotionController`'s spring, so release overshoots
 * briefly like the library's elastic preview; with reduced motion it settles immediately.
 */
@Stable
class OpalineContact internal constructor(
    internal val pressure: OpalineSpringState,
    private val pressedState: MutableState<Boolean>,
) {
    var point by mutableStateOf(Offset.Unspecified)
        internal set
    var focused by mutableStateOf(false)
        internal set
    var pressed: Boolean
        get() = pressedState.value
        internal set(value) {
            pressedState.value = value
        }
}

@Composable
fun rememberOpalineContact(
    interactionSource: InteractionSource?,
    enabled: Boolean = true,
): OpalineContact {
    val pressed = remember { mutableStateOf(false) }
    val pressure = rememberOpalineSpring(if (pressed.value && enabled) 1f else 0f)
    val contact = remember(pressure) { OpalineContact(pressure, pressed) }
    LaunchedEffect(interactionSource) {
        interactionSource ?: return@LaunchedEffect
        var presses = 0
        var focuses = 0
        interactionSource.interactions.collect { interaction ->
            when (interaction) {
                is PressInteraction.Press -> {
                    presses++
                    contact.point = interaction.pressPosition
                }
                is PressInteraction.Release, is PressInteraction.Cancel -> presses = (presses - 1).coerceAtLeast(0)
                is FocusInteraction.Focus -> focuses++
                is FocusInteraction.Unfocus -> focuses = (focuses - 1).coerceAtLeast(0)
            }
            contact.pressed = presses > 0
            contact.focused = focuses > 0
        }
    }
    return contact
}

/**
 * Draws [body] behind this element's content. Readable content is never deformed: the dent,
 * highlights and shadow move while text and icons stay on their stable front plane. The lambdas
 * are read while drawing, so an animating body redraws without recomposing; [flowTime] advances
 * the interior's flow field and is only non-zero for a deliberately animated hero.
 */
fun Modifier.opalineBody(
    body: OpalineBody,
    palette: OpalinePalette,
    renderer: OpalineRenderer,
    contact: OpalineContact? = null,
    enabled: Boolean = true,
    seed: Float = 0f,
    selected: () -> Float = { 0f },
    excitation: () -> Float = { 0f },
    flowTime: () -> Float = { 0f },
): Modifier =
    drawBehind {
        val pressure = contact?.pressure?.value ?: 0f
        val point =
            contact?.point?.takeIf { it.isSpecified } ?: Offset(size.width / 2f, (size.height - body.wall.toPx()) / 2f)
        renderer.draw(
            scope = this,
            palette = palette,
            body = body,
            pressure = pressure,
            contact = point,
            excitation = maxOf(excitation(), pressure.coerceAtLeast(0f)),
            selected = selected(),
            focus = if (contact?.focused == true) 1f else 0f,
            disabled = if (enabled) 0f else 1f,
            time = flowTime(),
            seed = seed,
        )
    }

/**
 * The body modifier for a kit component: current theme, a stable interior seed and an animated
 * selection state. Components pass the contact of their own interaction source.
 */
@Composable
fun rememberOpalineSurface(
    body: OpalineBody,
    contact: OpalineContact? = null,
    selected: Boolean = false,
    enabled: Boolean = true,
    excitation: () -> Float = { 0f },
    flowTime: () -> Float = { 0f },
): Modifier {
    val palette = Opaline.palette
    val renderer = Opaline.renderer
    val seed = rememberOpalineSeed()
    val selection = rememberOpalineSpring(if (selected) 1f else 0f, OpalineMotion.VALUE_FREQUENCY, OpalineMotion.VALUE_DAMPING)
    return Modifier.opalineBody(
        body = body,
        palette = palette,
        renderer = renderer,
        contact = contact,
        enabled = enabled,
        seed = seed,
        selected = { selection.value },
        excitation = excitation,
        flowTime = flowTime,
    )
}

/**
 * For actions without a body: host focus still shows as a ring, and contact brightens a soft
 * disc under the finger, so no control loses its feedback by dropping its shell.
 */
internal fun Modifier.drawQuietFocus(
    contact: OpalineContact,
    ring: Color,
    highlight: Color,
): Modifier =
    drawBehind {
        val corner = CornerRadius(minOf(size.width, size.height) / 2f)
        val pressure = contact.pressure.value.coerceIn(0f, 1f)
        if (pressure > 0f) drawRoundRect(highlight.copy(alpha = QUIET_PRESS_ALPHA * pressure), cornerRadius = corner)
        if (contact.focused) drawRoundRect(ring, cornerRadius = corner, style = Stroke(2f * density))
    }

private const val QUIET_PRESS_ALPHA = 0.12f

/** A stable per-call-site seed, so a body keeps its interior pattern when it is recomposed. */
@Composable
fun rememberOpalineSeed(): Float {
    val key = currentCompositeKeyHash
    return remember(key) { ((key.toLong() and 0xFFFF) / 65535f) * SEED_SPAN }
}

private const val SEED_SPAN = 256f

/**
 * Owns the one compiled body program and the baked cloud texture. There is one per process
 * ([shared]); every draw happens on the main thread and sets its uniforms immediately before
 * drawing, so all bodies share one program. AGSL runs on API 33+; below that, or if the program
 * cannot be created, bodies use [drawFallback], which keeps the same anatomy (shadow, sidewall,
 * shaded face, rim, contact glow) with gradients.
 */
@Stable
class OpalineRenderer internal constructor(
    allowShader: Boolean = true,
) {
    private var cloud by mutableStateOf<Bitmap?>(null)
    private var cloudImage: ImageBitmap? = null
    private val agsl: Any? =
        if (allowShader && Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            runCatching { AgslBody() }.getOrNull()
        } else {
            null
        }

    val usesShader: Boolean get() = agsl != null

    internal suspend fun bake() {
        if (cloud != null) return
        val bitmap = withContext(Dispatchers.Default) { OpalineTextures.cloud() }
        if (cloud != null) return
        cloudImage = bitmap.asImageBitmap()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) (agsl as? AgslBody)?.setCloud(bitmap)
        cloud = bitmap
    }

    companion object {
        val shared: OpalineRenderer by lazy { OpalineRenderer() }
    }

    internal fun draw(
        scope: DrawScope,
        palette: OpalinePalette,
        body: OpalineBody,
        pressure: Float,
        contact: Offset,
        excitation: Float,
        selected: Float,
        focus: Float,
        disabled: Float,
        time: Float,
        seed: Float,
    ) {
        if (scope.size.width < 1f || scope.size.height < 1f) return
        // Reading the texture state makes every body redraw once when the bake lands; until then
        // the program samples a flat mid-grey cloud.
        val bakedCloud = cloud?.let { cloudImage }
        val shader = agsl
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU && shader is AgslBody) {
            shader.draw(scope, palette, body, pressure, contact, excitation, selected, focus, disabled, time, seed)
        } else {
            scope.drawFallback(palette, body, pressure, contact, excitation, selected, focus, disabled, bakedCloud)
        }
    }
}

internal fun OpalinePalette.bodyColor(body: OpalineBody): Color =
    if (body.color != Color.Unspecified) {
        body.color
    } else {
        when (body.material) {
            OpalineMaterial.GEL, OpalineMaterial.NACRE -> gel
            OpalineMaterial.BLUE, OpalineMaterial.PIGMENT -> blue
            OpalineMaterial.WATER -> WATER_BODY
            OpalineMaterial.SHELL -> SHELL_BODY
            OpalineMaterial.FILM -> FILM_BODY
            OpalineMaterial.STONE -> slab
            OpalineMaterial.GLOW -> accent
        }
    }

// `materials.js` gives water, shell and film fixed near-white bodies tinted by attenuation.
private val WATER_BODY = Color(0xFFE1F8FA)
private val SHELL_BODY = Color(0xFFF2FBFF)
private val FILM_BODY = Color(0xFFF8FDFF)

private fun Dp.resolveRadius(
    scope: DrawScope,
    width: Float,
    height: Float,
): Float {
    val limit = minOf(width, height) / 2f
    return if (this == Dp.Infinity) limit else with(scope) { this@resolveRadius.toPx() }.coerceAtMost(limit)
}

@RequiresApi(Build.VERSION_CODES.TIRAMISU)
private class AgslBody {
    private val shader = RuntimeShader(OPALINE_BODY_AGSL)
    private val paint = android.graphics.Paint()
    private val key = OpalineLight.key
    private val fill = OpalineLight.fill
    private val keyColor = OpalineLight.keyColor.toArgb()
    private val fillColor = OpalineLight.fillColor.toArgb()
    private val rimColor = OpalineLight.rimColor.toArgb()

    init {
        shader.setInputShader("uCloud", BitmapShader(PLACEHOLDER, Shader.TileMode.REPEAT, Shader.TileMode.REPEAT))
        paint.isAntiAlias = false
    }

    fun setCloud(bitmap: Bitmap) {
        val child = BitmapShader(bitmap, Shader.TileMode.REPEAT, Shader.TileMode.REPEAT)
        child.filterMode = BitmapShader.FILTER_MODE_LINEAR
        shader.setInputShader("uCloud", child)
    }

    fun draw(
        scope: DrawScope,
        palette: OpalinePalette,
        body: OpalineBody,
        pressure: Float,
        contact: Offset,
        excitation: Float,
        selected: Float,
        focus: Float,
        disabled: Float,
        time: Float,
        seed: Float,
    ) = with(scope) {
        val w = size.width
        val h = size.height
        val wall = body.wall.toPx().coerceAtMost(h * 0.4f)
        val height = body.height.toPx()
        val material = body.material
        val theme = palette.theme
        shader.setFloatUniform("uSize", w, h)
        shader.setFloatUniform("uRadius", body.radius.resolveRadius(this, w, h - wall))
        shader.setFloatUniform("uHeight", height)
        shader.setFloatUniform("uWall", wall)
        shader.setFloatUniform("uDome", body.dome.toPx())
        shader.setFloatUniform("uRecess", if (body.recessed) 1f else 0f)
        shader.setFloatUniform("uContact", contact.x, contact.y, pressure)
        shader.setFloatUniform("uState", excitation, selected, focus, time)
        shader.setFloatUniform(
            "uMat",
            material.coverage,
            material.cloud(theme),
            material.iridescence,
            material.clearcoatRoughness(theme),
        )
        shader.setFloatUniform("uMat2", material.clearcoat, material.flow, material.f0, body.receiver)
        shader.setFloatUniform("uMat3", material.roughness(theme), TEXEL_PER_DP / density, seed, disabled)
        shader.setFloatUniform("uKey", key[0], key[1], key[2])
        shader.setFloatUniform("uFill", fill[0], fill[1], fill[2])
        val bodyColor = palette.bodyColor(body)
        shader.setColorUniform("uBody", bodyColor.toArgb())
        shader.setColorUniform("uDeep", palette.bodyDeep(body, bodyColor).toArgb())
        shader.setColorUniform("uAccent", palette.accent.toArgb())
        shader.setColorUniform("uSky", lerp(palette.gel, Color.White, SKY_TOWARD_WHITE).toArgb())
        shader.setColorUniform("uGround", palette.environmentLow.toArgb())
        shader.setColorUniform("uShade", palette.shadow.copy(alpha = SHADOW_ALPHA).toArgb())
        shader.setColorUniform("uKeyColor", keyColor)
        shader.setColorUniform("uFillColor", fillColor)
        shader.setColorUniform("uRimColor", rimColor)
        paint.shader = shader
        val margin = height * 2.2f + wall + MARGIN_DP * density
        drawIntoCanvas { it.nativeCanvas.drawRect(-margin, -margin, w + margin, h + margin, paint) }
    }

    private companion object {
        val PLACEHOLDER: Bitmap = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888).apply { eraseColor(0xFF808080.toInt()) }
    }
}

/** The colour light deepens toward along long paths through a body's shoulders. */
internal fun OpalinePalette.bodyDeep(
    body: OpalineBody,
    bodyColor: Color,
): Color =
    when (body.material) {
        OpalineMaterial.STONE -> lerp(bodyColor, deep, STONE_EDGE_TOWARD_DEEP)
        OpalineMaterial.WATER, OpalineMaterial.SHELL, OpalineMaterial.FILM -> water
        else -> deep
    }

private const val TEXEL_PER_DP = 1.1f
private const val MARGIN_DP = 14f
private const val SKY_TOWARD_WHITE = 0.55f
private const val SHADOW_ALPHA = 0.85f
private const val STONE_EDGE_TOWARD_DEEP = 0.55f

/** Gradient rendering of the same anatomy for devices without AGSL. */
private fun DrawScope.drawFallback(
    palette: OpalinePalette,
    body: OpalineBody,
    pressure: Float,
    contact: Offset,
    excitation: Float,
    selected: Float,
    focus: Float,
    disabled: Float,
    cloud: ImageBitmap?,
) {
    val w = size.width
    val h = size.height
    val wall = body.wall.toPx().coerceAtMost(h * 0.4f)
    val topHeight = h - wall
    val radius = body.radius.resolveRadius(this, w, topHeight)
    val corner = CornerRadius(radius)
    val bodyColor = palette.bodyColor(body)
    val deep = palette.bodyDeep(body, bodyColor)
    val dim = 1f - disabled * 0.35f
    val coverage = body.material.coverage * dim
    val lift = 1f - 0.4f * pressure.coerceIn(0f, 1f)

    if (body.receiver > 0f) {
        val offset = body.height.toPx() * 0.8f * lift + 3f * density
        for (layer in 1..SHADOW_LAYERS) {
            val spread = layer * (body.height.toPx() * 0.22f + 1f * density)
            drawRoundRect(
                color = palette.shadow.copy(alpha = body.receiver * 0.1f * lift),
                topLeft = Offset(offset * 0.6f - spread / 2f, offset - spread / 2f),
                size = Size(w + spread, h + spread),
                cornerRadius = CornerRadius(radius + spread / 2f),
            )
        }
    }
    if (body.recessed) {
        drawRoundRect(lerp(bodyColor, palette.environmentLow, 0.35f).copy(alpha = coverage), size = Size(w, topHeight), cornerRadius = corner)
        drawRoundRect(
            brush = Brush.verticalGradient(0f to palette.shadow.copy(alpha = 0.55f * dim), 0.45f to Color.Transparent),
            size = Size(w, topHeight),
            cornerRadius = corner,
        )
        drawRoundRect(palette.gel.copy(alpha = 0.18f * dim), size = Size(w, topHeight), cornerRadius = corner, style = Stroke(1f * density))
    } else {
        val sky = lerp(palette.gel, Color.White, SKY_TOWARD_WHITE)
        if (wall > 0f) {
            drawRoundRect(
                brush = Brush.verticalGradient(listOf(deep.copy(alpha = coverage), lerp(deep, bodyColor, 0.3f).copy(alpha = coverage))),
                size = Size(w, h),
                cornerRadius = corner,
            )
            drawRoundRect(sky.copy(alpha = 0.2f * dim), size = Size(w, h), cornerRadius = corner, style = Stroke(1f * density))
        }
        drawRoundRect(
            brush =
                Brush.verticalGradient(
                    0f to lerp(bodyColor, Color.White, 0.18f).copy(alpha = coverage),
                    0.5f to bodyColor.copy(alpha = coverage),
                    1f to lerp(bodyColor, deep, 0.45f).copy(alpha = coverage),
                    endY = topHeight,
                ),
            size = Size(w, topHeight),
            cornerRadius = corner,
        )
        if (cloud != null) {
            drawRoundRect(
                brush = ShaderBrush(ImageShader(cloud, TileMode.Repeated, TileMode.Repeated)),
                size = Size(w, topHeight),
                cornerRadius = corner,
                alpha = body.material.cloud(palette.theme) * 0.35f * dim,
                blendMode = androidx.compose.ui.graphics.BlendMode.Overlay,
            )
        }
        // Long paths through the shoulders deepen the colour toward every edge.
        val shoulder = (body.height.toPx() * 1.6f).coerceAtMost(minOf(w, topHeight) / 2f)
        val edgeX = (shoulder / w).coerceIn(0.01f, 0.49f)
        val edgeY = (shoulder / topHeight).coerceIn(0.01f, 0.49f)
        val shade = deep.copy(alpha = 0.45f * dim)
        drawRoundRect(
            brush = Brush.horizontalGradient(0f to shade, edgeX to Color.Transparent, 1f - edgeX to Color.Transparent, 1f to shade),
            size = Size(w, topHeight),
            cornerRadius = corner,
        )
        drawRoundRect(
            brush = Brush.verticalGradient(0f to shade.copy(alpha = 0.25f * dim), edgeY to Color.Transparent, 1f - edgeY to Color.Transparent, 1f to shade, endY = topHeight),
            size = Size(w, topHeight),
            cornerRadius = corner,
        )
        drawRoundRect(
            brush = Brush.verticalGradient(0f to sky.copy(alpha = 0.8f * dim), 0.35f to Color.Transparent, endY = topHeight),
            size = Size(w, topHeight),
            cornerRadius = corner,
            style = Stroke(1.4f * density),
        )
        if (body.material != OpalineMaterial.STONE) {
            val glint = Size(minOf(w, topHeight) * 0.24f, minOf(w, topHeight) * 0.1f)
            val glintAt = Offset(radius * 0.45f, topHeight * 0.1f)
            drawOval(
                brush =
                    Brush.radialGradient(
                        listOf(Color.White.copy(alpha = 0.6f * dim), Color.Transparent),
                        center = glintAt + Offset(glint.width / 2f, glint.height / 2f),
                        radius = glint.width / 2f,
                    ),
                topLeft = glintAt,
                size = glint,
            )
        }
    }
    val glow = (excitation * 0.42f + selected * 0.22f).coerceIn(0f, 1f)
    if (glow > 0f) {
        drawRoundRect(
            brush = Brush.radialGradient(listOf(palette.accent.copy(alpha = glow), Color.Transparent), center = contact, radius = maxOf(w, h) * 0.6f),
            size = Size(w, topHeight),
            cornerRadius = corner,
        )
    }
    if (focus > 0f) {
        val inset = -3f * density
        drawRoundRect(
            palette.accent,
            topLeft = Offset(inset, inset),
            size = Size(w - inset * 2f, h - inset * 2f),
            cornerRadius = CornerRadius(radius - inset),
            style = Stroke(1.5f * density),
        )
    }
}

private const val SHADOW_LAYERS = 5
