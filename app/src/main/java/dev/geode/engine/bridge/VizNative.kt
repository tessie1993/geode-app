package dev.geode.engine.bridge

// The JNI declarations of geode_viz_jni.cpp for the renderer, its scene and layers.
object VizNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /** [assets] is the app's `android.content.res.AssetManager`; typed as Any because this module has no Android SDK. */
    external fun vizCreate(
        assets: Any,
        cacheDir: String,
    ): Long

    external fun vizDestroy(handle: Long)

    external fun vizSetParams(
        handle: Long,
        values: FloatArray,
    )

    external fun vizSetReducedMotion(
        handle: Long,
        on: Boolean,
    )

    /** Customize › Layers, process-wide (core/viz/LayerState): native keeps the layer; [layer] is the line it last handed back. */
    external fun layerSet(layer: String)

    /** One field of native's layer changed (`geode_layer_edit`, [field] a `LAYER_*` up to [LAYER_STYLE]); the line to save, or "". */
    external fun layerEdit(
        field: Int,
        value: Float,
        styleId: String,
    ): String

    /** What Customize › Layers draws over [activeStyle], by `LAYER_*` (`geode_layer_values`); [family] -1 is the drawn style's. */
    external fun layerValues(
        activeStyle: String,
        family: Int,
    ): FloatArray

    /** One of the page's lists (`geode_layer_list`, [list] a `LAYER_LIST_*`); [family] as [layerValues] takes it. */
    external fun layerList(
        list: Int,
        activeStyle: String,
        family: Int,
    ): Array<String>

    /** The style the layer draws now over [activeStyle], "" for none. */
    external fun layerShown(activeStyle: String): String

    // Mirrors of GEODE_LAYER_*: a [layerValues] slot, and up to LAYER_STYLE what [layerEdit] changes.
    const val LAYER_ON: Int = 0
    const val LAYER_SOURCE: Int = 1
    const val LAYER_KNOB: Int = 2
    const val LAYER_BLEND: Int = 3
    const val LAYER_MIX: Int = 4
    const val LAYER_STYLE: Int = 5
    const val LAYER_FAMILY: Int = 6
    const val LAYER_PICKED: Int = 7

    // Mirrors of GEODE_LAYER_SOURCE_* and GEODE_LAYER_LIST_*.
    const val LAYER_SOURCE_USER: Int = 0
    const val LAYER_SOURCE_DIRECTOR: Int = 1
    const val LAYER_SOURCE_MACRO: Int = 2
    const val LAYER_LIST_SOURCES: Int = 0
    const val LAYER_LIST_BLENDS: Int = 1
    const val LAYER_LIST_FAMILIES: Int = 2
    const val LAYER_LIST_STYLES: Int = 3
    const val LAYER_LIST_KNOBS: Int = 4

    external fun vizSetTransition(
        handle: Long,
        id: String,
        durationMs: Long,
    )

    external fun vizTakeDirectorStyleMoments(handle: Long): Int

    external fun vizWriteAudio(
        handle: Long,
        interleaved: FloatArray,
        frames: Int,
        channels: Int,
        sampleRate: Int,
    )

    /** Settings › Audio's saved tuning (null: the engine's defaults), resolved natively as [SoundNative.soundTuningResolve]. */
    external fun vizSetSoundTuning(
        handle: Long,
        saved: FloatArray?,
    )

    /** A renderer's waves, as the saved text (geode_viz_set_lfos). */
    external fun vizSetLfos(
        handle: Long,
        lfos: String,
    )

    /** A renderer's envelopes, as the saved text (geode_viz_set_adsrs). */
    external fun vizSetAdsrs(
        handle: Long,
        adsrs: String,
    )

    /**
     * A renderer's reactions (geode_viz_set_reactions): the user's saved routes (null: Auto, the style's own
     * play). Native resolves what plays from these, the scene, `reactSeed` and the hand mask ([DirectorNative.handSet], or an
     * export's pin), which it reads itself. Any thread.
     */
    external fun vizSetReactions(
        handle: Long,
        reactions: String?,
    )

    /** The style a live surface draws, latched: each [vizFrame] asks for it until the renderer can draw it. Any thread. */
    external fun vizRequestScene(
        handle: Long,
        sceneId: String,
    )

    /**
     * The user's whole look (geode_viz_set_look): native passes on only the parts that differ from the look set last
     * on this handle, all of them on a new one. [reactions] null means the style's own routes. Any thread.
     */
    external fun vizSetLook(
        handle: Long,
        sceneId: String,
        params: FloatArray,
        lfos: String,
        adsrs: String,
        tuning: FloatArray?,
        reactions: String?,
        reducedMotion: Boolean,
        transitionId: String,
        transitionMs: Long,
    )

    /**
     * The look a renderer starts on before one is set, a line each: scene id, transition id and its ms, then the
     * shell's visual defaults: attack, decay, style timer s, playlist interval s and the preset morph's longest s.
     */
    external fun vizLookDefaults(): String

    /**
     * One whole live frame into framebuffer 0 (geode_viz_frame, `core/viz/frame/LiveFrame`), GL thread: the
     * renderer hears [ring] (0: nothing) and draws with what was pushed to it. [pacedFps] is the
     * rate the display last presented this surface at (0 not known) and [capFps] the surface's own cap (the same for a fixed
     * rate). Answers the `FRAME_*` events; act on a bit only when it is set.
     */
    external fun vizFrame(
        handle: Long,
        ring: Long,
        pacedFps: Float,
        capFps: Float,
    ): Int

    external fun vizSetOffscreen(
        handle: Long,
        on: Boolean,
    )

    external fun vizLastError(handle: Long): String

    external fun vizSurfaceCreated(handle: Long)

    external fun vizSurfaceChanged(
        handle: Long,
        width: Int,
        height: Int,
    )

    external fun vizSetScene(
        handle: Long,
        sceneId: String,
    ): Boolean

    external fun vizRender(
        handle: Long,
        timeSeconds: Double,
        targetFbo: Int,
    )

    /**
     * Full-frame RGBA8 underlay blended UNDER/INTO the scene: [blend] 0 = screen (replaces the scene
     * where it is black), 1 = multiply, 2 = add; [amount] 0..1. `pixels` is Android's `Bitmap.getPixels`
     * ARGB layout, width*height entries; null (or a non-positive size) clears it. Any thread; latched
     * for the next frame.
     */
    external fun vizSetUnderlay(
        handle: Long,
        pixels: IntArray?,
        width: Int,
        height: Int,
        blend: Int,
        amount: Float,
    )

    // Background image bake (core/viz/Underlay): pure, any thread, before vizSetUnderlay.

    /** The centred crop of a [srcW]x[srcH] picture to [targetW]:[targetH]'s aspect, as `[x, y, width, height]`. */
    external fun underlayCropRect(
        srcW: Int,
        srcH: Int,
        targetW: Int,
        targetH: Int,
    ): IntArray

    /**
     * BitmapFactory's power-of-two `inSampleSize` for a [srcW]x[srcH] picture: [fit] false, the largest leaving both
     * sides at least the target (the underlay); true, the smallest bringing both within it (the artwork).
     */
    external fun underlaySampleSize(
        srcW: Int,
        srcH: Int,
        targetW: Int,
        targetH: Int,
        fit: Boolean,
    ): Int

    /** Blurs, then dims, [pixels] (ARGB, [width]x[height]) in place; 0 skips either step. */
    external fun underlayFinish(
        pixels: IntArray,
        width: Int,
        height: Int,
        blurRadius: Int,
        dim: Float,
    )

    /** The background controls' ranges: amount and dim 0..1 (NaN = 0), blur 0..[underlayBlurMax]. */
    external fun underlayClampAmount(amount: Float): Float

    external fun underlayClampDim(dim: Float): Float

    external fun underlayClampBlur(radius: Int): Int

    external fun underlayBlurMax(): Int

    /** The background blends [vizSetUnderlay] takes, 0 until this count: 0 screen, 1 multiply, 2 add. */
    external fun underlayBlendCount(): Int

    /** The name a blend is saved under; [underlayBlendOf] reads it back (an unknown or null name is 0). */
    external fun underlayBlendName(blend: Int): String

    external fun underlayBlendOf(name: String?): Int
}
