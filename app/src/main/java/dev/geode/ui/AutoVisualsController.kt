package dev.geode.ui

import android.os.SystemClock
import dev.geode.data.AutoVisualsPrefsStore
import dev.geode.data.Preset
import dev.geode.data.VizUiState
import dev.geode.engine.bridge.AutoNative
import dev.geode.engine.bridge.SoundNative
import dev.geode.model.StyleCatalogue
import dev.geode.model.VizPlaylistEntry
import kotlinx.coroutines.flow.StateFlow

/**
 * Auto visuals' shell: the settings, the playlist as the user edits it, and the change native names. When the
 * playlist steps and to which entry, when the timed draw is due, what a director moment brings and what a draw
 * picks are native (core/viz/AutoPlan); this lists the sources, polls and puts the pick up.
 */
internal class AutoVisualsController(
    private val prefsStore: AutoVisualsPrefsStore,
    private val host: Host,
) {
    interface Host {
        val vizState: StateFlow<VizUiState>

        fun updateViz(transform: (VizUiState) -> VizUiState)

        /** Sound drives the visual: the player, the mic or a captured app. Auto holds while none does. */
        val soundPlaying: Boolean

        fun selectScene(sceneId: String)

        /** Whether the entitlement lets the user keep [sceneId]; Auto only draws from those. */
        fun allowsStyle(sceneId: String): Boolean

        fun applyPreset(preset: Preset)
    }

    /** Customize is open: native holds every change meanwhile and starts its waits over when it closes. */
    var editing = false
        set(value) {
            field = value
            AutoNative.autoEditingSet(value, SystemClock.elapsedRealtime())
        }

    /** The user picked a style or a preset by hand: native starts Auto's waits over. */
    fun noteHandPick() = AutoNative.autoHandPick(SystemClock.elapsedRealtime())

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
        SoundNative.autoTimerRestart(SoundNative.AUTO_TIMER_PLAYLIST, SystemClock.elapsedRealtime())
        persistAutoVisuals()
    }

    fun setVizPlaylistInterval(seconds: Int) {
        host.updateViz { it.copy(vizPlaylistIntervalSec = AutoNative.autoIntervalSeconds(seconds)) }
        persistAutoVisuals()
    }

    fun advanceVizPlaylist() {
        val s = host.vizState.value
        val now = SystemClock.elapsedRealtime()
        val index = AutoNative.autoPlaylistTick(s.vizPlaylist.size, s.vizPlaylistEnabled, host.soundPlaying, s.vizPlaylistIntervalSec, now)
        if (index >= 0) applyVizEntry(s.vizPlaylist[index], automatic = true)
    }

    /**
     * A step by hand (a gesture on the visual): the next entry now, or the previous one for a
     * negative [step], whether or not the playlist is cycling, and its clock restarts so the timer
     * does not jump again a moment later. False when the playlist has fewer than two looks, so the
     * caller steps the styles instead. [automatic]: a director moment's step, not the user's.
     */
    fun stepVizPlaylist(
        step: Int = 1,
        automatic: Boolean = false,
    ): Boolean {
        val s = host.vizState.value
        val index = AutoNative.autoPlaylistStep(s.vizPlaylist.size, step, SystemClock.elapsedRealtime())
        if (index < 0) return false
        applyVizEntry(s.vizPlaylist[index], automatic)
        return true
    }

    fun setRandomEnabled(enabled: Boolean) {
        host.updateViz {
            it.copy(
                randomEnabled = enabled,
                vizPlaylistEnabled = if (enabled) false else it.vizPlaylistEnabled,
            )
        }
        SoundNative.autoTimerRestart(SoundNative.AUTO_TIMER_RANDOM, SystemClock.elapsedRealtime())
        // The poll first, so the timer is running before the first look loads.
        advanceRandomMode()
        if (enabled) randomStepNow()
        persistAutoVisuals()
    }

    fun setRandomInterval(seconds: Int) {
        host.updateViz { it.copy(randomIntervalSec = AutoNative.autoIntervalSeconds(seconds)) }
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

    private fun persistAutoVisuals() {
        prefsStore.save(host.vizState.value)
    }

    /**
     * One Auto mode (owner): with the auto-director on, a new section or a drop also brings a new look:
     * the visual playlist's next entry when it is cycling, otherwise a draw from Auto's sources, as the
     * timer does. The director only decides while it drives (the whole song for Pro and trial, the first
     * minute for free), so the style changes follow the same rule.
     */
    fun onDirectorMoment() {
        val s = host.vizState.value
        when (AutoNative.autoMoment(s.sceneId, s.params.frame(), s.params.customPaletteId, host.soundPlaying, s.vizPlaylistEnabled)) {
            AutoNative.AUTO_MOMENT_PLAYLIST -> stepVizPlaylist(automatic = true)
            AutoNative.AUTO_MOMENT_RANDOM -> randomStepNow(keepLook = true)
        }
    }

    fun advanceRandomMode() {
        val s = host.vizState.value
        val now = SystemClock.elapsedRealtime()
        if (AutoNative.autoRandomTick(s.randomEnabled, host.soundPlaying, s.randomIntervalSec, now)) randomStepNow()
    }

    /**
     * A draw from Auto's sources, among the styles the user may keep. With [keepLook] (a director
     * moment) only the style changes: no preset, so Customize stays as set. Native draws.
     */
    fun randomStepNow(keepLook: Boolean = false) {
        val s = host.vizState.value
        var sources = 0
        if (s.randomIncludeStyles) sources = sources or AutoNative.AUTO_DRAW_STYLES
        if (s.randomIncludePresets) sources = sources or AutoNative.AUTO_DRAW_PRESETS
        if (keepLook) sources = sources or AutoNative.AUTO_DRAW_KEEP_LOOK
        val styles = StyleCatalogue.ids + s.presets.map { it.sceneId }
        val out = IntArray(2)
        val sceneId =
            AutoNative.autoDraw(
                styles.distinct().filterNot(host::allowsStyle).joinToString("\n"),
                s.sceneId,
                s.presets.joinToString("\n") { it.sceneId },
                s.presets.indexOfFirst { it.sceneId == s.sceneId && it.params == s.params },
                sources,
                SystemClock.elapsedRealtime(),
                out,
            ) ?: return
        val i = out[1]
        val entry =
            when (out[0]) {
                AutoNative.AUTO_KIND_PRESET -> VizPlaylistEntry(sceneId, presetName = s.presets[i].name, label = s.presets[i].name)
                else -> VizPlaylistEntry(sceneId = sceneId, label = sceneId)
            }
        applyVizEntry(entry, automatic = true)
    }

    /** [automatic]: Auto's own change, which native fades for longer than one by hand. */
    fun applyVizEntry(
        entry: VizPlaylistEntry,
        automatic: Boolean = false,
    ) {
        if (automatic) {
            SoundNative.autoMarkChange(entry.sceneId)
        }
        host.selectScene(entry.sceneId)
        if (entry.presetName != null) {
            host.vizState.value.presets
                .firstOrNull { it.name == entry.presetName && it.sceneId == entry.sceneId }
                ?.let { host.applyPreset(it) }
        }
        // Auto's own change (a moment, the timer, the playlist) is not the user's hand.
        host.vizState.value.let { AutoNative.autoLeftAs(it.sceneId, it.params.frame(), it.params.customPaletteId) }
    }
}
