package dev.geode.ui.theme

import android.content.ContentResolver
import android.database.ContentObserver
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import androidx.compose.animation.core.FiniteAnimationSpec
import androidx.compose.animation.core.snap
import androidx.compose.animation.core.spring
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.platform.LocalContext
import dev.geode.data.SystemMotion

/**
 * The app's motion: Material 3 Expressive springs, or none that travel when the user asked for
 * reduced motion. Read it through [LocalGeodeMotion] (`LocalGeodeMotion.current.defaultSpatialSpec()`)
 * rather than inventing tweens.
 *
 * The six specs mirror Material's `MotionScheme`, name for name, because that is where they will
 * go: in the Material 3 release the BOM pins (1.4.0) `MotionScheme`, `MaterialTheme(motionScheme =)`
 * and `MaterialTheme.motionScheme` are all still internal, so the theme cannot hand them to M3's
 * own components yet. When a stable release opens them, this interface becomes a `MotionScheme`
 * and [geodeMotionScheme] goes to `MaterialTheme`, with no change for the callers. Until then M3
 * components keep their default motion, which already follows the system's animator scale.
 *
 * Expressive, not standard: the springs overshoot a little on things that move (spatial) and
 * settle without bounce on things that fade or change colour (effects). The values are M3's
 * Expressive tokens ([GeodeMotion]).
 *
 * Reduced motion is the system's "Remove animations" (animator duration scale 0) or the app's
 * own setting. Things then jump instead of travelling ([snap]): no slide, no overshoot, no
 * zoom, which is what vestibular sensitivity needs. Fades and colour changes keep their short
 * effects springs, as WCAG 2.3.3 allows, so state changes are still visible.
 */
@Stable
interface GeodeMotionScheme {
    fun <T> defaultSpatialSpec(): FiniteAnimationSpec<T>

    fun <T> fastSpatialSpec(): FiniteAnimationSpec<T>

    fun <T> slowSpatialSpec(): FiniteAnimationSpec<T>

    fun <T> defaultEffectsSpec(): FiniteAnimationSpec<T>

    fun <T> fastEffectsSpec(): FiniteAnimationSpec<T>

    fun <T> slowEffectsSpec(): FiniteAnimationSpec<T>
}

/** The scheme for the user's reduced-motion choice. */
fun geodeMotionScheme(reducedMotion: Boolean): GeodeMotionScheme = if (reducedMotion) ReducedMotionScheme else ExpressiveMotionScheme

/** The theme's motion, provided by `CrystalMaterialTheme`. */
val LocalGeodeMotion = staticCompositionLocalOf<GeodeMotionScheme> { ExpressiveMotionScheme }

/** M3 Expressive's six springs (`MotionSchemeKeyTokens`, expressive set). */
private object ExpressiveMotionScheme : GeodeMotionScheme {
    override fun <T> defaultSpatialSpec(): FiniteAnimationSpec<T> = spring(GeodeMotion.SPATIAL_DAMPING, GeodeMotion.SPATIAL_DEFAULT)

    override fun <T> fastSpatialSpec(): FiniteAnimationSpec<T> = spring(GeodeMotion.SPATIAL_FAST_DAMPING, GeodeMotion.SPATIAL_FAST)

    override fun <T> slowSpatialSpec(): FiniteAnimationSpec<T> = spring(GeodeMotion.SPATIAL_DAMPING, GeodeMotion.SPATIAL_SLOW)

    override fun <T> defaultEffectsSpec(): FiniteAnimationSpec<T> = effects(GeodeMotion.EFFECTS_DEFAULT)

    override fun <T> fastEffectsSpec(): FiniteAnimationSpec<T> = effects(GeodeMotion.EFFECTS_FAST)

    override fun <T> slowEffectsSpec(): FiniteAnimationSpec<T> = effects(GeodeMotion.EFFECTS_SLOW)
}

/** Nothing travels; fades and colour changes stay as they are. */
private object ReducedMotionScheme : GeodeMotionScheme {
    override fun <T> defaultSpatialSpec(): FiniteAnimationSpec<T> = snap()

    override fun <T> fastSpatialSpec(): FiniteAnimationSpec<T> = snap()

    override fun <T> slowSpatialSpec(): FiniteAnimationSpec<T> = snap()

    override fun <T> defaultEffectsSpec(): FiniteAnimationSpec<T> = effects(GeodeMotion.EFFECTS_DEFAULT)

    override fun <T> fastEffectsSpec(): FiniteAnimationSpec<T> = effects(GeodeMotion.EFFECTS_FAST)

    override fun <T> slowEffectsSpec(): FiniteAnimationSpec<T> = effects(GeodeMotion.EFFECTS_SLOW)
}

private fun <T> effects(stiffness: Float): FiniteAnimationSpec<T> = spring(dampingRatio = 1f, stiffness = stiffness)

/**
 * Whether the system's "Remove animations" (Settings › Accessibility) is on, kept live.
 *
 * Android has no separate "reduce motion" flag; removing animations sets the animator duration
 * scale to 0, the signal the platform's own views follow. It is watched, not read once, so
 * turning it on while the app is open takes effect without a restart.
 */
@Composable
fun rememberSystemReducedMotion(): Boolean {
    val resolver = LocalContext.current.contentResolver
    var reduced by remember(resolver) { mutableStateOf(animationsRemoved(resolver)) }
    DisposableEffect(resolver) {
        val observer =
            object : ContentObserver(Handler(Looper.getMainLooper())) {
                override fun onChange(selfChange: Boolean) {
                    reduced = animationsRemoved(resolver)
                }
            }
        resolver.registerContentObserver(Settings.Global.getUriFor(Settings.Global.ANIMATOR_DURATION_SCALE), false, observer)
        // It may have changed between the first read and the registration.
        reduced = animationsRemoved(resolver)
        onDispose { resolver.unregisterContentObserver(observer) }
    }
    return reduced
}

private fun animationsRemoved(resolver: ContentResolver): Boolean = SystemMotion.isReduced(resolver)
