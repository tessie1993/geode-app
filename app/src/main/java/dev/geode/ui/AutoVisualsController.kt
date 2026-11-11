package dev.geode.ui

import dev.geode.data.PaletteStore
import dev.geode.data.Preset
import dev.geode.model.VizPlaylistEntry
import dev.geode.model.VizUiState
import dev.geode.render.scene.MilkdropEngine
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.flow.StateFlow

internal class AutoVisualsController(
    private val prefsStore: AutoVisualsPrefsStore,
    private val host: Host,
) {
    interface Host {
        val vizState: StateFlow<VizUiState>

        fun updateViz(transform: (VizUiState) -> VizUiState)

        val isPlaying: Boolean

        val presetLocked: Boolean

        fun selectScene(sceneId: String)

        fun applyPreset(preset: Preset)

        fun applyMilk(
            path: String,
            sceneId: String,
        )

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

    /**
     * Points the playlist's entries for [from] at [to] after a preset rename. Entries find their
     * preset by name when they come up, so without this a renamed preset would drop out of the
     * playlist silently; its place in the order is kept.
     */
    fun renamePresetInPlaylist(
        from: String,
        to: String,
    ) {
        val playlist = host.vizState.value.vizPlaylist
        if (playlist.none { it.presetName == from }) return
        host.updateViz { s ->
            s.copy(
                vizPlaylist =
                    s.vizPlaylist.map { e ->
                        if (e.presetName != from) e else e.copy(presetName = to, label = if (e.label == from) to else e.label)
                    },
            )
        }
        persistAutoVisuals()
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

    fun setVizPlaylistInterval(seconds: Int) {
        host.updateViz { it.copy(vizPlaylistIntervalSec = seconds.coerceIn(AutoVisualsPrefsStore.INTERVAL_SEC)) }
        persistAutoVisuals()
    }

    fun advanceVizPlaylist() {
        val s = host.vizState.value
        if (!s.vizPlaylistEnabled || s.vizPlaylist.size < 2 || !host.isPlaying) return
        val now = android.os.SystemClock.elapsedRealtime()
        if (now - lastVizSwitchMs < s.vizPlaylistIntervalSec * 1000L) return
        lastVizSwitchMs = now
        vizPlaylistIndex = (vizPlaylistIndex + 1) % s.vizPlaylist.size
        applyVizEntry(s.vizPlaylist[vizPlaylistIndex])
    }

    /**
     * A step by hand (a gesture on the visual): the next entry now, or the previous one for a
     * negative [step], whether or not the playlist is cycling, and its clock restarts so the timer
     * does not jump again a moment later. False when the playlist has fewer than two looks, so the
     * caller steps the styles instead.
     */
    fun stepVizPlaylist(step: Int = 1): Boolean {
        val s = host.vizState.value
        if (s.vizPlaylist.size < 2) return false
        lastVizSwitchMs = android.os.SystemClock.elapsedRealtime()
        vizPlaylistIndex = Math.floorMod(vizPlaylistIndex + if (step < 0) -1 else 1, s.vizPlaylist.size)
        applyVizEntry(s.vizPlaylist[vizPlaylistIndex])
        return true
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

    /**
     * One Auto mode (owner): with the auto-director on, a new section or a drop also brings a new look:
     * the visual playlist's next entry when it is cycling, otherwise a draw from Auto's sources, as the
     * timer does. The director only decides while it drives (the whole song for Pro and trial, the first
     * minute for free), so the style changes follow the same rule.
     */
    fun onDirectorMoment() {
        if (!host.isPlaying) return
        if (host.vizState.value.vizPlaylistEnabled) stepVizPlaylist() else randomStepNow()
    }

    fun advanceRandomMode() {
        val s = host.vizState.value
        if (!s.randomEnabled || !host.isPlaying) return
        if (android.os.SystemClock.elapsedRealtime() - lastRandomSwitchMs < s.randomIntervalSec * 1000L) return
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
