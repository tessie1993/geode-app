package dev.geode.engine.bridge

// The JNI declarations of geode_milk_jni.cpp (CLAUDE.md).
object MilkNative {
    init {
        GeodeNative.ensureLoaded()
    }

    // The MilkDrop preset every surface restores (core/viz/MilkMemory), one per process. The shell seeds it
    // with the path it saved and saves again when [milkMemoryLoaded] or [milkMemoryForget] says so.

    /** The saved path, null for none; only a process's first seed counts. */
    external fun milkMemorySeed(saved: String?)

    /** The UI asked for [path]; any thread. */
    external fun milkMemoryRequest(path: String)

    /** A renderer reported [path] shown; true when it is new and must be saved. */
    external fun milkMemoryLoaded(path: String): Boolean

    /** [paths] were deleted; true when the saved path must be removed. */
    external fun milkMemoryForget(paths: Array<String>): Boolean

    /** The last preset shown, still on disk; null when none has been. */
    external fun milkMemoryLastShown(): String?

    /**
     * What a new renderer starts on; null only when not even a bundled preset is there. With nothing shown or
     * asked for, the starter pack goes into [milkDir] first. [assets] is the app's `AssetManager`, typed as Any like [VizNative.vizCreate]'s.
     */
    external fun milkMemoryRestorePath(
        assets: Any,
        milkDir: String,
    ): String?

    /** Copies the bundled presets into [milkDir] once per pack version, never over a file; how many it wrote. */
    external fun milkStarterInstall(
        assets: Any,
        milkDir: String,
    ): Int

    // MilkDrop textures (core/viz/MilkTextures): which picked files the store takes and their names, the preset
    // that shows one, and which stored texture answers each sampler a preset names. The shell reads, writes,
    // deletes and links the files.

    /** How many bytes of a picked texture are read at most. */
    external fun milkTextureMaxBytes(): Long

    /** Whether a pick named [name] that claims [declaredBytes] (0 when unknown) is worth reading. */
    external fun milkTextureAdmits(
        name: String,
        declaredBytes: Long,
    ): Boolean

    /** The name to store [bytes] under, null when refused; [width] and [height] are the bounds decode's (-1 when it failed). */
    external fun milkTextureStoredName(
        name: String,
        bytes: ByteArray,
        width: Int,
        height: Int,
    ): String?

    /** The stored textures in [textureDir], in the store's order. */
    external fun milkTextureList(textureDir: String): Array<String>

    /** The generated presets (file names) to delete now that [texture] is gone from [textureDir]. */
    external fun milkTextureRemovedPresets(
        texture: String,
        textureDir: String,
    ): Array<String>

    /** The file name of the generated preset that shows [texture]. */
    external fun milkTextureShowPresetName(texture: String): String

    /** That preset's source. */
    external fun milkTextureShowPresetText(texture: String): String

    /**
     * The preset at [presetPath] resolved against [textureDir] and the person's choices (lowercased expected
     * name to stored name): "1" when a stand-in or a missing texture needs the person, else "0", then per sampler
     * [MILK_LINK_FIELDS] entries: expected name, texture ("" when missing), kind (a MilkTextureLinkKind ordinal),
     * and the two link file names to make ("" for the second when there is one).
     */
    external fun milkTextureLinks(
        presetPath: String,
        textureDir: String,
        overrideKeys: Array<String>,
        overrideValues: Array<String>,
    ): Array<String>

    /** The presets in [milkDir] that keep a link folder. */
    external fun milkTextureLinkedPresets(milkDir: String): Array<String>

    /** The link folders under [milkDir] that no preset keeps. */
    external fun milkTextureStaleLinkDirs(milkDir: String): Array<String>

    /** A file from an imported folder: [MILK_PACK_PRESET], [MILK_PACK_TEXTURE] or 0 for neither. */
    external fun milkPackEntryKind(name: String): Int

    /** The file name an imported folder's file is written as. */
    external fun milkPackTargetName(name: String): String

    /** Whether a sampler of the preset at [presetPath] names nothing in [textureDir]. */
    external fun milkPresetMissesTexture(
        presetPath: String,
        textureDir: String,
    ): Boolean

    /** The file name a single imported preset is written as in [milkDir]; with [keepExisting] never one already there. */
    external fun milkImportFileName(
        milkDir: String,
        displayName: String,
        keepExisting: Boolean,
    ): String

    const val MILK_PACK_PRESET: Int = 1
    const val MILK_PACK_TEXTURE: Int = 2
    const val MILK_LINK_FIELDS: Int = 5
}
