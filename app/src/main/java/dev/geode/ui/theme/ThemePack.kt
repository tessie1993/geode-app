package dev.geode.ui.theme

import androidx.annotation.DrawableRes
import androidx.annotation.RawRes
import androidx.compose.ui.graphics.Color

data class ThemePack(
    val slug: String,
    val name: String,
    val stone: String,
    val isLight: Boolean,
    val palette: StonePalette,
    val motion: StoneMotion,
    val material: StoneMaterial,
    val sounds: StoneSounds,
    val surfaces: Map<StoneComponent, StoneStateArt>,
) {
    fun surface(component: StoneComponent): StoneStateArt =
        requireNotNull(surfaces[component]) {
            "theme pack '$slug' is missing surface art for $component"
        }
}

data class StonePalette(
    val background: Color,
    val backgroundDeep: Color,
    val surface: Color,
    val surfaceHigh: Color,
    val primary: Color,
    val secondary: Color,
    val accent: Color,
    val glow: Color,
    val onBackground: Color,
    val onSurface: Color,
    val muted: Color,
    val outline: Color,
    val danger: Color,
)

data class StoneMotion(
    val pressDurationMs: Int,
    val pressScale: Float,
    val innerGlowGain: Float,
    val releaseDurationMs: Int,
    val focusDurationMs: Int,
    val edgeLightGain: Float,
    val selectedDurationMs: Int,
    val reduceMotionCrossfadeMs: Int,
)

data class StoneMaterial(
    @param:DrawableRes val tile: Int,
    @param:DrawableRes val glowOverlay: Int,
    @param:DrawableRes val refractionOverlay: Int,
    @param:DrawableRes val ambientPortrait: Int,
    @param:DrawableRes val ambientLandscape: Int,
    @param:DrawableRes val ambientSquare: Int,
    val backgroundOpacity: Float,
    val surfaceOpacity: Float,
    val disabledOpacity: Float,
)

data class StoneSounds(
    @param:RawRes val click: Int,
    @param:RawRes val confirm: Int,
    @param:RawRes val swoop: Int,
)

enum class StoneComponent {
    ALBUM_TILE,
    BOTTOM_SHEET,
    CARD,
    CHIP,
    COMPACT_BUTTON,
    DIALOG,
    ICON_BUTTON,
    KNOB,
    LIST_ROW,
    MINI_PLAYER,
    NAVIGATION_BAR,
    PRIMARY_BUTTON,
    PROGRESS_RING,
    SECONDARY_BUTTON,
    SLIDER_THUMB,
    SLIDER_TRACK,
    TEXT_FIELD,
    TOGGLE,
}

enum class StoneState {
    DEFAULT,
    FOCUSED,
    PRESSED,
    SELECTED,
    DISABLED,
}

data class StoneStateArt(
    @param:DrawableRes val default: Int,
    @param:DrawableRes val focused: Int,
    @param:DrawableRes val pressed: Int,
    @param:DrawableRes val selected: Int,
    @param:DrawableRes val disabled: Int,
) {
    @DrawableRes
    fun forState(state: StoneState): Int =
        when (state) {
            StoneState.DEFAULT -> default
            StoneState.FOCUSED -> focused
            StoneState.PRESSED -> pressed
            StoneState.SELECTED -> selected
            StoneState.DISABLED -> disabled
        }
}

fun stoneStateOf(
    enabled: Boolean = true,
    pressed: Boolean = false,
    selected: Boolean = false,
    focused: Boolean = false,
): StoneState =
    when {
        !enabled -> StoneState.DISABLED
        pressed -> StoneState.PRESSED
        selected -> StoneState.SELECTED
        focused -> StoneState.FOCUSED
        else -> StoneState.DEFAULT
    }
