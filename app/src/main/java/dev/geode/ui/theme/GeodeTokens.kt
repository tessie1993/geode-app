package dev.geode.ui.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.compositeOver
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

    /**
     * Secondary text: a row's second line, unselected tab labels and icons. It is drawn on the
     * glass far more than on the black canvas, and the glass over a white frame is a mid grey
     * ([GeodeGlass.WorstBackdrop]), so it is set by that grey, not by black: about 4.7:1 there
     * (WCAG's 4.5:1 for small text) and 12:1 on black. The old #A6A8B2 read 8.9:1 on black but
     * 2.6:1 on the tab bar over a white frame, which is where unselected tab labels vanished
     * (#559). It sits about where Material's dark onSurfaceVariant does (tone 80 under
     * onSurface's 90), so the two-level hierarchy keeps M3's step.
     */
    val OnSurfaceVariant = Color(0xFFC4C6D0)

    /**
     * Switch borders, unchecked thumbs, text-field and segmented-button edges. Not text, but these
     * controls are drawn on the glass, so it clears WCAG's 3:1 non-text minimum on
     * [GeodeGlass.WorstBackdrop] (about 3.1:1), not only on black (8.2:1) and on the Switch track
     * (5.9:1). The old #74767F read 1.8:1 on the worst-case glass.
     */
    val Outline = Color(0xFFA0A2AC)
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
 *
 * WHY the alphas are what they are: text on the glass has to stay readable over any frame the
 * visual draws, and the worst frame is pure white. Black at alpha α over white leaves a grey of
 * 1 − α (Android blends in sRGB), and the blur under the glass (the native UI blur, `glassBlur`)
 * spreads that white without darkening it. So the floor is set by contrast against that grey,
 * [WorstBackdrop], with a raised fill ([FILL_RAISED]) on top, because a chosen row lightens it
 * further. Material 3 › Typography › Color & contrast: "Small text should achieve a contrast ratio
 * of 4.5:1" and "Large text should achieve a contrast ratio of 3:1"
 * (m3.material.io/styles/typography/applying-type), WCAG 2.1's 1.4.3 and, for icons and control
 * edges, 1.4.11 ([GeodeContrast]). Measured on that grey (issue #559):
 *
 * | Glass α                       | OnSurface | OnSurfaceVariant | Outline | accent at 7:1 on black |
 * |-------------------------------|-----------|------------------|---------|------------------------|
 * | 0.35, the old floor           | 2.0:1     | 1.0:1 (#A6A8B2)  | 1.9:1   | 1.2:1                  |
 * | 0.62, the old tab bar         | 5.0:1     | 2.6:1 (#A6A8B2)  | 1.4:1   | 2.1:1                  |
 * | 0.72 + raised 0.05, the floor | 6.4:1     | 4.7:1 (#C4C6D0)  | 3.1:1   | lifted to 4.5:1        |
 *
 * 0.72 is the sheet's default opacity, so the default sheet looks as it did; the user's slider now
 * only darkens it. The chrome with no setting (tab bar, stage icons, mini player) draws at the
 * floor too, a little darker than the old 0.62.
 */
object GeodeGlass {
    /**
     * The range the user's sheet-opacity setting moves the glass in. The floor is the lowest alpha
     * at which every text role on [WorstBackdrop] still clears 4.5:1 (secondary text and the
     * fallback accent are the tight ones, about 4.6:1); the old 0.35 left body text at 2:1 over a
     * white frame. The ceiling keeps a trace of the visual through the sheet.
     */
    const val MATTE_FILL_MIN = 0.72f
    const val MATTE_FILL_MAX = 0.92f

    /** The default glass is the floor: the lowest alpha that passes contrast ([MATTE_FILL_MIN]). */
    const val MATTE_FILL = MATTE_FILL_MIN

    /**
     * A raised control on glass (a chip, the chosen row): white at this alpha over the matte. It
     * lightens what lies under the text, so it counts in [WorstBackdrop]; at the old 0.10 secondary
     * text on a chosen row could not reach 4.5:1 without crowding OnSurface.
     */
    const val FILL_RAISED = 0.05f

    /** Disabled content: exempt from the contrast minimums (WCAG 1.4.3; M3 says the same). */
    const val DISABLED_CONTENT = 0.38f

    /**
     * The lightest thing text on the glass can sit on: the glass at [MATTE_FILL_MIN] over a pure
     * white frame, with [FILL_RAISED] on top, a grey of about #515151 (relative luminance 0.081).
     * Every text colour drawn on glass is held to the minimums against this ([contrastOnGlass]),
     * and the accent rule lifts the visual's colour until it reads here too ([VisualAccent]).
     */
    val WorstBackdrop: Color =
        Color.White
            .copy(alpha = FILL_RAISED)
            .compositeOver(GeodeColors.Canvas.copy(alpha = MATTE_FILL_MIN).compositeOver(Color.White))

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

/**
 * Minimum contrast ratios, from WCAG 2.1 AA, which Material 3 › Typography › Color & contrast
 * adopts: "Small text should achieve a contrast ratio of 4.5:1", large text 3:1.
 *
 * A ratio is (L1 + 0.05) / (L2 + 0.05), L1 the lighter colour's relative luminance and L2 the
 * darker's. Relative luminance is 0.2126 R + 0.7152 G + 0.0722 B on linear channels, each sRGB
 * channel c (0..1) linearised as c / 12.92 at or below 0.04045, else ((c + 0.055) / 1.055)^2.4
 * (WCAG 2.1, "relative luminance", w3.org/TR/WCAG21/#dfn-relative-luminance). Compose's
 * `Color.luminance()` is that formula, so [contrastRatio] is too.
 */
object GeodeContrast {
    /** Small text (under 18 sp, or 14 sp bold): WCAG 1.4.3. */
    const val BODY_MIN = 4.5f

    /** Large text, icons and control edges: WCAG 1.4.3's large-text and 1.4.11's non-text minimum. */
    const val LARGE_MIN = 3f
}
