package dev.geode.ui

import dev.geode.data.GuiPrefs
import dev.geode.data.Preset
import dev.geode.data.VizStateStore
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.TransitionCatalog
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/** The style left on screen when the Styles grid closes over a preview of [shown]. */
internal fun sceneAfterPreview(
    shown: String,
    chosen: String,
    mayKeep: (String) -> Boolean,
): String = if (mayKeep(shown)) shown else chosen

internal class VisualSettingsController(
    private val vizStateStore: VizStateStore,
    private val scope: CoroutineScope,
    private val host: Host,
) {
    interface Host {
        val guiPrefs: GuiPrefs

        /** The preset's MilkDrop file, written if it is missing or stale; it does its disk work off the caller's thread. */
        suspend fun milkPathFor(preset: Preset): String?
    }

    /** The newest preset's MilkDrop file on its way from disk; a later apply or pick replaces it. */
    private var milkApply: Job? = null

    private val state get() = vizStateStore.state

    private val _vizApply = MutableSharedFlow<VizApply>(extraBufferCapacity = 8)
    val vizApply: SharedFlow<VizApply> = _vizApply

    private val _morphFade = MutableSharedFlow<Float>(extraBufferCapacity = 4)
    val morphFade: SharedFlow<Float> = _morphFade

    fun emitApply(apply: VizApply) {
        // A MilkDrop preset picked by hand is newer than one still being read for a preset apply.
        if (apply.milkPath != null) milkApply?.cancel()
        _vizApply.tryEmit(apply)
    }

    fun selectScene(sceneId: String) {
        vizStateStore.committedSceneId = null
        state.update { it.copy(sceneId = sceneId) }
        vizStateStore.persist()
    }

    /**
     * Shows [sceneId] on the stage without choosing it (the Styles grid's tap). The first preview
     * remembers the chosen style; later ones just swap what is shown.
     */
    fun previewScene(sceneId: String) {
        if (vizStateStore.committedSceneId == null) vizStateStore.committedSceneId = state.value.sceneId
        state.update { it.copy(sceneId = sceneId) }
    }

    /**
     * The style the user has chosen: the one on screen, or, while another is previewed, the one
     * the preview will fall back to. Entitlement checks read this, not the stage: a Pro style is
     * allowed on the stage as a preview, and must not be mistaken for a choice the user may not keep.
     */
    val chosenSceneId: String get() = vizStateStore.committedSceneId ?: state.value.sceneId

    /**
     * The grid was left (Customize, Presets, another tab) with a preview up. A tapped card is what
     * people mean as their pick, so the shown style becomes the chosen one if [mayKeep] allows it;
     * only a locked Pro preview falls back. Reverting here used to throw the pick away the moment
     * Customize opened, and the scene rebuild landed under the first macro turn, back on the old
     * (on a fresh install, the default) style.
     */
    fun leavePreview(mayKeep: (String) -> Boolean) {
        val chosen = vizStateStore.committedSceneId ?: return
        vizStateStore.committedSceneId = null
        val next = sceneAfterPreview(state.value.sceneId, chosen, mayKeep)
        state.update { it.copy(sceneId = next) }
        vizStateStore.persist()
    }

    fun setReactivity(
        attack: Float,
        decay: Float,
    ) {
        state.update { it.copy(attack = attack, decay = decay) }
        vizStateStore.persist()
    }

    fun setSceneParams(params: SceneParams) {
        state.update { it.copy(params = params) }
        vizStateStore.persist()
    }

    /**
     * A pinch and twist on the visual move the real Zoom and Rotation params, not a view-only
     * transform, so a gesture is kept in presets and takes. The maths and the clamp to each
     * slider's range are native (`core/viz/TouchTransform`).
     */
    fun nudgeTransform(
        zoomFactor: Float,
        degrees: Float,
    ) {
        val p = state.value.params
        val next =
            p.copy(
                zoom = GeodeNative.touchZoom(p.zoom, zoomFactor),
                rotation = GeodeNative.touchRotation(p.rotation, degrees),
            )
        if (next != p) setSceneParams(next)
    }

    fun reportShaderError(error: String?) {
        state.update { it.copy(shaderError = error) }
    }

    fun applyCustomShader(source: String) {
        val sceneId = state.value.sceneId
        state.update { it.copy(shaderError = null) }
        _vizApply.tryEmit(VizApply(customShader = source, sceneId = sceneId))
    }

    fun setTransitionId(id: String) {
        state.update {
            it.copy(
                transitionId = id,
                transitionStyle = TransitionCatalog.builtIn(id) ?: it.transitionStyle,
            )
        }
        vizStateStore.persist()
    }

    fun setTransitionDuration(seconds: Float) {
        state.update { it.copy(transitionDurationSec = seconds.coerceIn(0.3f, 5f)) }
        vizStateStore.persist()
    }

    fun applyPreset(preset: Preset) {
        // A preset is a choice: it ends any style preview rather than being saved under it.
        vizStateStore.committedSceneId = null
        state.update {
            it.copy(
                sceneId = preset.sceneId,
                params = preset.params,
                attack = preset.attack,
                decay = preset.decay,
            )
        }
        vizStateStore.persist()
        emitPresetMorph()
        preset.customShader?.let {
            _vizApply.tryEmit(VizApply(customShader = it, sceneId = preset.sceneId))
        }
        // The file is read, and written when stale, off the main thread; only the newest apply's
        // file may load, so two quick applies cannot land in the wrong order.
        milkApply?.cancel()
        milkApply =
            scope.launch {
                host.milkPathFor(preset)?.let {
                    _vizApply.tryEmit(VizApply(milkPath = it, sceneId = preset.sceneId))
                }
            }
    }

    /**
     * How long a preset load glides in for.
     *
     * It is a duration the user set, in seconds; nothing consults the analyser or assumes a tempo.
     */
    private fun emitPresetMorph() {
        val seconds = host.guiPrefs.presetMorphSeconds
        if (seconds <= 0f) return
        _morphFade.tryEmit(seconds)
    }
}
