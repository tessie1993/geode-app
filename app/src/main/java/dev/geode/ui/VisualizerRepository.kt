package dev.geode.ui

import dev.geode.analysis.AudioFeatures
import kotlinx.coroutines.flow.StateFlow

interface VisualizerRepository {
    val viz: StateFlow<VizUiState>

    val features: StateFlow<AudioFeatures>

    val waveform: StateFlow<FloatArray?>

    val activeMilkPath: StateFlow<String?>
}

internal class SessionVisualizerRepository(
    private val session: PlayerSession,
) : VisualizerRepository {
    override val viz: StateFlow<VizUiState> get() = session.vizState

    override val features: StateFlow<AudioFeatures> get() = session.features

    override val waveform: StateFlow<FloatArray?> get() = session.waveform

    override val activeMilkPath: StateFlow<String?> get() = session.activeMilkPath
}
