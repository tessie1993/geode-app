package dev.geode.ui

import android.content.Context
import android.graphics.Bitmap
import android.net.Uri
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dagger.hilt.android.lifecycle.HiltViewModel
import dagger.hilt.android.qualifiers.ApplicationContext
import dev.geode.data.BackgroundPrefs
import dev.geode.data.MilkTexture
import dev.geode.data.Preset
import dev.geode.data.PresetFolders
import dev.geode.data.PresetStore
import dev.geode.data.StillFiles
import dev.geode.data.TemplateFormat
import dev.geode.data.TemplateId
import dev.geode.data.TemplateImport
import dev.geode.data.TemplateParse
import dev.geode.data.TemplateWrite
import dev.geode.data.VideoTemplate
import dev.geode.data.VizUiState
import dev.geode.di.PlayerSessionProvider
import dev.geode.model.PresetLink
import dev.geode.render.AdsrConfig
import dev.geode.render.LfoConfig
import dev.geode.render.UnderlayBlend
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
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

/**
 * A shared link that reached the app and is waiting for the user's yes. The links are
 * BROWSABLE, so any web page or app can fire one; nothing is written until the user
 * confirms. [name] is blank when the link names nothing; [replaces] is the name of the
 * user's template a template link would overwrite (templates match on id).
 */
sealed interface SharedLinkOffer {
    val link: String
    val name: String

    data class Preset(
        override val link: String,
        override val name: String,
    ) : SharedLinkOffer

    data class Template(
        override val link: String,
        override val name: String,
        val replaces: String?,
    ) : SharedLinkOffer

    /** A .milk another app opened in this one; [link] is its uri. */
    data class MilkFile(
        override val link: String,
        override val name: String,
    ) : SharedLinkOffer
}

@HiltViewModel
class VisualsViewModel
    @Inject
    constructor(
        private val sessions: PlayerSessionProvider,
        @ApplicationContext context: Context,
    ) : ViewModel() {
        private val session: PlayerSession = sessions.get()
        private val visualizer: VisualizerRepository = session.visualizerRepository
        private val stills = StillFiles(context)

        val vizState: StateFlow<VizUiState> get() = visualizer.viz

        val activeMilkPath: StateFlow<String?> get() = visualizer.activeMilkPath

        /** What Liquid mode is doing now (0 off, 1 full, 2 lite, 3 warp, 4 paused); see [VisualizerRepository.liquidMode]. */
        val liquidMode: StateFlow<Int> get() = visualizer.liquidMode

        val textures: StateFlow<List<MilkTexture>> get() = session.textureController.textures

        val lfos: StateFlow<List<LfoConfig>> get() = session.modulation.lfos

        val adsrs: StateFlow<List<AdsrConfig>> get() = session.adsrs

        val reactions: StateFlow<List<dev.geode.render.ReactionRoute>?> get() = session.modulation.reactions

        val handMask: StateFlow<Int> get() = session.handMask

        val lockedParams: StateFlow<Set<String>> get() = session.modulation.lockedParams

        val presetFolders: StateFlow<PresetFolders> get() = session.presetLibrary.folders

        val backgroundPrefs: StateFlow<BackgroundPrefs> get() = session.backgroundController.prefs

        val backgroundPush: StateFlow<BackgroundPushState> get() = session.backgroundPush

        fun pickBackgroundImage(uri: Uri) = session.backgroundController.pick(uri)

        fun clearBackgroundImage() = session.backgroundController.clear()

        fun setBackgroundBlend(blend: UnderlayBlend) = session.backgroundController.setBlend(blend)

        fun setBackgroundAmount(amount: Float) = session.backgroundController.setAmount(amount)

        fun setBackgroundBlurRadius(radius: Int) = session.backgroundController.setBlurRadius(radius)

        fun setBackgroundDim(dim: Float) = session.backgroundController.setDim(dim)

        fun setBackgroundRenderSize(
            width: Int,
            height: Int,
        ) = session.backgroundController.setRenderSize(width, height)

        fun toggleParamLock(label: String) = session.modulation.toggleParamLock(label)

        fun borrowCustomizeCategory(
            category: CustomizeCategory,
            from: SceneParams,
        ) = session.modulation.borrowCategory(category, from)

        fun randomizeParams(
            category: CustomizeCategory? = null,
            basicsOnly: Boolean = false,
        ) = session.modulation.randomizeParams(category, basicsOnly)

        fun randomizeWaves() = session.modulation.randomizeWaves(all = true)

        /** The two-circle gesture's Randomize everything: style, preset, every category; [basicsOnly] without Pro. */
        fun randomizeEverything(basicsOnly: Boolean) = session.randomizeEverything(basicsOnly)

        /** Tap along's roll, free: Customize › Music's randomize (new moments and reactions), Keep respected. */
        fun tapAlongRoll() = session.modulation.randomizeParams(CustomizeCategory.MUSIC)

        /** Shake to screenshot: saves [still] to Pictures off the main thread; [onSaved] hears whether it landed. */
        fun saveStill(
            still: Bitmap,
            onSaved: (Boolean) -> Unit,
        ) {
            viewModelScope.launch {
                val saved = withContext(Dispatchers.IO) { stills.save(still, System.currentTimeMillis()) } != null
                onSaved(saved)
            }
        }

        val paramHistory: StateFlow<ParamHistoryState> get() = session.paramHistory

        /** The one write path for a Customize edit, so the panel's undo covers every control. */
        fun editSceneParams(params: SceneParams) = session.modulation.editSceneParams(params)

        fun undoParams() = session.modulation.undo()

        fun redoParams() = session.modulation.redo()

        fun resetCustomizeCategory(category: CustomizeCategory) = session.modulation.resetCategory(category)

        fun resetAllCustomize() = session.modulation.resetAll()

        fun setAdsr(
            index: Int,
            config: AdsrConfig,
        ) = session.modulation.setAdsr(index, config)

        fun setLfo(
            index: Int,
            config: LfoConfig,
        ) = session.modulation.setLfo(index, config)

        fun setReactions(routes: List<dev.geode.render.ReactionRoute>?) = session.modulation.setReactions(routes)

        fun setHandReactions(edit: dev.geode.render.HandReactions) = session.modulation.setHandReactions(edit)

        fun rerollReactions() = session.modulation.rerollReactions()

        fun importTextures(
            uris: List<Uri>,
            onImported: () -> Unit,
        ) = session.importTextures(uris, onImported)

        fun removeTexture(name: String) = session.textureController.removeTexture(name)

        fun useTexture(
            name: String,
            onReady: (String) -> Unit,
        ) = session.useTexture(name, onReady)

        fun applyPreset(preset: Preset) = session.applyPreset(preset)

        fun savePreset(
            name: String,
            customShader: String?,
            folder: String = "",
        ) = session.presetLibrary.savePreset(name, customShader, folder)

        fun deletePreset(name: String) = session.presetLibrary.deletePreset(name)

        fun renamePreset(
            from: String,
            to: String,
        ): String? = session.renamePreset(from, to)

        fun duplicatePreset(name: String): String? = session.presetLibrary.duplicatePreset(name)

        /** A .milk another app opened here, named for the confirm dialog; null when it is no .milk we read. */
        fun offerMilkFile(
            uri: Uri,
            onDone: (SharedLinkOffer?) -> Unit,
        ) = session.milkImport.offerOpenedMilkAsync(uri) { name ->
            onDone(name?.let { SharedLinkOffer.MilkFile(uri.toString(), it.removeSuffix(".milk")) })
        }

        /**
         * A .milk the user agreed to open: imported as the MilkDrop page's picker imports one (under a
         * new name, never over a preset already there), then chosen like a tap on it (MilkDrop through
         * the style gate, which opens the paywall if locked).
         */
        fun openMilkFile(
            uri: Uri,
            onDone: (Boolean) -> Unit,
        ) = session.milkImport.importOpenedMilkAsync(uri) { path ->
            if (path != null) {
                session.selectScene(SceneIds.MILKDROP)
                if (session.allowsStyle(SceneIds.MILKDROP)) {
                    session.visual.emitApply(VizApply(milkPath = path, sceneId = SceneIds.MILKDROP))
                }
            }
            onDone(path != null)
        }

        fun presetFile(name: String): File? = session.presetLibrary.presetFile(name)

        fun presetShareLink(name: String): String? = session.presetLibrary.presetShareLink(name)

        fun importPresetLink(text: String): String? = session.presetLibrary.importPresetLink(text)

        fun importSharedPreset(data: String): PresetLinkImport =
            if (!PresetLink.isPresetLink(data)) {
                PresetLinkImport.NotALink
            } else {
                session.presetLibrary
                    .importPresetLink(data)
                    ?.let(PresetLinkImport::Imported)
                    ?: PresetLinkImport.Unreadable
            }

        fun importPresetFile(
            uri: Uri,
            onResult: (String?) -> Unit,
        ) = session.importPresetFile(uri, onResult)

        fun addPresetFolder(path: String) = session.presetLibrary.addPresetFolder(path)

        fun renamePresetFolder(
            from: String,
            to: String,
        ) = session.presetLibrary.renamePresetFolder(from, to)

        fun movePresetToFolder(
            name: String,
            folder: String,
        ) = session.presetLibrary.movePresetToFolder(name, folder)

        fun userMilkPresets(): List<File> = session.presetLibrary.userMilkPresets()

        fun noteMilkPreset(path: String) = session.vizStateStore.noteMilkPreset(path)

        val templates: StateFlow<List<VideoTemplate>> get() = session.templateLibrary.library

        val templateStarters: List<VideoTemplate> get() = session.templateLibrary.starters

        fun applyTemplate(template: VideoTemplate) = session.templateLibrary.applyTemplate(template)

        fun saveCurrentAsTemplate(
            name: String,
            customShader: String?,
            onResult: (TemplateWrite) -> Unit,
        ) = session.saveCurrentAsTemplate(name, customShader, onResult)

        fun adoptTemplate(
            starter: VideoTemplate,
            onResult: (TemplateImport) -> Unit,
        ) = session.adoptTemplate(starter, onResult)

        fun deleteTemplate(id: TemplateId) = session.templateLibrary.deleteTemplate(id)

        fun importTemplateText(
            text: String,
            onResult: (TemplateImport) -> Unit,
        ) = session.importTemplateText(text, onResult)

        fun importTemplateFile(
            uri: Uri,
            onResult: (TemplateImport) -> Unit,
        ) = session.importTemplateFile(uri, onResult)

        fun templateShareLink(template: VideoTemplate): String? = session.templateLibrary.shareLink(template)

        /**
         * Reads a shared link without importing it, for the confirm dialog. Null when [data]
         * is no link of ours or cannot be read; the import paths then report it as before.
         */
        fun offerFor(data: String): SharedLinkOffer? {
            if (PresetLink.isPresetLink(data)) {
                val json = PresetLink.decode(data) ?: return null
                val preset = runCatching { PresetStore.fromJson(json) }.getOrNull() ?: return null
                return SharedLinkOffer.Preset(data, preset.name.trim())
            }
            val link = TemplateFormat.linkIn(data) ?: return null
            val template = (TemplateFormat.decode(link) as? TemplateParse.Parsed)?.template ?: return null
            val replaces =
                session.templateLibrary.library.value
                    .firstOrNull { it.id == template.id }
                    ?.name
            return SharedLinkOffer.Template(data, template.name.trim(), replaces)
        }

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
