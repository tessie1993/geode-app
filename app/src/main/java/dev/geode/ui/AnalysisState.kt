package dev.geode.ui

sealed interface AnalysisState {
    data object Idle : AnalysisState

    data class Running(val progress: Float) : AnalysisState

    data class Failed(val message: String) : AnalysisState
}

val AnalysisState.isRunning: Boolean
    get() =
        when (this) {
            is AnalysisState.Running -> true
            AnalysisState.Idle, is AnalysisState.Failed -> false
        }

val AnalysisState.progressOrZero: Float
    get() =
        when (this) {
            is AnalysisState.Running -> progress
            AnalysisState.Idle, is AnalysisState.Failed -> 0f
        }
