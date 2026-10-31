package dev.geode.ui

import android.app.Application
import android.net.Uri
import dev.geode.data.TemplateId
import dev.geode.data.TemplateImport
import dev.geode.data.TemplateLook
import dev.geode.data.TemplateRepository
import dev.geode.data.TemplateWrite
import dev.geode.data.VideoTemplate
import dev.geode.render.scene.SceneIds
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Owns the video-template library the same way PresetLibraryController owns presets:
 * a thin layer over [TemplateRepository] that turns UI intents into store writes and
 * hands the current visual "look" over as a template, without a back-reference into
 * PlayerSession itself.
 */
internal class TemplateController(
    private val application: Application,
    private val templates: TemplateRepository,
    private val scope: CoroutineScope,
    private val storeScope: CoroutineScope,
    private val host: Host,
) {
    interface Host {
        val vizState: StateFlow<VizUiState>

        val activeMilkPath: String?

        /** Applies a template's look through the same path a preset apply uses. */
        fun applyLook(
            look: TemplateLook,
            name: String,
        )
    }

    /** Saved templates, straight from the repository — it already owns this state. */
    val library: StateFlow<List<VideoTemplate>> = templates.templates

    val starters: List<VideoTemplate> = templates.starters

    fun refreshInitial() {
        scope.launch { templates.refresh() }
    }

    fun applyTemplate(template: VideoTemplate) = host.applyLook(template.look, template.name)

    /**
     * Saves the current scene, reactivity and params as a new template. Layout, text and
     * export settings are left at their documented defaults — there is no "current layout"
     * to capture yet, only a current look.
     */
    fun saveCurrentAsTemplate(
        name: String,
        customShader: String?,
        onResult: (TemplateWrite) -> Unit,
    ) {
        val cleaned = name.replace(" · ", " - ").trim().ifEmpty { application.getString(dev.geode.R.string.template_untitled) }
        val s = host.vizState.value
        val milkPath = host.activeMilkPath
        storeScope.launch {
            val milkSource =
                if (s.sceneId == SceneIds.MILKDROP) {
                    milkPath?.let { src -> runCatching { File(src).readText() }.getOrNull() }
                } else {
                    null
                }
            val look =
                TemplateLook(
                    sceneId = s.sceneId,
                    attack = s.attack,
                    decay = s.decay,
                    params = s.params,
                    customShader = customShader,
                    milkPreset = milkSource,
                )
            val template =
                VideoTemplate(
                    id = TemplateId.random(),
                    name = cleaned,
                    look = look,
                    createdAtMs = System.currentTimeMillis(),
                )
            val result = templates.save(template)
            withContext(Dispatchers.Main.immediate) { onResult(result) }
        }
    }

    fun adopt(
        starter: VideoTemplate,
        onResult: (TemplateImport) -> Unit,
    ) {
        storeScope.launch {
            val result = templates.adopt(starter)
            withContext(Dispatchers.Main.immediate) { onResult(result) }
        }
    }

    fun deleteTemplate(id: TemplateId) {
        storeScope.launch { templates.delete(id) }
    }

    /** Imports a whole template file, or a geode://template link, pasted as text. */
    fun importTemplateText(
        text: String,
        onResult: (TemplateImport) -> Unit,
    ) {
        storeScope.launch {
            val result = templates.importText(text)
            withContext(Dispatchers.Main.immediate) { onResult(result) }
        }
    }

    fun importTemplateFile(
        uri: Uri,
        onResult: (TemplateImport) -> Unit,
    ) {
        storeScope.launch {
            val result = templates.importFrom { runCatching { application.contentResolver.openInputStream(uri) }.getOrNull() }
            withContext(Dispatchers.Main.immediate) { onResult(result) }
        }
    }

    fun shareLink(template: VideoTemplate): String? = templates.shareLink(template)
}
