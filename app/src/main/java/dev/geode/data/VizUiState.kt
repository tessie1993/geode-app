package dev.geode.data

import dev.geode.engine.bridge.SceneParams
import dev.geode.model.VizPlaylistEntry
import dev.geode.pro.FreeTier

data class VizUiState(
    /** A fresh install (no saved live look) opens on the free showpiece, for Pro and free alike. */
    val sceneId: String = FreeTier.FIRST_STYLE_ID,
    val attack: Float = NativeLookDefaults.attack,
    val decay: Float = NativeLookDefaults.decay,
    val shaderError: String? = null,
    val presets: List<Preset> = emptyList(),
    val params: SceneParams = SceneParams.DEFAULT,
    val vizPlaylist: List<VizPlaylistEntry> = emptyList(),
    val vizPlaylistEnabled: Boolean = false,
    val vizPlaylistIntervalSec: Int = NativeLookDefaults.playlistTimerSec,
    /** Native's transition id (a built-in such as "fade", "random", or a gl-transitions name). */
    val transitionId: String = NativeLookDefaults.transitionId,
    val transitionDurationSec: Float = NativeLookDefaults.transitionMs / MS_PER_SECOND,
    val randomEnabled: Boolean = false,
    val randomIntervalSec: Int = NativeLookDefaults.styleTimerSec,
    val randomIncludeStyles: Boolean = true,
    val randomIncludePresets: Boolean = true,
    val randomIncludeMilk: Boolean = false,
)

private const val MS_PER_SECOND = 1000f
