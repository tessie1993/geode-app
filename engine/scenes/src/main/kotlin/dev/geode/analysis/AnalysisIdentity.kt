package dev.geode.analysis

import dev.geode.engine.audio.AnalysisFft

object AnalysisIdentity {
    // 3: the FFT follows the sample rate (AnalysisFft.sizeFor) and frames are stamped with the
    // hop the native session actually advances, so timelines of >48 kHz files changed.
    const val ALGORITHM_VERSION: Int = 3

    val CURRENT: String =
        "alg=$ALGORITHM_VERSION" +
            "|fft=${AnalysisFft.REFERENCE_FFT_SIZE}@${AnalysisFft.REFERENCE_SAMPLE_RATE_HZ}" +
            "|bands=${AnalysisEngine.DEFAULT_BAND_COUNT}" +
            "|liveHz=${AnalysisEngine.HOP_RATE_HZ}" +
            "|offlineHz=${OfflineAnalyzer.OFFLINE_HOP_RATE_HZ}"
}
