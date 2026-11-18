package dev.geode.engine.bridge

// The JNI declarations of geode_looks_jni.cpp (CLAUDE.md).
object LooksNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /** A name as a file stem (core/viz/LookFiles, `geode_file_safe_name`). Pure. */
    external fun fileSafeName(name: String): String

    /** [fileSafeName] without the hash: a preset folder's name (`geode_file_safe_chars`). Pure. */
    external fun fileSafeChars(name: String): String

    /** The indices of [names] in list order, [caseless] or by code point, ties kept (`geode_looks_order`). Pure. */
    external fun looksOrder(
        names: Array<String>,
        caseless: Boolean,
    ): IntArray

    /** A saved palette file's text as this build writes it, or null when it is no palette (`geode_saved_palette_read`). */
    external fun savedPaletteRead(text: String): String?

    /** [savedPaletteRead] with the id, name, hues and colours made safe (`geode_saved_palette_sanitize`). */
    external fun savedPaletteSanitize(text: String): String?

    /** A new saved palette's text: [stops] when not empty, else the hue gradient (`geode_saved_palette_create`). */
    external fun savedPaletteCreate(
        name: String,
        base: Float,
        span: Float,
        stops: IntArray,
    ): String

    /** The text a palette file should hold under its current id, or null for no palette (`geode_saved_palette_migrate`). */
    external fun savedPaletteMigrate(text: String): String?

    /** A palette name's id, its file stem (`geode_saved_palette_id_for`). */
    external fun savedPaletteIdFor(name: String): String

    /** A saved palette's "id" or "name" (`geode_saved_palette_string`). */
    external fun savedPaletteString(
        text: String,
        key: String,
    ): String

    /** A saved palette's hue base and span (`geode_saved_palette_hues`). */
    external fun savedPaletteHues(text: String): FloatArray

    /** A saved palette's colours, packed 0xRRGGBB (`geode_saved_palette_stops`). */
    external fun savedPaletteStops(text: String): IntArray

    /** How many colours a saved palette holds at most (`geode_saved_palette_max_stops`). */
    external fun savedPaletteMaxStops(): Int

    /** The params [frame] with saved palette [text] as its own palette, as a new frame (`geode_saved_palette_apply`). */
    external fun savedPaletteApply(
        text: String,
        frame: FloatArray,
    ): FloatArray

    /** The saved palette id a look naming [lookId] keeps once [deletedId] is deleted (`geode_saved_palette_kept_id`). */
    external fun savedPaletteKeptId(
        lookId: String,
        deletedId: String,
    ): String

    /**
     * The saved palette id a look naming [lookId] keeps on a params [frame] native wrote (a roll, a reset): the id
     * while the frame still draws its own palette, "" once it does not (`geode_saved_palette_frame_id`).
     */
    external fun savedPaletteFrameId(
        lookId: String,
        frame: FloatArray,
    ): String

    /**
     * A preset's text as org.json read it (core/viz/LookFiles, `geode_preset_read`): its params into [frame], into
     * [strings] (five slots) the name, scene id, custom shader, milk preset (null when absent) and saved palette id,
     * into [envelope] the attack and decay. False, nothing written, when it is no preset.
     */
    external fun presetRead(
        text: String,
        frame: FloatArray,
        strings: Array<String?>,
        envelope: FloatArray,
    ): Boolean

    /** A preset's file text, as org.json wrote it (`geode_preset_write`); null for a short frame. */
    external fun presetWrite(
        name: String,
        sceneId: String,
        attack: Float,
        decay: Float,
        customShader: String?,
        milkPreset: String?,
        frame: FloatArray,
        customPaletteId: String,
    ): String?

    /** The .milk file a preset named [presetName] mirrors its source to (`geode_preset_milk_file_name`). */
    external fun presetMilkFileName(presetName: String): String

    /** [presetRenameProblem] and [presetFolderNameProblem]: no name is left once cleaned. */
    const val NAME_BLANK: Int = 1

    /** [presetRenameProblem] and [presetFolderNameProblem]: another has that name, case ignored. */
    const val NAME_TAKEN: Int = 2

    /** True for a built-in preset's name, which holds " · " (`geode_preset_is_built_in`). */
    external fun presetIsBuiltIn(name: String): Boolean

    /** A preset's name as the store keeps it, or [fallback] when nothing is left (`geode_preset_clean_name`). */
    external fun presetCleanName(
        raw: String,
        fallback: String,
    ): String

    /** A preset folder's name as the store keeps it (`geode_preset_folder_name`). */
    external fun presetFolderName(raw: String): String

    /** 0, [NAME_BLANK] or [NAME_TAKEN] for [raw] as preset [current]'s new name (`geode_preset_rename_problem`). */
    external fun presetRenameProblem(
        raw: String,
        current: String,
        taken: Array<String>,
    ): Int

    /** 0, [NAME_BLANK] or [NAME_TAKEN] for [raw] as a folder's name, renaming [current] or new when null. */
    external fun presetFolderNameProblem(
        raw: String,
        current: String?,
        folders: Array<String>,
    ): Int

    /** A duplicate of preset [name]'s free name beside [taken] (`geode_preset_copy_name`). */
    external fun presetCopyName(
        name: String,
        taken: Array<String>,
    ): String

    /** An imported preset's name, [fallback] when blank, free beside [taken] (`geode_preset_import_name`). */
    external fun presetImportName(
        raw: String,
        fallback: String,
        taken: Array<String>,
    ): String

    /** The user preset among [presets] a save under [raw] overwrites, or null (`geode_preset_replace_target`). */
    external fun presetReplaceTarget(
        raw: String,
        presets: Array<String>,
    ): String?

    /** True when a save under [raw] names a built-in among [presets] (`geode_preset_built_in_clash`). */
    external fun presetBuiltInClash(
        raw: String,
        presets: Array<String>,
    ): Boolean

    /** True when a built-in of style [presetSceneId] is made for style [activeSceneId] (`geode_preset_matches_scene`). */
    external fun presetMatchesScene(
        presetSceneId: String,
        activeSceneId: String,
    ): Boolean

    /** A geode://preset link for an already packed [payload] (`geode_preset_link_frame`). */
    external fun presetLinkFrame(payload: String): String

    /** True when [link] is short enough to share as a link (`geode_preset_link_fits`). */
    external fun presetLinkFits(link: String): Boolean

    /** True when [text] is a preset link (`geode_preset_link_is`). */
    external fun presetLinkIs(text: String): Boolean

    /** The packed payload of the link [text], or null when it is none or too long (`geode_preset_link_payload`). */
    external fun presetLinkPayload(text: String): String?

    /** The first preset link inside pasted [text], or null (`geode_preset_link_find`). */
    external fun presetLinkFind(text: String): String?

    /** True when an unpacked payload is a preset's text (`geode_preset_link_body_accepted`). */
    external fun presetLinkBodyAccepted(body: String): Boolean

    /** The longest link minted or accepted (`geode_preset_link_max_length`). */
    external fun presetLinkMaxLength(): Int

    /** The most bytes a payload may unpack to (`geode_preset_link_max_inflated`). */
    external fun presetLinkMaxInflated(): Int

    /** A template's file in the library: its name's stem, "--", its id's tag and [suffix] (`geode_template_file_name`). */
    external fun templateFileName(
        name: String,
        id: String,
        suffix: String,
    ): String

    /** What every library file of template [id] ends with (`geode_template_file_tail`). */
    external fun templateFileTail(
        id: String,
        suffix: String,
    ): String

    /** The name a newly added template takes beside the [taken] ones (`geode_template_unique_name`). */
    external fun templateUniqueName(
        base: String,
        taken: Array<String>,
    ): String

    /** How many bundled starter templates there are (`geode_template_starter_count`). */
    external fun templateStarterCount(): Int

    /** Bundled starter [index] as a whole template file (`geode_template_starter`). */
    external fun templateStarter(index: Int): String

    /** True for a bundled starter's template id (`geode_template_is_starter`). */
    external fun templateIsStarter(id: String): Boolean

    /**
     * A template file's text as native reads it (core/viz/TemplateFile, `geode_template_read`): 1 with [strings]
     * (seven slots) the text this build writes for it, id, name, scene id, custom shader and milk preset (null when
     * absent) and saved palette id, its params in [frame] and attack and decay in [envelope]; 0 for JSON that is no
     * template and -1 for text that cannot be read, [strings] slot 0 the reason.
     */
    external fun templateRead(
        text: String,
        frame: FloatArray,
        strings: Array<String?>,
        envelope: FloatArray,
    ): Int

    /** A new template's text for this look, under a fresh id, every other section at its default (`geode_template_create`). */
    external fun templateCreate(
        name: String,
        createdAtMs: Long,
        sceneId: String,
        attack: Float,
        decay: Float,
        customShader: String?,
        milkPreset: String?,
        frame: FloatArray,
        customPaletteId: String,
    ): String?

    /** Template [text] under [name]; null when it is no template (`geode_template_renamed`). */
    external fun templateRenamed(
        text: String,
        name: String,
    ): String?

    /** Template [text] as a fresh copy: new id, [name], no author, [createdAtMs] (`geode_template_new_copy`). */
    external fun templateNewCopy(
        text: String,
        name: String,
        createdAtMs: Long,
    ): String?

    /** The geode://template link in [text] as the preset link of the same payload, or null (`geode_template_link_in`). */
    external fun templateLinkIn(text: String): String?

    /** A preset link as the template link of its payload; null past [maxLength] (`geode_template_link_of`). */
    external fun templateLinkOf(
        presetLink: String,
        maxLength: Int,
    ): String?

    /** The file name a shared copy of a template named [name] arrives under (`geode_template_share_name`). */
    external fun templateShareName(name: String): String
}
