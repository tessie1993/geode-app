package dev.geode.ui.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

/**
 * The design tokens of the single "black glass" theme.
 *
 * Every colour, spacing step, radius and duration the UI draws with comes from here, so the
 * look can be changed in one place and this file (with [GeodeTheme]) can move into a
 * `:core:designsystem` module without dragging screens along. Nothing in here touches
 * `android.*`.
 *
 * Black glass: a true-black canvas (the visualizer is the colour), panels of white at low alpha
 * with a hairline edge, one cool accent. Dark only, by decision: no light variant and no
 * dynamic colour.
 */
object GeodeColors {
    val Canvas = Color(0xFF000000)
    val CanvasRaised = Color(0xFF060607)

    /** Opaque equivalents of the glass layers over [Canvas], for places that cannot blend. */
    val Surface = Color(0xFF0E0E10)
    val SurfaceContainerLow = Color(0xFF111113)
    val SurfaceContainer = Color(0xFF16161A)
    val SurfaceContainerHigh = Color(0xFF1D1D22)
    val SurfaceContainerHighest = Color(0xFF26262C)

    val Primary = Color(0xFF9CC7FF)
    val OnPrimary = Color(0xFF00213F)
    val PrimaryContainer = Color(0xFF1B2B40)
    val OnPrimaryContainer = Color(0xFFD3E4FF)

    val Secondary = Color(0xFFC3C7D2)
    val OnSecondary = Color(0xFF1B1D22)
    val SecondaryContainer = Color(0xFF2A2D34)
    val OnSecondaryContainer = Color(0xFFE1E4EE)

    val Tertiary = Color(0xFFD9C2F0)
    val OnTertiary = Color(0xFF2A1840)

    val OnCanvas = Color(0xFFECECF0)
    val OnSurface = Color(0xFFE6E6EB)
    val OnSurfaceVariant = Color(0xFFA6A8B2)
    // Switch borders, unchecked thumbs and segmented-button edges are drawn in this, so it has to
    // clear the 3:1 non-text minimum against black and the Switch track (4.6:1 and 3.3:1).
    val Outline = Color(0xFF74767F)
    val OutlineVariant = Color(0xFF26272C)

    val Error = Color(0xFFFFB4AB)
    val OnError = Color(0xFF690005)
    val ErrorContainer = Color(0xFF93000A)
    val OnErrorContainer = Color(0xFFFFDAD6)

    val Scrim = Color(0xFF000000)
}

/** Alphas of white that make up the glass layers when they are drawn over live visuals. */
object GeodeGlass {
    const val FILL_REST = 0.06f
    const val FILL_RAISED = 0.10f
    const val FILL_SELECTED = 0.16f
    const val FILL_PRESSED = 0.20f
    const val EDGE = 0.12f
    const val EDGE_SELECTED = 0.42f

    /** The faint top-down sheen that reads as glass rather than grey plastic. */
    const val SHEEN = 0.05f
    const val DISABLED_CONTENT = 0.38f
}

object GeodeSpacing {
    val XXS = 2.dp
    val XS = 4.dp
    val S = 8.dp
    val M = 12.dp
    val L = 16.dp
    val XL = 24.dp
    val XXL = 32.dp

    /** Minimum touch target, from the Android accessibility guidance. */
    val TouchTarget = 48.dp
}

object GeodeRadius {
    val Small = 8.dp
    val Medium = 12.dp
    val Large = 16.dp
    val ExtraLarge = 28.dp
}

/** Durations in milliseconds; [GeodeMotion.REDUCED] replaces all of them under reduced motion. */
object GeodeMotion {
    const val PRESS = 90
    const val RELEASE = 200
    const val STATE = 150
    const val ENTER = 250
    const val EXIT = 200
    const val REDUCED = 0
    const val PRESS_SCALE = 0.97f
}

/** Minimum contrast ratios, from WCAG 2.1 AA. */
object GeodeContrast {
    const val BODY_MIN = 4.5f
    const val HINT_MIN = 3.0f
}
