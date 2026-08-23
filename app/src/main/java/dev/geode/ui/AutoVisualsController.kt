package dev.geode.ui

import dev.geode.analysis.AudioFeatures
import dev.geode.data.PaletteStore
import dev.geode.data.Preset
import dev.geode.render.LiveSignal
import dev.geode.render.scene.MilkdropEngine
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.flow.StateFlow

/** A switch waits for a hit this strong — the live transient, not a tracked beat. */
private const val STRONG_MOMENT_IMPULSE = 0.6f

internal class AutoVisualsController(
    private val prefsStore: AutoVisualsPrefsStore,
    private val host: Host,
) {
    interface Host {
        val vizState: StateFlow<VizUiState>

        fun updateViz(transform: (VizUiState) -> VizUiState)

        val isPlaying: Boolean
        val positionMs: Long

        fun features(): AudioFeatures

        val presetLocked: Boolean

        fun selectScene(sceneId: String)

        fun applyPreset(preset: Preset)

        fun applyMilk(
            path: String,
            sceneId: String,
        )

        fun analyzeCurrentTrack()

        fun milkFilesAsync(onDone: (List<MilkFile>) -> Unit)
    }

    private var lastVizSwitchMs = 0L
    private var vizPlaylistIndex = 0
    private var lastRandomSwitchMs = 0L
    private val randomRng = kotlin.random.Random(android.os.SystemClock.elapsedRealtime())

    private var cachedMilkFiles: List<MilkFile> = emptyList()

    fun addToVizPlaylist(entry: VizPlaylistEntry) {
        val s = host.vizState.value
        val duplicate =
            s.vizPlaylist.any {
                it == entry || (entry.presetName != null && it.presetName == entry.presetName)
            }
        if (duplicate) return
        host.updateViz { it.copy(vizPlaylist = it.vizPlaylist + entry) }
        persistAutoVisuals()
    }

    fun removeVizPlaylistAt(index: Int) {
        val s = host.vizState.value
        if (index in s.vizPlaylist.indices) {
            host.updateViz { it.copy(vizPlaylist = it.vizPlaylist.filterIndexed { i, _ -> i != index }) }
            persistAutoVisuals()
        }
    }

    fun setVizPlaylistEnabled(enabled: Boolean) {
        host.updateViz {
            it.copy(
                vizPlaylistEnabled = enabled,
                randomEnabled = if (enabled) false else it.randomEnabled,
            )
        }
        lastVizSwitchMs = android.os.SystemClock.elapsedRealtime()
        persistAutoVisuals()
    }

    fun setVizPlaylistIntelligent(enabled: Boolean) {
        host.updateViz { it.copy(vizPlaylistIntelligent = enabled) }
        persistAutoVisuals()
    }

    fun setVizPlaylistInterval(seconds: Int) {
        host.updateViz { it.copy(vizPlaylistIntervalSec = seconds.coerceIn(AutoVisualsPrefsStore.INTERVAL_SEC)) }
        persistAutoVisuals()
    }

    fun advanceVizPlaylist() {
        val s = host.vizState.value
        if (!s.vizPlaylistEnabled || s.vizPlaylist.size < 2 || !host.isPlaying) return
        val now = android.os.SystemClock.elapsedRealtime()
        val elapsed = now - lastVizSwitchMs
        val intervalMs = s.vizPlaylistIntervalSec * 1000L
        val due =
            if (s.vizPlaylistIntelligent) {
                val f = host.features()
                val minDwell = maxOf(8_000L, intervalMs / 2)
                (elapsed >= minDwell && LiveSignal.hit(f) >= STRONG_MOMENT_IMPULSE) || elapsed >= intervalMs * 2
            } else {
                elapsed >= intervalMs
            }
        if (!due) return
        lastVizSwitchMs = now
        vizPlaylistIndex = (vizPlaylistIndex + 1) % s.vizPlaylist.size
        applyVizEntry(s.vizPlaylist[vizPlaylistIndex])
    }

    fun setRandomEnabled(enabled: Boolean) {
        host.updateViz {
            it.copy(
                randomEnabled = enabled,
                vizPlaylistEnabled = if (enabled) false else it.vizPlaylistEnabled,
            )
        }
        lastRandomSwitchMs = android.os.SystemClock.elapsedRealtime()
        if (enabled && host.vizState.value.randomIncludeMilk) refreshMilkCache()
        if (enabled) randomStepNow()
        persistAutoVisuals()
    }

    fun setRandomInterval(seconds: Int) {
        host.updateViz { it.copy(randomIntervalSec = seconds.coerceIn(AutoVisualsPrefsStore.INTERVAL_SEC)) }
        persistAutoVisuals()
    }

    fun setRandomOnBeat(enabled: Boolean) {
        host.updateViz { it.copy(randomOnBeat = enabled) }
        persistAutoVisuals()
    }

    fun setRandomIncludeStyles(enabled: Boolean) {
        host.updateViz { it.copy(randomIncludeStyles = enabled) }
        persistAutoVisuals()
    }

    fun setRandomIncludePresets(enabled: Boolean) {
        host.updateViz { it.copy(randomIncludePresets = enabled) }
        persistAutoVisuals()
    }

    fun setRandomIncludeMilk(enabled: Boolean) {
        host.updateViz { it.copy(randomIncludeMilk = enabled) }
        if (enabled) refreshMilkCache()
        persistAutoVisuals()
    }

    fun setRandomizeColors(enabled: Boolean) {
        host.updateViz { it.copy(randomizeColors = enabled) }
        persistAutoVisuals()
    }

    private fun persistAutoVisuals() {
        prefsStore.save(host.vizState.value)
    }

    private fun refreshMilkCache() {
        host.milkFilesAsync { cachedMilkFiles = it }
    }

    private fun currentSectionIndex(): Int {
        val sections = host.vizState.value.sections
        if (sections.isEmpty()) return 0
        val pos = host.positionMs
        var idx = 0
        for (boundary in sections) {
            if (boundary <= pos) idx++ else break
        }
        return idx
    }

    private var lastStagedSection = -1

    fun onTrackChanged() {
        lastStagedSection = -1
    }

    fun advanceSectionStaging() {
        val s = host.vizState.value
        if (!s.sectionStaging || !host.isPlaying) return
        val index = currentSectionIndex()
        if (index == lastStagedSection) return
        lastStagedSection = index
        if (s.vizPlaylist.isNotEmpty()) {
            applyVizEntry(s.vizPlaylist[index % s.vizPlaylist.size])
            return
        }
        val pool = s.presets.filter { it.sceneId == s.sceneId }
        if (pool.isNotEmpty()) host.applyPreset(pool[index % pool.size])
    }

    fun setSectionStaging(enabled: Boolean) {
        host.updateViz { it.copy(sectionStaging = enabled) }
        lastStagedSection = -1
        if (enabled && host.vizState.value.sections.isEmpty()) host.analyzeCurrentTrack()
    }

    fun advanceRandomMode() {
        val s = host.vizState.value
        if (!s.randomEnabled || !host.isPlaying) return
        val now = android.os.SystemClock.elapsedRealtime()
        val elapsed = now - lastRandomSwitchMs
        val intervalMs = s.randomIntervalSec * 1000L
        val due =
            if (s.randomOnBeat) {
                val f = host.features()
                val minDwell = maxOf(6_000L, intervalMs / 2)
                (elapsed >= minDwell && LiveSignal.hit(f) >= STRONG_MOMENT_IMPULSE) || elapsed >= intervalMs * 2
            } else {
                elapsed >= intervalMs
            }
        if (!due) return
        randomStepNow()
    }

    fun randomStepNow() {
        if (host.presetLocked) return
        val s = host.vizState.value
        lastRandomSwitchMs = android.os.SystemClock.elapsedRealtime()
        val choices = mutableListOf<VizPlaylistEntry>()
        val sceneIds =
            dev.geode.render.scene.VisualStyleCatalog.silkIds +
                dev.geode.render.scene.VisualStyleCatalog.lifeIds +
                dev.geode.render.scene.VisualStyleCatalog.mycoIds +
                dev.geode.render.scene.VisualStyleCatalog.acidIds +
                dev.geode.render.scene.SceneCapabilities.SHADER_SCENES.keys
        if (s.randomIncludeStyles) sceneIds.forEach { choices += VizPlaylistEntry(sceneId = it, label = it) }
        if (s.randomIncludePresets) {
            s.presets.forEach { choices += VizPlaylistEntry(sceneId = it.sceneId, presetName = it.name, label = it.name) }
        }
        if (s.randomIncludeMilk && MilkdropEngine.available) {
            cachedMilkFiles.forEach {
                choices += VizPlaylistEntry(sceneId = SceneIds.MILKDROP, milkPath = it.path, label = it.name)
            }
        }
        if (choices.isEmpty()) return
        var pick = choices[randomRng.nextInt(choices.size)]
        if (choices.size > 1 && pick.sceneId == s.sceneId && pick.presetName == null && pick.milkPath == null) {
            pick = choices[randomRng.nextInt(choices.size)]
        }
        applyVizEntry(pick)
        if (s.randomizeColors) {
            val palette = randomRng.nextInt(SceneParams.PALETTES.size)
            val palette2 = randomRng.nextInt(SceneParams.PALETTES.size)
            val paletteMix = if (randomRng.nextBoolean()) randomRng.nextFloat() * 0.6f else 0f
            val colorShift = randomRng.nextFloat()
            host.updateViz { cur ->
                val rolled =
                    cur.params.copy(
                        palette = palette,
                        palette2 = palette2,
                        paletteMix = paletteMix,
                        colorShift = colorShift,
                    )
                cur.copy(params = PaletteStore.clear(PaletteStore.clear(rolled), second = true))
            }
        }
    }

    fun applyVizEntry(entry: VizPlaylistEntry) {
        host.selectScene(entry.sceneId)
        if (entry.presetName != null) {
            host.vizState.value.presets
                .firstOrNull { it.name == entry.presetName && it.sceneId == entry.sceneId }
                ?.let { host.applyPreset(it) }
        }
        if (entry.milkPath != null) {
            host.applyMilk(entry.milkPath, entry.sceneId)
        }
    }
}
