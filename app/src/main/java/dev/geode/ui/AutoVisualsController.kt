package dev.geode.ui

import dev.geode.data.AutoVisualsPrefsStore
import dev.geode.data.Preset
import dev.geode.data.VizUiState
import dev.geode.engine.bridge.GeodeNative
import dev.geode.model.MilkFile
import dev.geode.model.VizPlaylistEntry
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

        fun selectScene(sceneId: String)

        /** Whether the entitlement lets the user keep [sceneId]; Auto only draws from those. */
        fun allowsStyle(sceneId: String): Boolean

        fun applyPreset(preset: Preset)

        fun applyMilk(path: String)

        fun milkFilesAsync(onDone: (List<MilkFile>) -> Unit)
    }

    private var vizPlaylistIndex = 0
    private val randomRng = kotlin.random.Random(android.os.SystemClock.elapsedRealtime())

    private var cachedMilkFiles: List<MilkFile> = emptyList()

    /** The style and params as Auto last left them; a difference at the next moment is the user's hand. */
    private var leftAs: Pair<String, SceneParams>? = null

    /**
     * Customize is open. Auto holds every change meanwhile (a moment, the timer, the playlist), as
     * MilkDrop locks the preset while a menu is up, and its timers start over when it closes.
     */
    var editing = false
        set(value) {
            if (field && !value) {
                val now = android.os.SystemClock.elapsedRealtime()
                GeodeNative.autoTimerRestart(GeodeNative.AUTO_TIMER_PLAYLIST, now)
                GeodeNative.autoTimerRestart(GeodeNative.AUTO_TIMER_RANDOM, now)
            }
            field = value
        }

    /** Auto changes nothing now: Customize is open or nothing plays. */
    private val holding get() = editing || !host.isPlaying

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
        GeodeNative.autoTimerRestart(GeodeNative.AUTO_TIMER_PLAYLIST, android.os.SystemClock.elapsedRealtime())
        persistAutoVisuals()
    }

    fun setVizPlaylistInterval(seconds: Int) {
        host.updateViz { it.copy(vizPlaylistIntervalSec = seconds.coerceIn(AutoVisualsPrefsStore.INTERVAL_SEC)) }
        persistAutoVisuals()
    }

    fun advanceVizPlaylist() {
        val s = host.vizState.value
        val running = s.vizPlaylistEnabled && s.vizPlaylist.size >= 2 && !holding
        val now = android.os.SystemClock.elapsedRealtime()
        if (!GeodeNative.autoTimerDue(GeodeNative.AUTO_TIMER_PLAYLIST, now, s.vizPlaylistIntervalSec, running)) return
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
        GeodeNative.autoTimerRestart(GeodeNative.AUTO_TIMER_PLAYLIST, android.os.SystemClock.elapsedRealtime())
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
        GeodeNative.autoTimerRestart(GeodeNative.AUTO_TIMER_RANDOM, android.os.SystemClock.elapsedRealtime())
        // The poll first, so the timer is running before the first look loads.
        advanceRandomMode()
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
        if (holding) return
        // A style picked or a control moved since Auto's last change: this moment is the user's
        // (MilkDrop restarts its timer on a manual load and holds while a menu is up). While they
        // keep editing every moment is skipped; Auto picks up again at the first one after they stop.
        val now = host.vizState.value.let { it.sceneId to it.params }
        val touched = leftAs != null && leftAs != now
        leftAs = now
        if (touched) return
        // A moment changes the style, never the look: a preset would replace every Customize value.
        if (host.vizState.value.vizPlaylistEnabled) stepVizPlaylist() else randomStepNow(keepLook = true)
    }

    fun advanceRandomMode() {
        val s = host.vizState.value
        val running = s.randomEnabled && !holding
        val now = android.os.SystemClock.elapsedRealtime()
        if (GeodeNative.autoTimerDue(GeodeNative.AUTO_TIMER_RANDOM, now, s.randomIntervalSec, running)) randomStepNow()
    }

    /**
     * A draw from Auto's sources, among the styles the user may keep. With [keepLook] (a director
     * moment) only the style changes: no preset, so Customize stays as set.
     */
    fun randomStepNow(keepLook: Boolean = false) {
        val s = host.vizState.value
        GeodeNative.autoTimerRestart(GeodeNative.AUTO_TIMER_RANDOM, android.os.SystemClock.elapsedRealtime())
        val choices = mutableListOf<VizPlaylistEntry>()
        val sceneIds =
            dev.geode.render.scene.VisualStyleCatalog.silkIds +
                dev.geode.render.scene.VisualStyleCatalog.lifeIds +
                dev.geode.render.scene.VisualStyleCatalog.mycoIds +
                dev.geode.render.scene.VisualStyleCatalog.acidIds +
                dev.geode.render.scene.SceneCapabilities.SHADER_SCENES.keys
        // Locked styles are left out of the draw: a pick the gate then refused was a moment with no change.
        if (s.randomIncludeStyles) {
            sceneIds.filter(host::allowsStyle).forEach { choices += VizPlaylistEntry(sceneId = it, label = it) }
        }
        if (s.randomIncludePresets && !keepLook) {
            s.presets.filter { host.allowsStyle(it.sceneId) }.forEach {
                choices += VizPlaylistEntry(sceneId = it.sceneId, presetName = it.name, label = it.name)
            }
        }
        if (s.randomIncludeMilk && MilkdropEngine.available && host.allowsStyle(SceneIds.MILKDROP)) {
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
    }

    fun applyVizEntry(entry: VizPlaylistEntry) {
        host.selectScene(entry.sceneId)
        if (entry.presetName != null) {
            host.vizState.value.presets
                .firstOrNull { it.name == entry.presetName && it.sceneId == entry.sceneId }
                ?.let { host.applyPreset(it) }
        }
        if (entry.milkPath != null) {
            host.applyMilk(entry.milkPath)
        }
        // Auto's own change (a moment, the timer, the playlist) is not the user's hand.
        leftAs = host.vizState.value.let { it.sceneId to it.params }
    }
}
