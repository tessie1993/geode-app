package dev.geode.analysis

import dev.geode.engine.audio.AnalysisFft

object AnalysisIdentity {
    // 3: the FFT follows the sample rate (AnalysisFft.sizeFor) and frames are stamped with the
    // hop the native session actually advances, so timelines of >48 kHz files changed.
    // 4: the hop is a quarter of the window (no fixed 60 Hz frame rate), frames are stamped at
    // the window's end, the tempo has no preferred BPM and structure events are loudness-relative.
    const val ALGORITHM_VERSION: Int = 4

    val CURRENT: String =
        "alg=$ALGORITHM_VERSION" +
            "|fft=${AnalysisFft.REFERENCE_FFT_SIZE}@${AnalysisFft.REFERENCE_SAMPLE_RATE_HZ}" +
            "|bands=${AnalysisEngine.DEFAULT_BAND_COUNT}"
}
