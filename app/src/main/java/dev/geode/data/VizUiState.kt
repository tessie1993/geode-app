package dev.geode.data

import dev.geode.pro.FreeTier
import dev.geode.render.TransitionStyle
import dev.geode.render.scene.SceneParams

data class VizUiState(
    /** A fresh install (no saved live look) opens on the free showpiece, for Pro and free alike. */
    val sceneId: String = FreeTier.FIRST_STYLE_ID,
    val attack: Float = 0.6f,
    val decay: Float = 0.12f,
    val shaderError: String? = null,
    val presets: List<Preset> = emptyList(),
    val params: SceneParams = SceneParams.DEFAULT,
    val vizPlaylist: List<VizPlaylistEntry> = emptyList(),
    val vizPlaylistEnabled: Boolean = false,
    val vizPlaylistIntervalSec: Int = 30,
    val transitionStyle: TransitionStyle = TransitionStyle.FADE,
    val transitionId: String = TransitionStyle.FADE.name.lowercase(),
    val transitionDurationSec: Float = 1.2f,
    val randomEnabled: Boolean = false,
    val randomIntervalSec: Int = 20,
    val randomIncludeStyles: Boolean = true,
    val randomIncludePresets: Boolean = true,
    val randomIncludeMilk: Boolean = false,
    val randomizeColors: Boolean = false,
)
