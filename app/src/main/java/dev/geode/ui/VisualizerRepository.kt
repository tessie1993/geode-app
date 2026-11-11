package dev.geode.ui

import dev.geode.data.VizUiState
import kotlinx.coroutines.flow.StateFlow

interface VisualizerRepository {
    val viz: StateFlow<VizUiState>

    val activeMilkPath: StateFlow<String?>

    /** What Liquid mode is doing right now (0 off, 1 full, 2 lite, 3 warp, 4 paused), from the live renderer. */
    val liquidMode: StateFlow<Int>
}

internal class SessionVisualizerRepository(
    private val session: PlayerSession,
) : VisualizerRepository {
    override val viz: StateFlow<VizUiState> get() = session.vizState

    override val activeMilkPath: StateFlow<String?> get() = session.activeMilkPath

    override val liquidMode: StateFlow<Int> get() = session.liquidMode
}
