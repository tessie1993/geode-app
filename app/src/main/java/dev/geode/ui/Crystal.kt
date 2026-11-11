package dev.geode.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.Typography
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.layout.layout
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import dev.geode.data.GuiPrefs
import dev.geode.render.VisualizerView
import dev.geode.ui.theme.GeodeColors
import dev.geode.ui.theme.GeodeGlass
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.LocalReducedMotion
import dev.geode.ui.theme.LocalUiSounds
import dev.geode.ui.theme.geodeColorScheme
import dev.geode.ui.theme.geodeMotionScheme
import dev.geode.ui.theme.geodeTypography
import dev.geode.ui.theme.isReadableOnCanvas
import dev.geode.ui.theme.rememberSystemReducedMotion
import dev.geode.ui.theme.rememberUiSounds

/**
 * The app's one theme: black glass on the Material 3 type scale, with M3 Expressive motion.
 *
 * The `Crystal*` names are kept as the seam every screen already calls through; what they draw
 * is now plain Material with the [GeodeColors]/[GeodeGlass] tokens.
 *
 * [accentSource] is the current visual's main colour, which the accent follows
 * ([dev.geode.ui.theme.VisualAccent]); null keeps the fallback #9CC7FF. The motion scheme follows
 * the system's "Remove animations" live, or the app's own reduced-motion setting.
 */
@Composable
internal fun CrystalMaterialTheme(
    gui: GuiPrefs,
    accentSource: Color? = null,
    content: @Composable () -> Unit,
) {
    val fontColor = gui.fontColorArgb?.let { Color(it) }?.takeIf { isReadableOnCanvas(it) }
    val scheme =
        geodeColorScheme(
            accentIntensity = gui.accentIntensity,
            fontColorOverride = fontColor,
            accentSource = accentSource,
        )
    // Android's "Remove animations" alone (data/SystemMotion, #336), watched live.
    val reducedMotion = rememberSystemReducedMotion()
    CompositionLocalProvider(
        LocalFontColor provides fontColor,
        LocalContentColor provides scheme.onBackground,
        LocalReducedMotion provides reducedMotion,
        LocalGeodeMotion provides geodeMotionScheme(reducedMotion),
        LocalUiSounds provides rememberUiSounds(gui.uiSounds),
    ) {
        MaterialTheme(
            colorScheme = scheme,
            shapes = gui.cornerStyle.shapes(),
            typography = crystalTypography(gui.textScale),
            content = content,
        )
    }
}

fun crystalTypography(textScale: Float = 1f): Typography =
    geodeTypography(
        textScale.coerceIn(GuiPrefs.TEXT_SCALE_MIN, GuiPrefs.TEXT_SCALE_MAX),
    )

fun crystalShardShape(
    cut: Dp = 12.dp,
    minor: Dp = 4.dp,
): Shape = RoundedCornerShape(cut + minor)

/** A small selection dot: the indicator under the selected tab and nav item. */
@Composable
fun CrystalGem(
    color: Color,
    modifier: Modifier = Modifier,
    size: Dp = 7.dp,
) {
    Box(
        modifier
            .size(size)
            .clip(CircleShape)
            .background(color),
    )
}

@Composable
fun CrystalOverline(
    text: String,
    modifier: Modifier = Modifier,
    color: Color = accentTextColor(),
) {
    Text(
        text.uppercase(),
        modifier,
        style =
            MaterialTheme.typography.labelSmall.copy(
                letterSpacing = 1.2.sp,
                fontWeight = FontWeight.Medium,
            ),
        color = color,
    )
}

@Composable
fun GlowTitle(
    text: String,
    modifier: Modifier = Modifier,
    style: TextStyle = MaterialTheme.typography.headlineSmall,
) {
    // Every top-level screen opens with this, so it is the heading TalkBack users jump between.
    Text(text, modifier.semantics { heading() }, style = style, color = MaterialTheme.colorScheme.onBackground)
}

/**
 * A glass panel: [tint] at [opacity] over whatever is behind it, a faint top sheen and a hairline
 * edge. [glow] tints the edge, [glowStrength] how visible it is; [prismatic] marks a selected
 * panel with a brighter edge.
 */
fun Modifier.crystalPanel(
    opacity: Float,
    tint: Color,
    glow: Color,
    corner: Dp = 18.dp,
    glowStrength: Float = 1f,
    prismatic: Boolean = false,
): Modifier {
    val shape = RoundedCornerShape(corner)
    val fill = tint.copy(alpha = opacity.coerceIn(0f, 1f))
    val edge =
        if (prismatic) {
            glow.copy(alpha = GeodeGlass.EDGE_SELECTED)
        } else {
            Color.White.copy(alpha = (GeodeGlass.EDGE * glowStrength.coerceIn(0.4f, 1.4f)).coerceAtMost(1f))
        }
    return this
        .clip(shape)
        .background(fill)
        .background(
            Brush.verticalGradient(
                0f to Color.White.copy(alpha = GeodeGlass.SHEEN),
                0.5f to Color.Transparent,
            ),
        ).border(1.dp, edge, shape)
}

/**
 * The app's one live [VisualizerView], hosted once by the shell (`AppRoot`) for every screen and
 * laid out at [bounds] (the Visuals screen's, in root pixels), so a tab switch never detaches it.
 * Off Visuals ([shown] false) it is hidden with alpha 0, not `INVISIBLE` (#852): an invisible
 * SurfaceView gives its surface back, and showing it again is a `surfaceChanged` that parks the
 * main thread until the GL thread has drawn a frame. At alpha 0 the surface and the GL thread
 * and EGL context all stay; the view stops its own pacer and gyro (see `VisualizerView.setAlpha`).
 *
 * It used to be composed inside Visuals, so leaving Visuals removed it from the hierarchy: a
 * main-thread wait of up to 200 ms for the scene release, a GL thread join, and coming back a
 * new EGL context, every program rebuilt through ProgramStager and a black first frame.
 *
 * Still hidden until [bounds] has been measured once, so a cold start on Visuals never shows a
 * 0×0 surface. The second screen remains the one real move: the shell drops this host while a
 * Presentation holds the view, and the factory takes it back from whatever parent it has.
 * Being drawn before the screens, the surface shows through wherever they draw nothing.
 */
@Composable
fun VisualizerCanvasHost(
    view: VisualizerView,
    shown: Boolean,
    bounds: androidx.compose.runtime.State<androidx.compose.ui.unit.IntRect>,
    modifier: Modifier = Modifier,
) {
    val measured =
        androidx.compose.runtime.remember(bounds) {
            androidx.compose.runtime.derivedStateOf { !bounds.value.isEmpty }
        }
    val visible = shown && measured.value
    AndroidView(
        factory = {
            (view.parent as? android.view.ViewGroup)?.removeView(view)
            view
        },
        // Bounds are read in the layout pass only, so a rotation or a PiP resize relays this out
        // without recomposing the shell.
        modifier =
            modifier.layout { measurable, constraints ->
                val r = bounds.value
                val w = r.width.coerceAtLeast(0)
                val h = r.height.coerceAtLeast(0)
                val placeable = measurable.measure(constraints.copy(minWidth = w, maxWidth = w, minHeight = h, maxHeight = h))
                layout(constraints.maxWidth, constraints.maxHeight) { placeable.place(r.left, r.top) }
            },
        update = { it.alpha = if (visible) 1f else 0f },
    )
}
