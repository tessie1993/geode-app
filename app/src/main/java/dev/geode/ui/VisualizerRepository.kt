package dev.geode.ui

import kotlinx.coroutines.flow.StateFlow

interface VisualizerRepository {
    val viz: StateFlow<VizUiState>

    val activeMilkPath: StateFlow<String?>
}

internal class SessionVisualizerRepository(
    private val session: PlayerSession,
) : VisualizerRepository {
    override val viz: StateFlow<VizUiState> get() = session.vizState

    override val activeMilkPath: StateFlow<String?> get() = session.activeMilkPath
}
