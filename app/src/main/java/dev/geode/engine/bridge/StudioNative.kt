package dev.geode.engine.bridge

// The JNI declarations of geode_studio_jni.cpp (CLAUDE.md).
object StudioNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /**
     * A Studio project's edit (timeline, markers, keyframes; core/studio) read from the JSON project.json keeps
     * under "edit", as canonical JSON to store as it is; null when it is no edit. A null [json] is the empty edit.
     */
    external fun studioEditRead(json: String?): String?

    /** A project's kind read from its "kind" JSON, canonical; null when unknown. A null [json] is the standard kind. */
    external fun studioKindRead(json: String?): String?

    /**
     * A new project's edit: the song [songUri] (null for none) on an audio lane and, with [sceneId], the look over
     * the song or the render ([renderMs]), whichever is longer. Null only when native could not make it.
     */
    external fun studioEditNew(
        songUri: String?,
        songTitle: String,
        songMs: Long,
        sceneId: String?,
        renderMs: Long,
    ): String?

    /** The shortest clip native makes: a render range shorter than this has no stretch of song to draw over. */
    external fun studioMinClipMs(): Long
}
