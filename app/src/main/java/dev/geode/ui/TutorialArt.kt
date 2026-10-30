package dev.geode.ui

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.rotate
import androidx.compose.ui.graphics.drawscope.scale
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.util.lerp
import dev.geode.ui.theme.GeodeColors
import dev.geode.ui.theme.GeodeGlass
import dev.geode.ui.theme.LocalReducedMotion
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.min
import kotlin.math.sin

/**
 * The tour's illustrations: one small animated drawing per card, each a cartoon of the feature
 * the card talks about rather than a screenshot of it.
 *
 * Drawn in code, not shipped as Lottie or video, for two reasons. The app is offline with no
 * animation library, and drawings made of the theme's own colours cannot go stale or clash when
 * a screen is restyled. Each one is a loop of one [rememberTourClock] period, so nothing jumps
 * when it wraps, and under reduced motion each is frozen on a frame that still reads.
 *
 * Decorative: every card's text says everything its drawing does, so TalkBack skips these.
 */
enum class TutorialArt {
    HELLO,
    CHAPTERS,
    TRANSPORT,
    LYRICS,
    TIMER,
    ALBUMS,
    LIST,
    SEARCH,
    STYLES,
    MORPH,
    AUTO,
    SLIDERS,
    BURST,
    DETAIL,
    DICE,
    EXPAND,
    TOUCH,
    PIP,
    FILM,
    LOOP,
    TIMELINE,
    PHONE,
    DREAM,
    WIDGET,
    GEM,
    TRIAL,
    GEAR,
    CONFETTI,
}

private val ice = GeodeColors.Primary
private val lilac = GeodeColors.Tertiary
private val pink = Color(0xFFEC4899)
private val cyan = Color(0xFF22D3EE)
private val violet = Color(0xFF7C3AED)
private val inks = listOf(cyan, ice, lilac, pink)
private val glass = Color.White.copy(alpha = GeodeGlass.FILL_RAISED)
private val edge = Color.White.copy(alpha = GeodeGlass.EDGE * 2f)

/** The frame every drawing holds under reduced motion: mid-gesture, so it still shows the idea. */
private const val STILL_FRAME = 0.3f
private const val TAU = (2 * PI).toFloat()

/** One full loop of the tour's drawings, 0 → 1, or [STILL_FRAME] forever under reduced motion. */
@Composable
internal fun rememberTourClock(periodMs: Int = 3200): () -> Float {
    if (LocalReducedMotion.current) return { STILL_FRAME }
    val transition = rememberInfiniteTransition(label = "tour-clock")
    val t by transition.animateFloat(
        initialValue = 0f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(periodMs, easing = LinearEasing)),
        label = "tour-t",
    )
    // Read inside draw, so a frame of the loop redraws the canvas without recomposing anything.
    return { t }
}

@Composable
internal fun TutorialArtwork(
    art: TutorialArt,
    modifier: Modifier = Modifier,
) {
    val time = rememberTourClock()
    Canvas(modifier) { drawArt(art, time()) }
}

/**
 * Wave, the tour's guide: an equaliser with a face.
 *
 * [talking] turns the bars up while a card is new, so Wave appears to say it; [cheer] is a hop
 * for a finished chapter. Both ease rather than snap, and under reduced motion Wave holds still.
 */
@Composable
internal fun WaveMascot(
    talking: Boolean,
    modifier: Modifier = Modifier,
    cheer: Boolean = false,
) {
    val time = rememberTourClock(periodMs = 1600)
    val amp by animateFloatAsState(if (talking || cheer) 1f else 0.35f, tween(400), label = "wave-amp")
    val hop by animateFloatAsState(if (cheer) 1f else 0f, tween(260), label = "wave-hop")
    Canvas(modifier) {
        val t = time()
        val r = size.minDimension / 2
        val bob = sin(t * TAU) * r * 0.05f - hop * r * 0.18f
        translate(top = bob) {
            drawCircle(Brush.radialGradient(listOf(ice.copy(alpha = 0.28f), Color.Transparent), center, r), r, center)
            drawCircle(glass, r * 0.86f, center)
            drawCircle(edge, r * 0.86f, center, style = Stroke(r * 0.04f))
            val bars = 5
            val w = r * 0.16f
            for (i in 0 until bars) {
                val x = center.x + (i - 2) * w * 1.45f
                val level = 0.25f + amp * 0.75f * abs(sin((t * (2 + i % 3) + i * 0.17f) * TAU))
                val h = r * (0.18f + 0.5f * level) * (if (i == 2) 1.15f else 1f)
                drawLine(
                    Brush.verticalGradient(listOf(lilac, ice, cyan), center.y - h, center.y + h * 0.6f),
                    Offset(x, center.y + r * 0.22f - h * 0.5f),
                    Offset(x, center.y + r * 0.22f + h * 0.5f),
                    strokeWidth = w,
                    cap = StrokeCap.Round,
                )
            }
            // Blinks for a moment once a loop, the cheapest way to look alive.
            val blink = if ((t * 2f) % 1f in 0.9f..0.96f) 0.15f else 1f
            for (side in listOf(-1, 1)) {
                val eye = Offset(center.x + side * r * 0.24f, center.y - r * 0.3f)
                scale(1f, blink, eye) { drawCircle(Color.White, r * 0.085f, eye) }
            }
        }
    }
}

/** A bright confetti burst for a finished chapter; [progress] runs 0 → 1 once. */
internal fun DrawScope.drawConfettiBurst(progress: Float) {
    if (progress <= 0f || progress >= 1f) return
    val origin = Offset(size.width / 2, size.height * 0.45f)
    for (i in 0 until 36) {
        val angle = i * 137.5f / 360f * TAU
        val speed = 0.35f + (i % 5) * 0.12f
        val d = progress * speed * size.minDimension
        val p = origin + Offset(cos(angle) * d, sin(angle) * d + progress * progress * size.height * 0.3f)
        rotate(progress * 540f + i * 40f, p) {
            drawRect(inks[i % inks.size].copy(alpha = 1f - progress), p - Offset(6f, 3f), Size(12f, 6f))
        }
    }
}

private fun wave(
    t: Float,
    cycles: Float = 1f,
    phase: Float = 0f,
): Float = sin((t * cycles + phase) * TAU)

private fun pulse(
    t: Float,
    cycles: Float = 1f,
    phase: Float = 0f,
): Float = (wave(t, cycles, phase) + 1f) / 2f

private fun fract(x: Float) = x - floor(x)

private fun ink(i: Int) = inks[((i % inks.size) + inks.size) % inks.size]

private fun DrawScope.drawArt(
    art: TutorialArt,
    t: Float,
) {
    when (art) {
        TutorialArt.HELLO -> hello(t)
        TutorialArt.CHAPTERS -> chapters(t)
        TutorialArt.TRANSPORT -> transport(t)
        TutorialArt.LYRICS -> lyrics(t)
        TutorialArt.TIMER -> timer(t, moon = true)
        TutorialArt.ALBUMS -> albums(t)
        TutorialArt.LIST -> list(t)
        TutorialArt.SEARCH -> search(t)
        TutorialArt.STYLES -> styles(t)
        TutorialArt.MORPH -> morph(t)
        TutorialArt.AUTO -> auto(t)
        TutorialArt.SLIDERS -> sliders(t)
        TutorialArt.BURST -> burst(t)
        TutorialArt.DETAIL -> detail(t)
        TutorialArt.DICE -> dice(t)
        TutorialArt.EXPAND -> windows(t, shrinking = false)
        TutorialArt.TOUCH -> touch(t)
        TutorialArt.PIP -> windows(t, shrinking = true)
        TutorialArt.FILM -> film(t)
        TutorialArt.LOOP -> loop(t)
        TutorialArt.TIMELINE -> timeline(t)
        TutorialArt.PHONE -> phone(t)
        TutorialArt.DREAM -> dream(t)
        TutorialArt.WIDGET -> widget(t)
        TutorialArt.GEM -> gem(t)
        TutorialArt.TRIAL -> timer(t, moon = false)
        TutorialArt.GEAR -> gear(t)
        TutorialArt.CONFETTI -> confetti(t)
    }
}

private fun DrawScope.panel(
    rect: Rect,
    corner: Float = 18f,
    fill: Color = glass,
) {
    drawRoundRect(fill, rect.topLeft, rect.size, CornerRadius(corner))
    drawRoundRect(edge, rect.topLeft, rect.size, CornerRadius(corner), style = Stroke(2f))
}

private fun DrawScope.waves(
    rect: Rect,
    t: Float,
) {
    for (k in 0 until 3) {
        val path = Path()
        val steps = 40
        for (s in 0..steps) {
            val x = rect.left + rect.width * s / steps
            val y = rect.center.y + rect.height * 0.18f * sin(s / steps.toFloat() * TAU * (1 + k) + t * TAU + k)
            if (s == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }
        drawPath(path, ink(k), alpha = 0.85f, style = Stroke(rect.height * 0.03f, cap = StrokeCap.Round))
    }
}

private fun DrawScope.hello(t: Float) {
    for (i in 0 until 4) {
        val k = fract(t + i / 4f)
        drawCircle(ink(i), size.minDimension * (0.15f + 0.4f * k), center, alpha = (1f - k) * 0.6f, style = Stroke(4f))
    }
    for (i in 0 until 8) {
        val a = (t + i / 8f) * TAU
        val p = center + Offset(cos(a), sin(a)) * size.minDimension * 0.42f
        drawCircle(ink(i), 4f + 4f * pulse(t, 2f, i / 8f), p)
    }
}

private fun DrawScope.chapters(t: Float) {
    val n = 10
    val lit = t * (n + 2)
    for (i in 0 until n) {
        val x = size.width * (0.08f + 0.84f * i / (n - 1))
        val y = center.y + sin(i / (n - 1f) * PI.toFloat()) * -size.height * 0.2f
        if (i > 0) {
            val px = size.width * (0.08f + 0.84f * (i - 1) / (n - 1))
            val py = center.y + sin((i - 1) / (n - 1f) * PI.toFloat()) * -size.height * 0.2f
            drawLine(edge, Offset(px, py), Offset(x, y), 3f)
        }
        val on = i < lit
        drawCircle(if (on) ink(i) else glass, if (on) 14f else 10f, Offset(x, y))
        if (on && i.toFloat() > lit - 1.5f) drawCircle(ink(i), 26f, Offset(x, y), alpha = 0.3f)
    }
}

private fun DrawScope.transport(t: Float) {
    val bars = 18
    val bw = size.width * 0.8f / bars
    for (i in 0 until bars) {
        val h = size.height * (0.1f + 0.35f * pulse(t, 3f + i % 4, i * 0.13f))
        drawRoundRect(
            ink(i / 5),
            Offset(size.width * 0.1f + i * bw + bw * 0.15f, size.height * 0.62f - h),
            Size(bw * 0.7f, h),
            CornerRadius(bw * 0.35f),
        )
    }
    val y = size.height * 0.78f
    drawLine(edge, Offset(size.width * 0.1f, y), Offset(size.width * 0.9f, y), 6f, StrokeCap.Round)
    drawLine(ice, Offset(size.width * 0.1f, y), Offset(size.width * (0.1f + 0.8f * t), y), 6f, StrokeCap.Round)
    drawCircle(Color.White, 10f, Offset(size.width * (0.1f + 0.8f * t), y))
    val c = Offset(center.x, size.height * 0.25f)
    val r = size.minDimension * (0.11f + 0.015f * pulse(t, 4f))
    drawCircle(ice, r, c)
    val tri =
        Path().apply {
            moveTo(c.x - r * 0.3f, c.y - r * 0.45f)
            lineTo(c.x + r * 0.5f, c.y)
            lineTo(c.x - r * 0.3f, c.y + r * 0.45f)
            close()
        }
    drawPath(tri, GeodeColors.OnPrimary)
}

private fun DrawScope.lyrics(t: Float) {
    val lineH = size.height / 6
    // Four lines a loop, and line widths that repeat every four lines, so the wrap is invisible.
    val lines = t * 4
    val scroll = fract(lines) * lineH
    for (i in -1 until 7) {
        val y = i * lineH - scroll + lineH / 2
        val focus = 1f - min(1f, abs(y - center.y) / (lineH * 1.2f))
        val w = size.width * (0.45f + 0.3f * pulse(0f, 1f, ((i + floor(lines).toInt()) % 4) * 0.31f))
        val brush = Brush.horizontalGradient(listOf(lilac, pink), center.x - w / 2, center.x + w / 2)
        drawLine(
            if (focus > 0.5f) brush else Brush.linearGradient(listOf(edge, edge)),
            Offset(center.x - w / 2, y),
            Offset(center.x + w / 2, y),
            strokeWidth = 10f + 8f * focus,
            cap = StrokeCap.Round,
        )
    }
}

private fun DrawScope.timer(
    t: Float,
    moon: Boolean,
) {
    val r = size.minDimension * 0.34f
    val tl = center - Offset(r, r)
    drawArc(edge, 0f, 360f, false, tl, Size(r * 2, r * 2), style = Stroke(10f))
    drawArc(
        Brush.sweepGradient(listOf(cyan, lilac, pink, cyan), center),
        -90f,
        360f * (1f - t),
        false,
        tl,
        Size(r * 2, r * 2),
        style = Stroke(10f, cap = StrokeCap.Round),
    )
    if (moon) {
        drawCircle(lilac, r * 0.42f, center)
        drawCircle(GeodeColors.Canvas, r * 0.38f, center + Offset(r * 0.2f, -r * 0.14f))
        for (i in 0 until 3) {
            val p = center + Offset((i - 1) * r * 0.9f, -r * 1.25f + (i % 2) * 18f)
            drawCircle(Color.White, 3f + 3f * pulse(t, 3f, i / 3f), p)
        }
    } else {
        val a = t * TAU * 4 - PI.toFloat() / 2
        drawLine(Color.White, center, center + Offset(cos(a), sin(a)) * r * 0.7f, 8f, StrokeCap.Round)
        drawCircle(ice, 10f, center)
        for (i in 0 until 6) {
            val sa = i / 6f * TAU + t * TAU
            drawCircle(ink(i), 4f + 4f * pulse(t, 2f, i / 6f), center + Offset(cos(sa), sin(sa)) * r * 1.3f)
        }
    }
}

private fun DrawScope.albums(t: Float) {
    val cols = 3
    val s = min(size.width / (cols + 1f), size.height / 2.6f)
    val gap = s * 0.2f
    val left = center.x - (cols * s + (cols - 1) * gap) / 2
    for (row in 0 until 2) {
        for (col in 0 until cols) {
            val i = row * cols + col
            val lift = 8f * pulse(t, 1f, i / 6f)
            val tl = Offset(left + col * (s + gap), center.y - s - gap / 2 + row * (s + gap) - lift)
            drawRoundRect(
                Brush.linearGradient(listOf(ink(i), ink(i + 1)), tl, tl + Offset(s, s)),
                tl,
                Size(s, s),
                CornerRadius(14f),
            )
            drawCircle(Color.Black.copy(alpha = 0.35f), s * 0.18f, tl + Offset(s / 2, s / 2))
        }
    }
}

private fun DrawScope.rows(
    t: Float,
    highlight: Int,
) {
    val n = 5
    val h = size.height / (n + 1)
    for (i in 0 until n) {
        val enter = (t * 4f - i * 0.25f).coerceIn(0f, 1f)
        val x = size.width * 0.1f + (1f - enter) * size.width
        val top = h * 0.5f + i * h
        val rect = Rect(Offset(x, top), Size(size.width * 0.8f, h * 0.78f))
        panel(rect, 14f, if (i == highlight) ice.copy(alpha = 0.22f) else glass)
        drawRoundRect(ink(i), rect.topLeft + Offset(10f, 8f), Size(rect.height - 16f, rect.height - 16f), CornerRadius(8f))
        drawLine(
            Color.White.copy(alpha = 0.7f),
            Offset(rect.left + rect.height + 12f, rect.center.y - 6f),
            Offset(rect.left + rect.width * 0.7f, rect.center.y - 6f),
            8f,
            StrokeCap.Round,
        )
        drawLine(
            edge,
            Offset(rect.left + rect.height + 12f, rect.center.y + 10f),
            Offset(rect.left + rect.width * 0.5f, rect.center.y + 10f),
            6f,
            StrokeCap.Round,
        )
    }
}

private fun DrawScope.list(t: Float) = rows(t, highlight = (t * 5).toInt().coerceIn(0, 4))

private fun DrawScope.search(t: Float) {
    rows(1f, highlight = -1)
    val p = Offset(center.x + wave(t) * size.width * 0.25f, center.y + wave(t, 2f) * size.height * 0.25f)
    val r = size.minDimension * 0.14f
    drawCircle(ice.copy(alpha = 0.18f), r, p)
    drawCircle(ice, r, p, style = Stroke(8f))
    drawLine(ice, p + Offset(r * 0.7f, r * 0.7f), p + Offset(r * 1.4f, r * 1.4f), 12f, StrokeCap.Round)
}

private fun DrawScope.styleTile(
    rect: Rect,
    kind: Int,
    t: Float,
) {
    panel(rect, 22f, GeodeColors.SurfaceContainer)
    val c = rect.center
    val r = rect.minDimension * 0.42f
    when (kind % 3) {
        0 -> {
            for (k in 0 until 5) drawCircle(ink(k), r * fract(k / 5f + t), c, style = Stroke(5f))
        }
        1 -> {
            waves(rect, t)
        }
        else -> {
            for (k in 0 until 8) {
                val a = k / 8f * TAU + t * TAU / 2
                drawLine(ink(k), c, c + Offset(cos(a), sin(a)) * r, 7f, StrokeCap.Round)
            }
        }
    }
}

private fun DrawScope.styles(t: Float) {
    val w = size.width * 0.5f
    val h = size.height * 0.75f
    // Three tiles a loop with three kinds of tile, so a tile always lands where its twin started.
    for (j in -1..6) {
        val x = center.x - w / 2 + (j - t * 3) * (w + 24f)
        styleTile(Rect(Offset(x, center.y - h / 2), Size(w, h)), j + 3, t)
    }
}

private fun DrawScope.morph(t: Float) {
    val m = pulse(t)
    val path = Path()
    val steps = 90
    val r = size.minDimension * 0.32f
    for (s in 0..steps) {
        val a = s / steps.toFloat() * TAU
        val rr = r * (1f + 0.28f * m * sin(5 * a + t * TAU))
        val p = center + Offset(cos(a), sin(a)) * rr
        if (s == 0) path.moveTo(p.x, p.y) else path.lineTo(p.x, p.y)
    }
    path.close()
    val fill = Brush.linearGradient(listOf(lerp(cyan, pink, m), lerp(lilac, ice, m)), center - Offset(r, r), center + Offset(r, r))
    drawPath(path, fill)
    drawPath(path, Color.White.copy(alpha = 0.5f), style = Stroke(3f))
}

private fun DrawScope.auto(t: Float) {
    val y = size.height * 0.82f
    val left = size.width * 0.1f
    val w = size.width * 0.8f
    val sections = 4
    for (i in 0 until sections) {
        drawRoundRect(ink(i), Offset(left + w * i / sections + 3f, y - 14f), Size(w / sections - 6f, 28f), CornerRadius(14f))
    }
    val now = (t * sections).toInt().coerceIn(0, sections - 1)
    drawLine(Color.White, Offset(left + w * t, y - 28f), Offset(left + w * t, y + 28f), 5f, StrokeCap.Round)
    val r = size.minDimension * (0.24f + 0.03f * pulse(t, 8f))
    val c = Offset(center.x, size.height * 0.38f)
    drawCircle(Brush.radialGradient(listOf(ink(now), ink(now + 1).copy(alpha = 0f)), c, r * 1.6f), r * 1.6f, c)
    drawCircle(ink(now), r * 0.7f, c)
}

private fun DrawScope.sliders(t: Float) {
    val n = 6
    val gap = size.width / (n + 1)
    for (i in 0 until n) {
        val x = gap * (i + 1)
        val top = size.height * 0.12f
        val bottom = size.height * 0.88f
        val v = 0.5f + 0.35f * wave(t, 1f, i * 0.06f)
        val y = bottom - (bottom - top) * v
        drawLine(edge, Offset(x, top), Offset(x, bottom), 10f, StrokeCap.Round)
        drawLine(ink(i), Offset(x, y), Offset(x, bottom), 10f, StrokeCap.Round)
        drawCircle(Color.White, 16f, Offset(x, y))
    }
}

private fun DrawScope.burst(t: Float) {
    val k = fract(t * 2)
    val kick = 1f - k
    drawCircle(pink.copy(alpha = 0.25f * kick), size.minDimension * (0.12f + 0.4f * k), center)
    drawCircle(pink, size.minDimension * (0.1f + 0.05f * kick * kick), center)
    for (i in 0 until 16) {
        val a = i / 16f * TAU
        val d = size.minDimension * (0.15f + 0.32f * k)
        drawCircle(ink(i), 7f * kick + 2f, center + Offset(cos(a), sin(a)) * d)
    }
}

private fun DrawScope.detail(t: Float) {
    val chips = 6
    val cw = size.width * 0.8f / chips
    val sel = (t * chips).toInt().coerceIn(0, chips - 1)
    for (i in 0 until chips) {
        val rect = Rect(Offset(size.width * 0.1f + i * cw + 4f, size.height * 0.1f), Size(cw - 8f, 30f))
        panel(rect, 15f, if (i == sel) ice.copy(alpha = 0.35f) else glass)
    }
    for (row in 0 until 3) {
        val y = size.height * (0.42f + row * 0.2f)
        val v = 0.5f + 0.4f * wave(t, 1f + row, row * 0.3f)
        val l = size.width * 0.12f
        val r = size.width * 0.78f
        drawLine(edge, Offset(l, y), Offset(r, y), 8f, StrokeCap.Round)
        drawLine(ink(row + sel), Offset(l, y), Offset(l + (r - l) * v, y), 8f, StrokeCap.Round)
        drawCircle(Color.White, 13f, Offset(l + (r - l) * v, y))
        // The Reset that appears once a control is off its default.
        if (abs(v - 0.5f) > 0.15f) drawCircle(lilac, 9f, Offset(size.width * 0.88f, y))
    }
}

private fun DrawScope.dice(t: Float) {
    val s = size.minDimension * 0.42f
    val face = (floor(t * 4).toInt() % 6) + 1
    val spin = fract(t * 4)
    val angle = if (spin < 0.3f) spin / 0.3f * 90f else 0f
    rotate(angle, center) {
        drawRoundRect(
            Brush.linearGradient(listOf(lilac, ice), center - Offset(s / 2, s / 2), center + Offset(s / 2, s / 2)),
            center - Offset(s / 2, s / 2),
            Size(s, s),
            CornerRadius(s * 0.18f),
        )
        val pips =
            mapOf(
                1 to listOf(0 to 0),
                2 to listOf(-1 to -1, 1 to 1),
                3 to listOf(-1 to -1, 0 to 0, 1 to 1),
                4 to listOf(-1 to -1, 1 to -1, -1 to 1, 1 to 1),
                5 to listOf(-1 to -1, 1 to -1, 0 to 0, -1 to 1, 1 to 1),
                6 to listOf(-1 to -1, 1 to -1, -1 to 0, 1 to 0, -1 to 1, 1 to 1),
            )
        pips.getValue(face).forEach { (dx, dy) ->
            drawCircle(GeodeColors.OnPrimary, s * 0.08f, center + Offset(dx * s * 0.26f, dy * s * 0.26f))
        }
    }
    drawArc(edge, 200f, 140f, false, center - Offset(s, s), Size(s * 2, s * 2), style = Stroke(6f, cap = StrokeCap.Round))
}

private fun DrawScope.windows(
    t: Float,
    shrinking: Boolean,
) {
    val e = (pulse(t) * 1.4f - 0.2f).coerceIn(0f, 1f)
    val full = Rect(Offset(size.width * 0.06f, size.height * 0.06f), Size(size.width * 0.88f, size.height * 0.88f))
    panel(full, 20f, GeodeColors.SurfaceContainerLow)
    if (shrinking) {
        rows(1f, highlight = -1)
        val small = Rect(Offset(size.width * 0.55f, size.height * 0.58f), Size(size.width * 0.34f, size.height * 0.3f))
        val r = lerpRect(full, small, e)
        panel(r, 16f, GeodeColors.SurfaceContainerHighest)
        waves(r, t)
    } else {
        val small = Rect(center = center, radius = size.minDimension * 0.15f)
        val r = lerpRect(small, full, e)
        panel(r, 16f, GeodeColors.SurfaceContainerHighest)
        styleTile(r, 0, t)
    }
}

private fun lerpRect(
    a: Rect,
    b: Rect,
    f: Float,
) = Rect(lerp(a.left, b.left, f), lerp(a.top, b.top, f), lerp(a.right, b.right, f), lerp(a.bottom, b.bottom, f))

private fun DrawScope.touch(t: Float) {
    val finger = center + Offset(wave(t) * size.width * 0.18f, wave(t, 2f, 0.25f) * size.height * 0.18f)
    for (arm in 0 until 3) {
        for (s in 0 until 14) {
            val a = arm / 3f * TAU + s * 0.35f + t * TAU
            val d = s * size.minDimension * 0.03f
            drawCircle(ink(arm), 3f + s * 0.6f, finger + Offset(cos(a), sin(a)) * d, alpha = 1f - s / 16f)
        }
    }
    for (i in 0 until 3) {
        val k = fract(t * 2 + i / 3f)
        drawCircle(Color.White, 20f + 60f * k, finger, alpha = (1f - k) * 0.5f, style = Stroke(3f))
    }
    drawCircle(Color.White.copy(alpha = 0.9f), 18f, finger)
}

private fun DrawScope.film(t: Float) {
    val fh = size.height * 0.42f
    val fw = fh * 1.2f
    val top = center.y - fh / 2 - 10f
    drawRect(GeodeColors.SurfaceContainer, Offset(0f, top - 24f), Size(size.width, fh + 48f))
    val shift = fract(t * 2) * (fw + 16f)
    var i = -1
    var x = -shift
    while (x < size.width) {
        val done = x + fw / 2 < center.x
        val rect = Rect(Offset(x, top), Size(fw, fh))
        drawRoundRect(if (done) ink(i) else edge, rect.topLeft, rect.size, CornerRadius(8f), alpha = if (done) 0.85f else 1f)
        drawCircle(Color.Black, 5f, Offset(x + fw / 2, top - 12f))
        drawCircle(Color.Black, 5f, Offset(x + fw / 2, top + fh + 12f))
        x += fw + 16f
        i++
    }
    drawLine(Color.White, Offset(center.x, top - 30f), Offset(center.x, top + fh + 30f), 4f)
    val y = size.height * 0.88f
    drawLine(edge, Offset(size.width * 0.1f, y), Offset(size.width * 0.9f, y), 10f, StrokeCap.Round)
    drawLine(ice, Offset(size.width * 0.1f, y), Offset(size.width * (0.1f + 0.8f * t), y), 10f, StrokeCap.Round)
}

private fun DrawScope.loop(t: Float) {
    val a = size.width * 0.36f

    fun at(u: Float): Offset {
        val th = u * TAU
        val d = 1f + sin(th) * sin(th)
        return center + Offset(a * cos(th) / d, a * sin(th) * cos(th) / d)
    }
    val path = Path()
    for (s in 0..120) {
        val p = at(s / 120f)
        if (s == 0) path.moveTo(p.x, p.y) else path.lineTo(p.x, p.y)
    }
    drawPath(path, edge, style = Stroke(12f))
    for (k in 0 until 16) {
        drawCircle(ink(k / 4), 14f - k * 0.7f, at(t - k * 0.012f), alpha = 1f - k / 16f)
    }
}

private fun DrawScope.timeline(t: Float) {
    val lanes = 3
    val lh = size.height / (lanes + 1)
    val clips = listOf(listOf(0.05f to 0.3f, 0.34f to 0.62f, 0.66f to 0.95f), listOf(0.1f to 0.5f, 0.55f to 0.8f))
    val head = size.width * (0.05f + 0.9f * t)
    clips.forEachIndexed { lane, spans ->
        spans.forEachIndexed { j, (s, e) ->
            val l = size.width * s
            val r = size.width * e
            val under = head in l..r
            drawRoundRect(
                ink(lane * 2 + j),
                Offset(l, lh * (lane + 0.6f)),
                Size(r - l, lh * 0.7f),
                CornerRadius(10f),
                alpha = if (under) 1f else 0.5f,
            )
        }
    }
    val ky = lh * 2.95f
    for (i in 0 until 6) {
        val kx = size.width * (0.1f + i * 0.16f)
        val big = abs(kx - head) < 30f
        rotate(45f, Offset(kx, ky)) {
            val s = if (big) 22f else 15f
            drawRect(if (big) Color.White else lilac, Offset(kx - s / 2, ky - s / 2), Size(s, s))
        }
    }
    drawLine(pink, Offset(head, lh * 0.3f), Offset(head, size.height - lh * 0.4f), 5f, StrokeCap.Round)
}

private fun DrawScope.phoneFrame(
    landscape: Boolean,
    t: Float,
): Rect {
    val h = size.height * (if (landscape) 0.5f else 0.9f)
    val w = if (landscape) h * 1.9f else h * 0.5f
    val rect = Rect(Offset(center.x - w / 2, center.y - h / 2), Size(w, h))
    drawRoundRect(Color.Black, rect.topLeft, rect.size, CornerRadius(26f))
    waves(rect.deflate(14f), t)
    drawRoundRect(edge, rect.topLeft, rect.size, CornerRadius(26f), style = Stroke(5f))
    return rect
}

private fun DrawScope.phone(t: Float) {
    val rect = phoneFrame(landscape = false, t)
    val s = rect.width / 6
    for (row in 0 until 2) {
        for (col in 0 until 3) {
            val tl = Offset(rect.left + s * (0.75f + col * 1.75f), rect.top + rect.height * 0.62f + row * s * 1.5f)
            drawRoundRect(Color.White.copy(alpha = 0.22f), tl, Size(s, s), CornerRadius(s * 0.3f))
        }
    }
}

private fun DrawScope.dream(t: Float) {
    val rect = phoneFrame(landscape = true, t)
    val base = rect.bottom + 18f
    drawLine(edge, Offset(center.x - 40f, base), Offset(center.x + 40f, base), 10f, StrokeCap.Round)
    val c = Offset(rect.right - 10f, rect.top - 30f)
    drawCircle(lilac, 26f, c)
    drawCircle(GeodeColors.Canvas, 22f, c + Offset(12f, -8f))
    drawCircle(ice, 6f + 4f * pulse(t, 2f), Offset(rect.left + 10f, rect.top - 40f))
}

private fun DrawScope.widget(t: Float) {
    val rect = Rect(Offset(size.width * 0.08f, size.height * 0.25f), Size(size.width * 0.84f, size.height * 0.5f))
    panel(rect, 28f, GeodeColors.SurfaceContainerHigh)
    val art = rect.height - 32f
    rotate(t * 360f, rect.topLeft + Offset(16f + art / 2, 16f + art / 2)) {
        drawRoundRect(
            Brush.sweepGradient(inks + cyan, rect.topLeft + Offset(16f + art / 2, 16f + art / 2)),
            rect.topLeft + Offset(16f, 16f),
            Size(art, art),
            CornerRadius(art / 2),
        )
    }
    val tx = rect.left + art + 36f
    val title = Color.White.copy(alpha = 0.8f)
    drawLine(title, Offset(tx, rect.top + 36f), Offset(rect.right - 40f, rect.top + 36f), 10f, StrokeCap.Round)
    drawLine(edge, Offset(tx, rect.top + 60f), Offset(rect.right - 90f, rect.top + 60f), 8f, StrokeCap.Round)
    for (i in 0 until 3) {
        val c = Offset(tx + 24f + i * (rect.right - tx - 48f) / 2, rect.bottom - 40f)
        drawCircle(if (i == 1) ice else edge, if (i == 1) 22f + 3f * pulse(t, 4f) else 16f, c)
    }
}

private fun DrawScope.gem(t: Float) {
    val r = size.minDimension * 0.34f
    val squash = 0.35f + 0.65f * abs(cos(t * TAU / 2))
    scale(squash, 1f, center) {
        val top = center.y - r * 0.6f
        val girdle = center.y - r * 0.1f
        val path =
            Path().apply {
                moveTo(center.x - r * 0.55f, top)
                lineTo(center.x + r * 0.55f, top)
                lineTo(center.x + r, girdle)
                lineTo(center.x, center.y + r)
                lineTo(center.x - r, girdle)
                close()
            }
        val facets = Brush.linearGradient(listOf(violet, pink, lilac), Offset(center.x - r, top), Offset(center.x + r, center.y + r))
        drawPath(path, facets)
        drawLine(Color.White.copy(alpha = 0.5f), Offset(center.x - r, girdle), Offset(center.x + r, girdle), 3f)
        drawLine(Color.White.copy(alpha = 0.4f), Offset(center.x, top), Offset(center.x, center.y + r), 3f)
    }
    for (i in 0 until 5) {
        val p = center + Offset(cos(i * 1.3f) * r * 1.35f, sin(i * 1.9f) * r * 1.2f)
        val s = 14f * pulse(t, 2f, i / 5f)
        drawLine(Color.White, p - Offset(s, 0f), p + Offset(s, 0f), 3f, StrokeCap.Round)
        drawLine(Color.White, p - Offset(0f, s), p + Offset(0f, s), 3f, StrokeCap.Round)
    }
}

private fun DrawScope.gear(t: Float) {
    val r = size.minDimension * 0.26f
    val c = Offset(size.width * 0.32f, center.y)
    rotate(t * 90f, c) {
        for (i in 0 until 8) {
            rotate(i * 45f, c) {
                drawRoundRect(ice, Offset(c.x - r * 0.16f, c.y - r * 1.18f), Size(r * 0.32f, r * 0.5f), CornerRadius(6f))
            }
        }
        drawCircle(ice, r, c)
        drawCircle(GeodeColors.Canvas, r * 0.42f, c)
    }
    for (row in 0 until 3) {
        val y = size.height * (0.3f + row * 0.2f)
        val l = size.width * 0.6f
        val rr = size.width * 0.92f
        val v = pulse(t, 1f, row * 0.33f)
        drawLine(edge, Offset(l, y), Offset(rr, y), 8f, StrokeCap.Round)
        drawCircle(ink(row + 1), 12f, Offset(l + (rr - l) * v, y))
    }
}

private fun DrawScope.confetti(t: Float) {
    for (i in 0 until 40) {
        val x = size.width * fract(i * 0.618f)
        val y = size.height * fract(t + i * 0.137f) - 10f
        val p = Offset(x + 10f * wave(t, 2f, i * 0.1f), y)
        rotate((t * 2 + i * 0.1f) * 360f, p) { drawRect(ink(i), p - Offset(7f, 4f), Size(14f, 8f)) }
    }
}
