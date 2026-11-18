package dev.geode.ui

import dev.geode.data.Preset
import dev.geode.data.VizStateStore
import dev.geode.engine.bridge.TouchNative
import dev.geode.model.SceneParams
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.update

/** The style left on screen when the Styles grid closes over a preview of [shown]. */
internal fun sceneAfterPreview(
    shown: String,
    chosen: String,
    mayKeep: (String) -> Boolean,
): String = if (mayKeep(shown)) shown else chosen

internal class VisualSettingsController(
    private val vizStateStore: VizStateStore,
    private val host: Host,
) {
    interface Host {
        /** Settings › Visuals' blend between looks, in seconds. */
        val presetMorphSeconds: Float
    }

    private val state get() = vizStateStore.state

    private val _vizApply = MutableSharedFlow<VizApply>(extraBufferCapacity = 8)
    val vizApply: SharedFlow<VizApply> = _vizApply

    private val _morphFade = MutableSharedFlow<Float>(extraBufferCapacity = 4)
    val morphFade: SharedFlow<Float> = _morphFade

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
     * transform, so a gesture is kept in presets. The maths and the clamp to each
     * slider's range are native (`core/viz/TouchTransform`).
     */
    fun nudgeTransform(
        zoomFactor: Float,
        degrees: Float,
    ) {
        val p = state.value.params
        val next =
            p
                .with("zoom", TouchNative.touchZoom(p["zoom"], zoomFactor))
                .with("rotation", TouchNative.touchRotation(p["rotation"], degrees))
        if (next != p) setSceneParams(next)
    }

    fun reportShaderError(error: String?) {
        state.update { it.copy(shaderError = error) }
    }

    fun setTransitionId(id: String) {
        state.update { it.copy(transitionId = id) }
        vizStateStore.persist()
    }

    fun setTransitionDuration(seconds: Float) {
        state.update { it.copy(transitionDurationSec = seconds) }
        vizStateStore.persist()
    }

    fun applyPreset(preset: Preset) {
        // A preset is a choice: it ends any style preview rather than being saved under it.
        vizStateStore.committedSceneId = null
        state.update {
            it.copy(
                sceneId = preset.sceneId,
                params = preset.params.keepingInputsOf(it.params),
                attack = preset.attack,
                decay = preset.decay,
            )
        }
        vizStateStore.persist()
        emitPresetMorph()
        preset.customShader?.let {
            _vizApply.tryEmit(VizApply(customShader = it, sceneId = preset.sceneId))
        }
    }

    /**
     * How long a preset load glides in for.
     *
     * It is a duration the user set, in seconds; nothing consults the analyser or assumes a tempo.
     */
    private fun emitPresetMorph() {
        val seconds = host.presetMorphSeconds
        if (seconds <= 0f) return
        _morphFade.tryEmit(seconds)
    }
}
