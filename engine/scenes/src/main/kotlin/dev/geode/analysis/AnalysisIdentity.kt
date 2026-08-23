package dev.geode.analysis

object AnalysisIdentity {
    const val ALGORITHM_VERSION: Int = 2

    val CURRENT: String =
        "alg=$ALGORITHM_VERSION" +
            "|fft=${AnalysisEngine.DEFAULT_FFT_SIZE}" +
            "|bands=${AnalysisEngine.DEFAULT_BAND_COUNT}" +
            "|liveHz=${AnalysisEngine.HOP_RATE_HZ}" +
            "|offlineHz=${OfflineAnalyzer.OFFLINE_HOP_RATE_HZ}"
}
