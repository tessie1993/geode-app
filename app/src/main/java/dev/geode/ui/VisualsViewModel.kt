package dev.geode.ui

import android.net.Uri
import androidx.lifecycle.ViewModel
import dagger.hilt.android.lifecycle.HiltViewModel
import dev.geode.data.BackgroundPrefs
import dev.geode.data.MilkTexture
import dev.geode.data.Preset
import dev.geode.data.PresetFolders
import dev.geode.data.TemplateFormat
import dev.geode.data.TemplateId
import dev.geode.data.TemplateImport
import dev.geode.data.TemplateWrite
import dev.geode.data.VideoTemplate
import dev.geode.di.PlayerSessionProvider
import dev.geode.render.AdsrConfig
import dev.geode.render.LfoConfig
import dev.geode.render.UnderlayBlend
import dev.geode.render.scene.CustomizeTab
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.flow.StateFlow
import java.io.File
import javax.inject.Inject

sealed interface PresetLinkImport {
    data object NotALink : PresetLinkImport

    data class Imported(
        val name: String,
    ) : PresetLinkImport

    data object Unreadable : PresetLinkImport
}

/** Outcome of a `geode://template/...` link reaching the app through an intent. */
sealed interface TemplateLinkImport {
    data class Imported(
        val name: String,
    ) : TemplateLinkImport

    data class Replaced(
        val name: String,
    ) : TemplateLinkImport

    data class Unreadable(
        val why: String,
    ) : TemplateLinkImport
}

@HiltViewModel
class VisualsViewModel
    @Inject
    constructor(
        private val sessions: PlayerSessionProvider,
    ) : ViewModel() {
        private val session: PlayerSession = sessions.get()
        private val visualizer: VisualizerRepository = session.visualizerRepository

        val vizState: StateFlow<VizUiState> get() = visualizer.viz

        val activeMilkPath: StateFlow<String?> get() = visualizer.activeMilkPath

        val textures: StateFlow<List<MilkTexture>> get() = session.textures

        val lfos: StateFlow<List<LfoConfig>> get() = session.lfos

        val adsrs: StateFlow<List<AdsrConfig>> get() = session.adsrs

        val lockedParams: StateFlow<Set<String>> get() = session.lockedParams

        val presetLocked: StateFlow<Boolean> get() = session.presetLocked

        val presetFolders: StateFlow<PresetFolders> get() = session.presetFolders

        val backgroundPrefs: StateFlow<BackgroundPrefs> get() = session.backgroundPrefs

        val backgroundPush: StateFlow<BackgroundPushState> get() = session.backgroundPush

        fun pickBackgroundImage(uri: Uri) = session.pickBackgroundImage(uri)

        fun clearBackgroundImage() = session.clearBackgroundImage()

        fun setBackgroundBlend(blend: UnderlayBlend) = session.setBackgroundBlend(blend)

        fun setBackgroundAmount(amount: Float) = session.setBackgroundAmount(amount)

        fun setBackgroundBlurRadius(radius: Int) = session.setBackgroundBlurRadius(radius)

        fun setBackgroundDim(dim: Float) = session.setBackgroundDim(dim)

        fun setBackgroundRenderSize(
            width: Int,
            height: Int,
        ) = session.setBackgroundRenderSize(width, height)

        fun toggleParamLock(label: String) = session.toggleParamLock(label)

        fun randomizeParams(tab: CustomizeTab? = null) = session.randomizeParams(tab)

        val paramHistory: StateFlow<ParamHistoryState> get() = session.paramHistory

        val abSnapshots: StateFlow<AbSnapshotState> get() = session.abSnapshots

        /** The one write path for a Customize edit, so the panel's undo covers every control. */
        fun editSceneParams(params: SceneParams) = session.editSceneParams(params)

        fun undoParams() = session.undoParams()

        fun redoParams() = session.redoParams()

        fun resetCustomizeTab(tab: CustomizeTab) = session.resetCustomizeTab(tab)

        fun resetAllCustomize() = session.resetAllCustomize()

        fun captureSnapshotA() = session.captureSnapshotA()

        fun captureSnapshotB() = session.captureSnapshotB()

        fun recallSnapshotA() = session.recallSnapshotA()

        fun recallSnapshotB() = session.recallSnapshotB()

        fun blendSnapshots(t: Float) = session.blendSnapshots(t)

        fun setAdsr(
            index: Int,
            config: AdsrConfig,
        ) = session.setAdsr(index, config)

        fun setLfo(
            index: Int,
            config: LfoConfig,
        ) = session.setLfo(index, config)

        fun importTextures(
            uris: List<Uri>,
            onImported: () -> Unit,
        ) = session.importTextures(uris, onImported)

        fun removeTexture(name: String) = session.removeTexture(name)

        fun useTexture(
            name: String,
            onReady: (String) -> Unit,
        ) = session.useTexture(name, onReady)

        fun togglePresetLock() = session.togglePresetLock()

        fun applyPreset(preset: Preset) = session.applyPreset(preset)

        fun savePreset(
            name: String,
            customShader: String?,
            folder: String = "",
        ) = session.savePreset(name, customShader, folder)

        fun deletePreset(name: String) = session.deletePreset(name)

        fun presetFile(name: String): File? = session.presetFile(name)

        fun presetShareLink(name: String): String? = session.presetShareLink(name)

        fun importPresetLink(text: String): String? = session.importPresetLink(text)

        fun importSharedPreset(data: String): PresetLinkImport =
            if (!PresetLink.isPresetLink(data)) {
                PresetLinkImport.NotALink
            } else {
                session
                    .importPresetLink(data)
                    ?.let(PresetLinkImport::Imported)
                    ?: PresetLinkImport.Unreadable
            }

        fun importPresetFile(
            uri: Uri,
            onResult: (String?) -> Unit,
        ) = session.importPresetFile(uri, onResult)

        fun addPresetFolder(path: String) = session.addPresetFolder(path)

        fun renamePresetFolder(
            from: String,
            to: String,
        ) = session.renamePresetFolder(from, to)

        fun movePresetToFolder(
            name: String,
            folder: String,
        ) = session.movePresetToFolder(name, folder)

        fun userMilkPresets(): List<File> = session.userMilkPresets()

        fun noteMilkPreset(path: String) = session.noteMilkPreset(path)

        internal fun milkPresetPathFor(preset: Preset): String? = session.milkPresetPathFor(preset)

        val templates: StateFlow<List<VideoTemplate>> get() = session.templates

        val templateStarters: List<VideoTemplate> get() = session.templateStarters

        fun applyTemplate(template: VideoTemplate) = session.applyTemplate(template)

        fun saveCurrentAsTemplate(
            name: String,
            customShader: String?,
            onResult: (TemplateWrite) -> Unit,
        ) = session.saveCurrentAsTemplate(name, customShader, onResult)

        fun adoptTemplate(
            starter: VideoTemplate,
            onResult: (TemplateImport) -> Unit,
        ) = session.adoptTemplate(starter, onResult)

        fun deleteTemplate(id: TemplateId) = session.deleteTemplate(id)

        fun importTemplateText(
            text: String,
            onResult: (TemplateImport) -> Unit,
        ) = session.importTemplateText(text, onResult)

        fun importTemplateFile(
            uri: Uri,
            onResult: (TemplateImport) -> Unit,
        ) = session.importTemplateFile(uri, onResult)

        fun templateShareLink(template: VideoTemplate): String? = session.templateShareLink(template)

        /** Whether [data] carries a template link at all, before the async import runs. */
        fun isTemplateLink(data: String): Boolean = TemplateFormat.linkIn(data) != null

        fun importSharedTemplate(
            data: String,
            onResult: (TemplateLinkImport) -> Unit,
        ) {
            session.importTemplateText(data) { outcome ->
                onResult(
                    when (outcome) {
                        is TemplateImport.Added -> TemplateLinkImport.Imported(outcome.template.name)
                        is TemplateImport.Replaced -> TemplateLinkImport.Replaced(outcome.template.name)
                        is TemplateImport.Unreadable -> TemplateLinkImport.Unreadable(outcome.why)
                        is TemplateImport.WriteFailed -> TemplateLinkImport.Unreadable(outcome.why)
                    },
                )
            }
        }
    }
