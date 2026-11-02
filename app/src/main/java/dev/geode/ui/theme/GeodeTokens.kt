package dev.geode.ui.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

/**
 * The design tokens of the single "black glass" theme.
 *
 * Every colour, spacing step, radius and duration the UI draws with comes from here, so the look
 * can be changed in one place and this file (with [GeodeTheme]) can move into a
 * `:core:designsystem` module without dragging screens along. Nothing in here touches `android.*`.
 *
 * Black glass: a true-black canvas (the visualizer is the colour), floating panels of translucent
 * black matte glass with soft real shadows, one accent taken from the visual ([VisualAccent]).
 * No glows, glowing hairlines, shard corners or edge highlights. Dark only, by decision: no light
 * variant and no dynamic colour.
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

    /** The accent when the visual gives none ([VisualAccent.Fallback]). */
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

    /** The shadow under floating glass: black, so it reads as depth over a bright visual. */
    val Shadow = Color(0xFF000000)
}

/**
 * The glass. Floating surfaces (the Visuals sheet, mini player, quick settings, pop-ups, the tab
 * bar pill) are matte black at [MATTE_FILL] over the live visual, lifted by a soft shadow
 * ([GeodeElevation.Floating]) instead of an edge: draw them with `Modifier.floatingGlass`.
 */
object GeodeGlass {
    /** Black at this alpha over the visual: dark enough for body text, light enough to see through. */
    const val MATTE_FILL = 0.62f

    /** The range the user's sheet-opacity setting moves [MATTE_FILL] in. */
    const val MATTE_FILL_MIN = 0.35f
    const val MATTE_FILL_MAX = 0.92f

    /** A raised control on glass (a chip, a pressed row): white at this alpha over the matte. */
    const val FILL_RAISED = 0.10f
    const val DISABLED_CONTENT = 0.38f

    // The old crystal edge and sheen. Screens still on crystalPanel draw these until they are
    // rebuilt on floatingGlass (wave 1, tier 3); new code must not use them.
    const val EDGE = 0.12f
    const val EDGE_SELECTED = 0.42f
    const val SHEEN = 0.05f
}

/** Shadow depth of the floating glass. Soft and real: an ambient and a key shadow, no glow. */
object GeodeElevation {
    val Floating = 12.dp

    /** A pop-up over the sheet (a menu, a dialog). */
    val Overlay = 16.dp

    /** Shadow strength: alpha of [GeodeColors.Shadow] for the ambient and key (spot) shadows. */
    const val AMBIENT_ALPHA = 0.45f
    const val SPOT_ALPHA = 0.65f
}

object GeodeSpacing {
    val XXS = 2.dp
    val XS = 4.dp
    val S = 8.dp
    val M = 12.dp
    val L = 16.dp
    val XL = 24.dp
    val XXL = 32.dp

    /** How far floating glass sits in from the screen edges (and the system bars). */
    val EdgeInset = 12.dp

    /** The edge inset on compact heights or narrow widths, where 4 dp each side matters. */
    val EdgeInsetCompact = 8.dp

    /** Minimum touch target, from the Android accessibility guidance. */
    val TouchTarget = 48.dp
}

/**
 * Corner radii, Material 3's shape scale. Floating glass uses [Floating] (M3's extra-large, about
 * 28 dp) whatever the Look setting's corner style; controls inside it follow the M3 shapes that
 * setting picks.
 */
object GeodeRadius {
    val Small = 8.dp
    val Medium = 12.dp
    val Large = 16.dp
    val ExtraLarge = 28.dp

    /** The sheet, mini player, quick settings and pop-ups. */
    val Floating = ExtraLarge
}

/**
 * Motion values. Most animation goes through `MaterialTheme.motionScheme` ([geodeMotionScheme]);
 * the spring constants are M3 Expressive's tokens, kept here so the press feedback and any
 * hand-built animation use the same numbers.
 */
object GeodeMotion {
    /** Press feedback: its duration in milliseconds and how far a pressed control shrinks. */
    const val PRESS = 90
    const val PRESS_SCALE = 0.97f

    /** Spatial springs (position, size, scale): a little overshoot, then settle. */
    const val SPATIAL_DAMPING = 0.8f
    const val SPATIAL_FAST_DAMPING = 0.6f
    const val SPATIAL_FAST = 800f
    const val SPATIAL_DEFAULT = 380f
    const val SPATIAL_SLOW = 200f

    /** Effects springs (alpha, colour): critically damped, no bounce. */
    const val EFFECTS_FAST = 3800f
    const val EFFECTS_DEFAULT = 1600f
    const val EFFECTS_SLOW = 800f
}

/** Minimum contrast ratios, from WCAG 2.1 AA. */
object GeodeContrast {
    const val BODY_MIN = 4.5f
}
