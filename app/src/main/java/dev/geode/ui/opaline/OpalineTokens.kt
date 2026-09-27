package dev.geode.ui.opaline

import androidx.annotation.StringRes
import androidx.compose.runtime.Immutable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.luminance
import dev.geode.R
import kotlin.math.ln
import kotlin.math.pow
import kotlin.math.sqrt

/**
 * The six coordinated themes of `ui-system/Opaline-3D-Library`.
 *
 * Colours are the renderer values from `src/materials.js` `THEMES`. `catalogue/design-tokens.json`
 * repeats the accent, environment and nature colours; its Tidal gel (#c6f0f3) is a design target
 * that differs from the material gel (#99cedd), and the tokens file defers to `materials.js` for
 * shader values, so the material value is what renders here.
 */
enum class OpalineThemeId(
    @param:StringRes val labelRes: Int,
    val gel: Long,
    val blue: Long,
    val deep: Long,
    val accent: Long,
    val water: Long,
    val stone: Long,
    val leaf: Long,
    val background: Long,
) {
    TIDAL(R.string.opaline_theme_tidal, 0xFF99CEDD, 0xFF448DC1, 0xFF285F80, 0xFFA8FFF1, 0xFF5BAAAA, 0xFF526D78, 0xFF467C66, 0xFF122B3C),
    OPAL(R.string.opaline_theme_opal, 0xFFF1E5F3, 0xFF9DAEDE, 0xFFC99FC6, 0xFFFFF0C9, 0xFF95BDC9, 0xFFA8A8B4, 0xFF77978C, 0xFF252C44),
    MOSS(R.string.opaline_theme_moss, 0xFFCEE7CB, 0xFF77B8A3, 0xFF426C55, 0xFFD9F8A7, 0xFF739E8A, 0xFF5E7265, 0xFF7FA665, 0xFF162F28),
    OBSIDIAN(
        R.string.opaline_theme_obsidian,
        0xFF788296,
        0xFF657CA8,
        0xFF27384C,
        0xFFA3DCF0,
        0xFF405C70,
        0xFF303A47,
        0xFF476477,
        0xFF0B111D,
    ),
    AURORA(R.string.opaline_theme_aurora, 0xFFD0CEF5, 0xFF8DA5EF, 0xFF8371BD, 0xFFA0FFE0, 0xFF668BA9, 0xFF616C86, 0xFF829C99, 0xFF171E39),
    AMBER(R.string.opaline_theme_amber, 0xFFF0D7AF, 0xFFD4AB7C, 0xFFB77744, 0xFFFFE4A3, 0xFF92B5A4, 0xFF877668, 0xFF8B976A, 0xFF302C2A),
    ;

    val palette: OpalinePalette by lazy { OpalinePalette.of(this) }

    companion object {
        val DEFAULT = TIDAL

        fun fromName(name: String?): OpalineThemeId = entries.firstOrNull { it.name == name } ?: DEFAULT
    }
}

/**
 * Application colour roles derived from one theme.
 *
 * The library's gel is the colour of action bodies (buttons, thumbs, beads), and dense gel
 * scatters light, so labels on it use [ink]. Structural slabs (panels, rows, dock bed) are a
 * deep mineral tone lifted from the environment toward the theme's blue, and carry [text].
 * `OpalinePaletteTest` pins every text/body pairing to WCAG AA contrast.
 */
@Immutable
data class OpalinePalette(
    val theme: OpalineThemeId,
    val gel: Color,
    val blue: Color,
    val deep: Color,
    val accent: Color,
    val water: Color,
    val stone: Color,
    val leaf: Color,
    val environment: Color,
    val environmentLow: Color,
    val slab: Color,
    val text: Color,
    val textMuted: Color,
    val ink: Color,
    val onGel: Color,
    val error: Color,
    val shadow: Color,
) {
    companion object {
        private val PEARL = Color(0xFFF2FCFA)
        private val ERROR = Color(0xFFFFADB5)

        fun of(theme: OpalineThemeId): OpalinePalette {
            val environment = Color(theme.background)
            val blue = Color(theme.blue)
            val gel = Color(theme.gel)
            val slab = lerp(environment, blue, SLAB_LIFT)
            val ink = lerp(environment, Color.Black, INK_DARKEN)
            return OpalinePalette(
                theme = theme,
                gel = gel,
                blue = blue,
                deep = Color(theme.deep),
                accent = Color(theme.accent),
                water = Color(theme.water),
                stone = Color(theme.stone),
                leaf = Color(theme.leaf),
                environment = environment,
                environmentLow = lerp(environment, Color.Black, ENVIRONMENT_FALLOFF),
                slab = slab,
                text = PEARL,
                textMuted = lerp(PEARL, gel, MUTED_TOWARD_GEL).copy(alpha = MUTED_ALPHA),
                ink = ink,
                onGel = if (contrast(ink, gel) >= contrast(PEARL, gel)) ink else PEARL,
                error = ERROR,
                shadow = lerp(environment, Color.Black, SHADOW_DARKEN),
            )
        }

        /** WCAG 2 contrast ratio between two opaque colours. */
        fun contrast(
            a: Color,
            b: Color,
        ): Float {
            val la = a.luminance()
            val lb = b.luminance()
            return (maxOf(la, lb) + 0.05f) / (minOf(la, lb) + 0.05f)
        }

        private const val SLAB_LIFT = 0.22f
        private const val INK_DARKEN = 0.35f
        private const val ENVIRONMENT_FALLOFF = 0.45f
        private const val MUTED_TOWARD_GEL = 0.25f
        private const val MUTED_ALPHA = 0.82f
        private const val SHADOW_DARKEN = 0.7f
    }
}

/**
 * One shared light rig (`design-tokens.json` `lighting`): every body is lit by the same key and
 * fill. The key direction is the token's (-0.45, 0.8, 0.5) with +Y up, flipped into screen space
 * (+Y down) and normalised. The tokens give fill and rim temperatures but no fill direction; the
 * fill is placed opposite the key, below and to the right, so shoulders facing away from the key
 * still read as translucent rather than black.
 */
object OpalineLight {
    val key: FloatArray = normalized(-0.45f, -0.8f, 0.5f)
    val fill: FloatArray = normalized(0.55f, 0.6f, 0.58f)
    val keyColor: Color = kelvin(KEY_KELVIN)
    val fillColor: Color = kelvin(FILL_KELVIN)
    val rimColor: Color = kelvin(RIM_KELVIN)

    private const val KEY_KELVIN = 5600f
    private const val FILL_KELVIN = 7200f
    private const val RIM_KELVIN = 6400f

    private fun normalized(
        x: Float,
        y: Float,
        z: Float,
    ): FloatArray {
        val length = sqrt(x * x + y * y + z * z)
        return floatArrayOf(x / length, y / length, z / length)
    }

    /** Tanner Helland's blackbody approximation, valid over 1000-40000 K. */
    internal fun kelvin(kelvin: Float): Color {
        val t = kelvin / 100f
        val red = if (t <= 66f) 255f else 329.698727446f * (t - 60f).pow(-0.1332047592f)
        val green =
            if (t <= 66f) {
                99.4708025861f * ln(t) - 161.1195681661f
            } else {
                288.1221695283f * (t - 60f).pow(-0.0755148492f)
            }
        val blue =
            when {
                t >= 66f -> 255f
                t <= 19f -> 0f
                else -> 138.5177312231f * ln(t - 10f) - 305.0447927307f
            }
        return Color(red.coerceIn(0f, 255f) / 255f, green.coerceIn(0f, 255f) / 255f, blue.coerceIn(0f, 255f) / 255f)
    }
}

/**
 * The distinct physical families of `materials.js`, not one universal glass.
 *
 * Roughness, clearcoat, clearcoat roughness, IOR, iridescence and the `cloud`/`flow`/`grain`
 * shader fields are the library's values, including its separate Opal ("pearl") tuning of gel
 * and blue. [coverage] has no library counterpart: a raster body cannot refract what is behind
 * it, so it states how much of the environment the body hides. Dense gel scatters most light
 * ("dense optical depth", `AI-HANDOFF.md`) and hides nearly all of it; shell, water and film
 * stay clear.
 */
enum class OpalineMaterial(
    private val roughness: Float,
    private val pearlRoughness: Float,
    val clearcoat: Float,
    private val clearcoatRoughness: Float,
    private val pearlClearcoatRoughness: Float,
    val ior: Float,
    val iridescence: Float,
    private val cloud: Float,
    private val pearlCloud: Float,
    val flow: Float,
    val grain: Float,
    val coverage: Float,
) {
    GEL(0.15f, 0.24f, 0.8f, 0.085f, 0.13f, 1.39f, 0.16f, 0.18f, 0.42f, 0.34f, 0.025f, 0.95f),
    BLUE(0.12f, 0.17f, 0.9f, 0.07f, 0.07f, 1.4f, 0.12f, 0.14f, 0.36f, 0.23f, 0.018f, 0.93f),
    WATER(0.045f, 0.045f, 0.35f, 0.035f, 0.035f, 1.333f, 0f, 0f, 0f, 0f, 0f, 0.55f),
    SHELL(0.065f, 0.065f, 1f, 0.045f, 0.045f, 1.46f, 0f, 0f, 0f, 0f, 0.01f, 0.4f),
    PIGMENT(0.2f, 0.2f, 0.6f, 0.13f, 0.13f, 1.37f, 0f, 0.78f, 0.78f, 1.1f, 0.022f, 0.9f),
    FILM(0.035f, 0.035f, 1f, 0.025f, 0.025f, 1.333f, 1f, 0.015f, 0.015f, 0.2f, 0f, 0.3f),
    NACRE(0.24f, 0.24f, 0.8f, 0.12f, 0.12f, 1.52f, 0.72f, 0.27f, 0.27f, 0.07f, 0.042f, 0.97f),
    STONE(0.69f, 0.69f, 0.74f, 0.14f, 0.14f, 1.5f, 0f, 0.25f, 0.25f, 0f, 0.25f, 1f),
    GLOW(0.2f, 0.2f, 1f, 0.08f, 0.08f, 1.37f, 0f, 0.18f, 0.18f, 0.5f, 0f, 0.95f),
    ;

    /** Normal-incidence Fresnel reflectance for this family's index of refraction. */
    val f0: Float get() = ((ior - 1f) / (ior + 1f)).let { it * it }

    fun roughness(theme: OpalineThemeId): Float = if (theme == OpalineThemeId.OPAL) pearlRoughness else roughness

    fun clearcoatRoughness(theme: OpalineThemeId): Float =
        if (theme == OpalineThemeId.OPAL) pearlClearcoatRoughness else clearcoatRoughness

    fun cloud(theme: OpalineThemeId): Float = if (theme == OpalineThemeId.OPAL) pearlCloud else cloud
}

/**
 * `design-tokens.json` `motion`: interaction springs step at a fixed 1/120 s, and contact always
 * releases or cancels ownership. [pressFrequency]/[pressDamping] and [valueFrequency]/[valueDamping]
 * are `MotionController`'s `pressure` and `value` springs in `src/motion.js`.
 */
object OpalineMotion {
    const val FIXED_DT = 1f / 120f
    const val PRESS_FREQUENCY = 4f
    const val PRESS_DAMPING = 0.68f
    const val VALUE_FREQUENCY = 4f
    const val VALUE_DAMPING = 0.8f

    /** `MotionController` stretches a moving slider thumb by up to 28 % along its travel. */
    const val THUMB_STRETCH_MAX = 0.28f
    const val THUMB_STRETCH_PER_VELOCITY = 0.12f
}
