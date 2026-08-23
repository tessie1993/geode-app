package dev.geode.render

import android.content.Context
import android.opengl.GLES30
import dev.geode.engine.scenes.R
import dev.geode.render.fluid.CurlFlowScene
import dev.geode.render.fluid.FluidScene
import dev.geode.render.fluid.WaterScene
import dev.geode.render.scene.AcidScene
import dev.geode.render.scene.BeamScene
import dev.geode.render.scene.CymaticsScene
import dev.geode.render.scene.GlUtil
import dev.geode.render.scene.LifeScene
import dev.geode.render.scene.MilkdropEngine
import dev.geode.render.scene.MilkdropScene
import dev.geode.render.scene.MycoScene
import dev.geode.render.scene.Scene
import dev.geode.render.scene.SceneCapabilities
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import dev.geode.render.scene.ShaderScene
import dev.geode.render.scene.SilkScene
import dev.geode.render.scene.VisualStyleCatalog
import java.io.File
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.ConcurrentLinkedQueue

internal class SceneRegistry(
    private val context: Context,
    private val host: Host,
) {
    interface Host {
        fun onShaderError(message: String?)

        fun onMilkPresetLoaded(path: String)
    }

    private val scenes = LinkedHashMap<String, Scene>()
    private val activeCustomShaders = ConcurrentHashMap<String, String>()
    private val pendingCustomShaders = ConcurrentLinkedQueue<Pair<String, String>>()

    private var buildableIds: Set<String> = emptySet()

    private var paletteLutTex = 0

    /**
     * The renderer's one [TouchField], held here for the same reason [paletteLutTex] is: scenes
     * are built lazily on first use, so whatever a scene needs handed to it has to survive
     * between the handover and the build.
     */
    private var touchField: TouchField? = null

    @Volatile
    private var milkdropScene: MilkdropScene? = null

    @Volatile
    private var lastMilkPreset: String? = null

    @Volatile
    private var fluidForceSrc: String? = null

    @Volatile
    private var fluidDyeSrc: String? = null

    @Volatile
    private var fluidInjectionDirty = false

    var sceneParams: SceneParams = SceneParams.DEFAULT

    private var renderWidth = 1
    private var renderHeight = 1
    private var windowWidth = 1
    private var windowHeight = 1

    val milkdropAvailable: Boolean get() = MilkdropEngine.available

    fun availableSceneIds(): List<String> =
        buildList {
            addAll(VisualStyleCatalog.silkIds)
            addAll(VisualStyleCatalog.lifeIds)
            addAll(VisualStyleCatalog.mycoIds)
            addAll(VisualStyleCatalog.acidIds)
            addAll(SceneCapabilities.SHADER_SCENES.keys)
            if (MilkdropEngine.available) add(SceneIds.MILKDROP)
            add(SceneIds.FLUID)
            add(SceneIds.CURLFLOW)
            add(SceneIds.WATER)
            addAll(VisualStyleCatalog.cymaticsIds)
            add(SceneIds.BEAM)
        }

    fun submitShader(
        sceneId: String,
        fragmentSrc: String,
    ) {
        pendingCustomShaders.add(sceneId to fragmentSrc)
    }

    fun customShaderFor(sceneId: String): String? = activeCustomShaders[sceneId]

    fun drainPendingShaders() {
        while (true) {
            val (sceneId, src) = pendingCustomShaders.poll() ?: break
            (scenes[sceneId] as? ShaderScene)?.setFragmentSource(src)
        }
    }

    fun submitFluidInjectionShaders(
        force: String?,
        dye: String?,
    ) {
        fluidForceSrc = force
        fluidDyeSrc = dye
        fluidInjectionDirty = true
    }

    fun applyPendingFluidInjection() {
        if (!fluidInjectionDirty) return
        (scenes[SceneIds.FLUID] as? FluidScene)?.let { fluid ->
            fluidInjectionDirty = false
            fluid.setInjectionShaders(fluidForceSrc, fluidDyeSrc)
        }
    }

    fun loadMilkPreset(path: String) {
        lastMilkPreset = path
        milkdropScene?.queuePreset(path)
    }

    fun reloadCurrentMilkPreset() {
        milkdropScene?.reloadCurrent()
    }

    /**
     * Drops every built scene while the GL context is still current.
     *
     * [onSurfaceCreated] does this because a NEW context has arrived and the old objects are
     * stale. This is the other direction: a host that is going away for good. It matters because
     * MilkdropScene owns a projectM instance in the NATIVE heap, and losing the GL context
     * reclaims none of that - only MilkdropScene.release() calls nativeDestroy. Pure-GL scenes
     * would be reclaimed either way.
     *
     * Must be called on the GL thread.
     */
    fun releaseAll() {
        milkdropScene = null
        scenes.values.forEach { it.release() }
        scenes.clear()
    }

    fun onSurfaceCreated(
        width: Int,
        height: Int,
    ) {
        releaseAll()
        if (paletteLutTex != 0) {
            GLES30.glDeleteTextures(1, intArrayOf(paletteLutTex), 0)
            paletteLutTex = 0
        }
        buildableIds = availableSceneIds().toSet()
        if (fluidForceSrc != null || fluidDyeSrc != null) fluidInjectionDirty = true
        this.renderWidth = width
        this.renderHeight = height
    }

    fun createPaletteLut() {
        paletteLutTex = CyclicPalettes.createTexture(context)
        scenes.values.filterIsInstance<ShaderScene>().forEach { it.setPaletteLut(paletteLutTex) }
    }

    /** Hand the renderer's touch field to every shader scene, now and on every later build. */
    fun setTouchField(field: TouchField) {
        touchField = field
        scenes.values.filterIsInstance<ShaderScene>().forEach { it.setTouchField(field) }
    }

    fun resize(
        width: Int,
        height: Int,
        windowWidth: Int,
        windowHeight: Int,
    ) {
        renderWidth = width
        renderHeight = height
        this.windowWidth = windowWidth
        this.windowHeight = windowHeight
        scenes.values.forEach { it.resize(width, height) }
        milkdropScene?.setWindowSize(windowWidth, windowHeight)
    }

    @Suppress("ReturnCount")
    fun sceneFor(id: String): Scene? {
        scenes[id]?.let { return it }
        if (id !in buildableIds) return null
        return buildScene(id)
    }

    private fun buildScene(id: String): Scene {
        val scene = createScene(id, GlUtil.loadShader(context, R.raw.quad_vert))
        if (scene is ShaderScene) {
            if (paletteLutTex != 0) scene.setPaletteLut(paletteLutTex)
            touchField?.let { scene.setTouchField(it) }
        }
        scene.init()
        scene.setParams(sceneParams)
        scene.resize(renderWidth, renderHeight)
        activeCustomShaders[id]?.let { (scene as? ShaderScene)?.setFragmentSource(it) }
        if (scene is MilkdropScene) {
            milkdropScene = scene
            scene.setWindowSize(windowWidth, windowHeight)
            lastMilkPreset?.let { scene.queuePreset(it) }
        }
        if (scene is FluidScene && (fluidForceSrc != null || fluidDyeSrc != null)) {
            scene.setInjectionShaders(fluidForceSrc, fluidDyeSrc)
        }
        scenes[id] = scene
        return scene
    }

    fun setMilkdropWindowSize(
        width: Int,
        height: Int,
    ) {
        milkdropScene?.setWindowSize(width, height)
    }

    fun createScene(
        id: String,
        quadVert: String,
        export: Boolean = false,
    ): Scene =
        SceneCapabilities.SHADER_SCENES[id]?.let { res ->
            val frag = if (export) activeCustomShaders[id] ?: GlUtil.loadShader(context, res) else GlUtil.loadShader(context, res)
            ShaderScene(
                id,
                quadVert,
                frag,
                onError = { host.onShaderError(it) },
                onUserSourceCompiled = { compiled -> activeCustomShaders[id] = compiled },
            )
        }
            ?: VisualStyleCatalog.cymatics(id)?.let { style ->
                CymaticsScene(context, style).also { plate ->
                    plate.onShaderError = { host.onShaderError(it) }
                }
            }
            ?: VisualStyleCatalog.silk(id)?.let { style ->
                SilkScene(context, style).also { scene ->
                    scene.onShaderError = { host.onShaderError(it) }
                }
            }
            ?: VisualStyleCatalog.life(id)?.let { style ->
                LifeScene(context, style).also { scene ->
                    scene.onShaderError = { host.onShaderError(it) }
                }
            }
            ?: VisualStyleCatalog.acid(id)?.let { style ->
                AcidScene(context, style).also { scene ->
                    scene.onShaderError = { host.onShaderError(it) }
                }
            }
            ?: VisualStyleCatalog.myco(id)?.let { style ->
                MycoScene(context, style).also { scene ->
                    scene.onShaderError = { host.onShaderError(it) }
                }
            }
            ?: buildNamedScene(id)

    private fun buildNamedScene(id: String): Scene =
        when (id) {
            SceneIds.FLUID ->
                FluidScene(context).also { fluid ->
                    fluid.onShaderError = { host.onShaderError(it) }
                }
            SceneIds.CURLFLOW ->
                CurlFlowScene(context).also { curl ->
                    curl.onShaderError = { host.onShaderError(it) }
                }
            SceneIds.WATER ->
                WaterScene(context).also { water ->
                    water.onShaderError = { host.onShaderError(it) }
                }
            SceneIds.BEAM ->
                BeamScene(context).also { beam ->
                    beam.onShaderError = { host.onShaderError(it) }
                }
            SceneIds.MILKDROP ->
                MilkdropScene(
                    postVertexSrc = GlUtil.loadShader(context, R.raw.fade_vert),
                    postFragmentSrc = GlUtil.loadShader(context, R.raw.pm_post_frag),
                    sharedTextureDir = File(context.filesDir, "milk/textures").absolutePath,
                    onError = { host.onShaderError(it) },
                    onPresetLoaded = { host.onMilkPresetLoaded(it) },
                )
            else -> error("availableSceneIds offers \"$id\" but createScene cannot build it")
        }

    fun exportScene(
        sceneId: String,
        params: SceneParams,
    ): Scene {
        val quadVert = GlUtil.loadShader(context, R.raw.quad_vert)
        val scene = createScene(sceneId, quadVert, export = true)
        (scene as? FluidScene)?.setInjectionShaders(fluidForceSrc, fluidDyeSrc)
        (scene as? MilkdropScene)?.let { pm ->
            lastMilkPreset?.let { pm.queuePreset(it) }
        }
        scene.setParams(params)
        return scene
    }
}
