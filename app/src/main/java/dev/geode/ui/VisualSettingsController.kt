package dev.geode.ui

import dev.geode.analysis.AnalysisEngine
import dev.geode.analysis.LiveInputProfile
import dev.geode.data.Preset
import dev.geode.render.TransitionCatalog
import dev.geode.render.TransitionStyle
import dev.geode.render.scene.SceneParams
import dev.geode.render.scene.TouchTransform
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.update

internal class VisualSettingsController(
    private val engine: AnalysisEngine,
    private val vizStateStore: VizStateStore,
    private val host: Host,
) {
    interface Host {
        val guiPrefs: GuiPrefs

        fun setGuiPrefs(prefs: GuiPrefs)

        fun milkPathFor(preset: Preset): String?
    }

    private val state get() = vizStateStore.state

    private val _vizApply = MutableSharedFlow<VizApply>(extraBufferCapacity = 8)
    val vizApply: SharedFlow<VizApply> = _vizApply

    private val _morphFade = MutableSharedFlow<Float>(extraBufferCapacity = 4)
    val morphFade: SharedFlow<Float> = _morphFade

    fun emitApply(apply: VizApply) {
        _vizApply.tryEmit(apply)
    }

    fun selectScene(sceneId: String) {
        state.update { it.copy(sceneId = sceneId) }
        vizStateStore.persist()
    }

    fun setReactivity(
        attack: Float,
        decay: Float,
    ) {
        engine.attack = attack
        engine.decay = decay
        state.update { it.copy(attack = attack, decay = decay) }
        vizStateStore.persist()
    }

    fun setSceneParams(params: SceneParams) {
        state.update { it.copy(params = params) }
        vizStateStore.persist()
    }

    fun resetSceneParams() = setSceneParams(SceneParams.DEFAULT)

    fun nudgeTransform(
        zoomFactor: Float,
        rotationDegrees: Float,
    ) {
        val p = state.value.params
        val next =
            p.copy(
                zoom = TouchTransform.zoom(p.zoom, zoomFactor),
                rotation = TouchTransform.rotation(p.rotation, rotationDegrees),
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

    fun setTransitionStyle(style: TransitionStyle) {
        state.update { it.copy(transitionStyle = style, transitionId = style.name.lowercase()) }
    }

    fun setTransitionId(id: String) {
        state.update {
            it.copy(
                transitionId = id,
                transitionStyle = TransitionCatalog.builtIn(id) ?: it.transitionStyle,
            )
        }
    }

    fun setTransitionDuration(seconds: Float) {
        state.update { it.copy(transitionDurationSec = seconds.coerceIn(0.3f, 5f)) }
    }

    fun applyPreset(preset: Preset) {
        engine.attack = preset.attack
        engine.decay = preset.decay
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
        host.milkPathFor(preset)?.let {
            _vizApply.tryEmit(VizApply(milkPath = it, sceneId = preset.sceneId))
        }
    }

    fun applyLiveInputProfile(profile: LiveInputProfile) {
        host.setGuiPrefs(
            host.guiPrefs.copy(
                beatSensitivity = profile.beatSigma,
                beatMinIntervalMs = profile.beatIntervalMs,
            ),
        )
        setReactivity(profile.attack, profile.decay)
        setSceneParams(profile.apply(state.value.params))
    }

    /**
     * How long a preset load glides in for.
     *
     * The length used to be a count of beats divided by the tracked tempo, which made the glide
     * as wrong as the estimate and silently used 120 BPM on live input, where there is no
     * estimate. It is a duration the user set, in seconds, and nothing consults the analyser.
     */
    private fun emitPresetMorph() {
        val seconds = host.guiPrefs.presetMorphSeconds
        if (seconds <= 0f) return
        _morphFade.tryEmit(seconds)
    }
}
