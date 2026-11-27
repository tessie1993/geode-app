package dev.geode.data

import dev.geode.engine.bridge.PerfNative

/**
 * What native published about the render resolution (`core/viz/frame/ResolutionState`) once it had the panel, the view
 * and the settings: read as it is, never derived here.
 */
data class ResolutionMirror(
    /** The stored resolution. */
    val resolution: Int,
    /** The [PerformanceMode] these values are, or -1 for Custom. */
    val preset: Int,
    /** The index into [steps] of the one on show, or -1. */
    val selected: Int,
    /** The resolutions the tab offers on this panel, ascending; [PerfNative.PERF_NATIVE] last. */
    val steps: List<Int>,
    /** The window buffer for the view; 0 until there is a view. */
    val bufferWidth: Int,
    val bufferHeight: Int,
    /** What the resolution draws full screen on the current mode; 0 for an unknown mode. */
    val drawnWidth: Int,
    val drawnHeight: Int,
    val modeWidth: Int,
    val modeHeight: Int,
) {
    val mode: PerformanceMode? get() = PerformanceMode.entries.getOrNull(preset)

    companion object {
        val EMPTY = ResolutionMirror(PerfNative.PERF_NATIVE, -1, -1, emptyList(), 0, 0, 0, 0, 0, 0)

        fun read(): ResolutionMirror {
            val d = IntArray(PerfNative.PERF_DESCRIPTOR_INTS)
            if (!PerfNative.perfResolutionRead(d)) return EMPTY
            return ResolutionMirror(
                resolution = d[PerfNative.PERF_FIELD_RESOLUTION],
                preset = d[PerfNative.PERF_FIELD_PRESET],
                selected = d[PerfNative.PERF_FIELD_SELECTED],
                steps = List(d[PerfNative.PERF_FIELD_STEP_COUNT]) { d[PerfNative.PERF_FIELD_STEPS + it] },
                bufferWidth = d[PerfNative.PERF_FIELD_BUFFER_WIDTH],
                bufferHeight = d[PerfNative.PERF_FIELD_BUFFER_HEIGHT],
                drawnWidth = d[PerfNative.PERF_FIELD_DRAWN_WIDTH],
                drawnHeight = d[PerfNative.PERF_FIELD_DRAWN_HEIGHT],
                modeWidth = d[PerfNative.PERF_FIELD_MODE_WIDTH],
                modeHeight = d[PerfNative.PERF_FIELD_MODE_HEIGHT],
            )
        }
    }
}