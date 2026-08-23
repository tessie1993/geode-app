package dev.geode.ui

import android.app.Application
import android.net.Uri
import dev.geode.RingLog
import dev.geode.analysis.AnalysisCache
import dev.geode.analysis.FeatureTimeline
import dev.geode.analysis.IntelligenceMode
import dev.geode.analysis.OfflineAnalyzer
import dev.geode.analysis.SceneSuggester
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

internal class TrackAnalysisController(
    private val application: Application,
    private val scope: CoroutineScope,
    private val host: Host,
) {
    interface Host {
        val currentUri: Uri?
        val guiPrefs: GuiPrefs
        val vizState: MutableStateFlow<VizUiState>
        val playerPositionMs: Long
        val presetLocked: Boolean

        fun applyKeyColor(key: String)

        fun noteAnalysis(
            uri: Uri,
            timeline: FeatureTimeline,
        )
    }

    private val offlineAnalyzer = OfflineAnalyzer(application)

    private val _waveform = MutableStateFlow<FloatArray?>(null)
    val waveform: StateFlow<FloatArray?> = _waveform

    private var timelineBacking: FeatureTimeline? = null

    var timeline: FeatureTimeline?
        get() = timelineBacking
        set(value) {
            timelineBacking = value
            _waveform.value = value?.let(::waveformOf)
        }

    private var analyzingUri: Uri? = null
    private var beatRedecideJob: Job? = null
    private var autoSuggestKey = Long.MIN_VALUE

    fun redecideCachedBeats(prefs: GuiPrefs) {
        val base = timeline ?: return
        val uri = host.currentUri
        beatRedecideJob?.cancel()
        beatRedecideJob =
            scope.launch(Dispatchers.Default) {
                delay(REDECIDE_DEBOUNCE_MS)
                val updated = base.withBeatSensitivity(prefs.beatSensitivity, prefs.effectiveBeatMinIntervalMs)
                val now = host.guiPrefs
                val stillCurrent =
                    now.beatSensitivity == prefs.beatSensitivity &&
                        now.effectiveBeatMinIntervalMs == prefs.effectiveBeatMinIntervalMs
                if (stillCurrent && host.currentUri == uri) timeline = updated
            }
    }

    suspend fun analyzeCached(
        uri: Uri,
        onProgress: (Float) -> Unit,
    ): FeatureTimeline {
        val gui = host.guiPrefs
        AnalysisCache
            .load(application, uri, gui.beatSensitivity, gui.effectiveBeatMinIntervalMs)
            ?.let {
                onProgress(1f)
                return it
            }
        return offlineAnalyzer
            .analyze(uri, gui.beatSensitivity, gui.effectiveBeatMinIntervalMs, onProgress)
            .also { AnalysisCache.save(application, uri, it) }
    }

    fun loadCachedForManualMode() {
        val uri = host.currentUri ?: return
        val gui = host.guiPrefs
        scope.launch(Dispatchers.IO) {
            AnalysisCache
                .load(application, uri, gui.beatSensitivity, gui.effectiveBeatMinIntervalMs)
                ?.let { t ->
                    if (host.currentUri == uri) {
                        timeline = t
                        host.vizState.update { it.copy(bpm = t.bpm, sections = t.detectSections()) }
                    }
                }
        }
    }

    fun setIntelligenceMode(mode: IntelligenceMode) {
        host.vizState.update { it.copy(intelligenceMode = mode) }
        if (mode != IntelligenceMode.MANUAL && timeline == null) analyzeCurrentTrack()
    }

    @Suppress("TooGenericExceptionCaught")
    fun analyzeCurrentTrack() {
        val uri = host.currentUri ?: return
        if (analyzingUri == uri) return
        analyzingUri = uri
        host.vizState.update { it.copy(analysis = AnalysisState.Running(0f)) }
        scope.launch(Dispatchers.Default) {
            try {
                val t =
                    analyzeCached(uri) { p ->
                        host.vizState.update { it.copy(analysis = AnalysisState.Running(p)) }
                    }
                host.noteAnalysis(uri, t)
                if (host.currentUri == uri) {
                    withContext(Dispatchers.Main) { host.applyKeyColor(t.key) }
                    timeline = t
                    val suggestion = SceneSuggester.suggestForTrack(t)
                    host.vizState.update {
                        it.copy(
                            analysis = AnalysisState.Idle,
                            bpm = t.bpm,
                            sections = t.detectSections(),
                            suggestedSceneId = suggestion,
                        )
                    }
                    if (analyzingUri == uri) analyzingUri = null
                    withContext(Dispatchers.Main) { applyIntelligence() }
                } else {
                    if (analyzingUri == uri) {
                        analyzingUri = null
                        host.vizState.update { it.copy(analysis = AnalysisState.Idle) }
                        if (host.vizState.value.intelligenceMode != IntelligenceMode.MANUAL) {
                            withContext(Dispatchers.Main) { analyzeCurrentTrack() }
                        }
                    }
                }
            } catch (c: CancellationException) {
                throw c
            } catch (t: Throwable) {
                RingLog.note("Analysis", "track analysis failed", t)
                if (analyzingUri == uri) {
                    analyzingUri = null
                    host.vizState.update { it.copy(analysis = AnalysisState.Idle) }
                }
            }
        }
    }

    @Suppress("ReturnCount")
    fun applyIntelligence() {
        if (host.presetLocked) return
        if (host.vizState.value.intelligenceMode != IntelligenceMode.AUTO) return
        val t = timeline ?: return
        val pos = host.playerPositionMs
        val section = host.vizState.value.sections.count { it <= pos }
        val key = (System.identityHashCode(t).toLong() shl 16) or (section.toLong() and 0xFFFF)
        if (key == autoSuggestKey) return
        autoSuggestKey = key
        val f = t.featuresAt(pos)
        val suggestion =
            SceneSuggester.suggest(
                t.bpm,
                f.rms,
                f.centroid,
                f.pulseConfidence,
                f.chromaConfidence,
                f.stereoWidth,
            )
        host.vizState.update { if (it.sceneId == suggestion) it else it.copy(sceneId = suggestion) }
    }

    @Suppress("ReturnCount")
    private fun waveformOf(timeline: FeatureTimeline): FloatArray? {
        val frames = timeline.frames
        if (frames.size < WAVEFORM_BUCKETS) return null
        val out = FloatArray(WAVEFORM_BUCKETS)
        var peak = 0f
        for (b in 0 until WAVEFORM_BUCKETS) {
            val from = b * frames.size / WAVEFORM_BUCKETS
            val to = ((b + 1) * frames.size / WAVEFORM_BUCKETS).coerceAtMost(frames.size)
            var max = 0f
            for (i in from until to) {
                val v = frames[i].features.rms
                if (v > max) max = v
            }
            out[b] = max
            if (max > peak) peak = max
        }
        if (peak <= 0f) return null
        for (i in out.indices) out[i] = out[i] / peak
        return out
    }

    private companion object {
        const val WAVEFORM_BUCKETS = 240
        const val REDECIDE_DEBOUNCE_MS = 120L
    }
}
