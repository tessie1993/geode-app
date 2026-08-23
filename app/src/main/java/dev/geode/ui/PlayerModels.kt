package dev.geode.ui

import androidx.media3.common.Player
import dev.geode.analysis.IntelligenceMode
import dev.geode.data.Preset
import dev.geode.render.TransitionStyle
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams

data class PlayerUiState(
    val isPlaying: Boolean = false,
    val positionMs: Long = 0,
    val durationMs: Long = 0,
    val title: String? = null,
    val artist: String? = null,
    val hasMedia: Boolean = false,
    val queueSize: Int = 0,
    val queueIndex: Int = 0,
    val shuffle: Boolean = false,
    val repeatMode: Int = Player.REPEAT_MODE_OFF,
)

data class VizUiState(
    val sceneId: String = SceneIds.DEFAULT,
    val intelligenceMode: IntelligenceMode = IntelligenceMode.MANUAL,
    val suggestedSceneId: String? = null,
    val attack: Float = 0.6f,
    val decay: Float = 0.12f,
    val analysis: AnalysisState = AnalysisState.Idle,
    val bpm: Float = 0f,
    val sections: List<Long> = emptyList(),
    val shaderError: String? = null,
    val presets: List<Preset> = emptyList(),
    val params: SceneParams = SceneParams.DEFAULT,
    val vizPlaylist: List<VizPlaylistEntry> = emptyList(),
    val vizPlaylistEnabled: Boolean = false,
    val vizPlaylistIntervalSec: Int = 30,
    val vizPlaylistIntelligent: Boolean = false,
    val transitionStyle: TransitionStyle = TransitionStyle.FADE,
    val transitionId: String = TransitionStyle.FADE.name.lowercase(),
    val transitionDurationSec: Float = 1.2f,
    val randomEnabled: Boolean = false,
    val randomIntervalSec: Int = 20,
    val randomOnBeat: Boolean = true,
    val randomIncludeStyles: Boolean = true,
    val randomIncludePresets: Boolean = true,
    val randomIncludeMilk: Boolean = false,
    val randomizeColors: Boolean = false,
    val sectionStaging: Boolean = false,
)

data class VizPlaylistEntry(
    val sceneId: String,
    val presetName: String? = null,
    val milkPath: String? = null,
    val label: String,
)

data class VizApply(
    val milkPath: String? = null,
    val customShader: String? = null,
    val sceneId: String? = null,
)

data class MilkFile(
    val name: String,
    val path: String,
)

data class QueueTrack(
    val uri: String,
    val title: String = "",
    val artist: String = "",
)

data class QueueUiState(
    val tracks: List<QueueTrack> = emptyList(),
    val index: Int = 0,
)
