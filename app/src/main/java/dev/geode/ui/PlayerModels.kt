package dev.geode.ui

import androidx.media3.common.Player

/**
 * What playback looks like, apart from the position. The position moves every poll tick, and as a
 * field here it made every collector of this state (the shell, the Player, the live view)
 * recompose twice a second for as long as music played; it lives in its own flow instead
 * ([PlaybackRepository.positionMs]), read only by the seek bars and clocks that draw it.
 */
data class PlayerUiState(
    val isPlaying: Boolean = false,
    val durationMs: Long = 0,
    val title: String? = null,
    val artist: String? = null,
    val hasMedia: Boolean = false,
    /** The loaded track's key, emitted with its title so the heart and the art never show another track's. */
    val trackUri: String? = null,
    val queueSize: Int = 0,
    val queueIndex: Int = 0,
    val shuffle: Boolean = false,
    val repeatMode: Int = Player.REPEAT_MODE_OFF,
)

data class VizApply(
    val milkPath: String? = null,
)
