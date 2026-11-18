package dev.geode.ui

import dev.geode.data.VizUiState
import kotlinx.coroutines.flow.StateFlow

interface VisualizerRepository {
    val viz: StateFlow<VizUiState>

    /** What Liquid mode is doing right now (0 off, 1 full), from the live renderer. */
    val liquidMode: StateFlow<Int>
}

internal class SessionVisualizerRepository(
    private val session: PlayerSession,
) : VisualizerRepository {
    override val viz: StateFlow<VizUiState> get() = session.vizState

    override val liquidMode: StateFlow<Int> get() = session.liquidMode
}
