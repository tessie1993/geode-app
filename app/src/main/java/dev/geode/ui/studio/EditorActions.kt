package dev.geode.ui.studio

import android.net.Uri
import dev.geode.editor.ClipId
import dev.geode.editor.EditResult
import dev.geode.editor.EditorProject
import dev.geode.editor.KeyframeId
import dev.geode.editor.MarkerId
import dev.geode.editor.TransientEnvelope
import dev.geode.editor.TransientSource
import dev.geode.export.StudioClip
import dev.geode.render.scene.SceneParams

/** Everything the timeline UI needs from the session, so the composables stay free of the view model. */
interface EditorActions {
    fun edit(transform: (EditorProject) -> EditorProject)

    fun apply(result: EditResult)

    fun undo()

    fun redo()

    fun setPlayhead(ms: Long)

    fun newClipId(): ClipId

    fun newMarkerId(): MarkerId

    fun newKeyframeId(): KeyframeId

    /** The analysed track's envelope, or null until the current track has been analysed. */
    fun transientEnvelope(source: TransientSource): TransientEnvelope?

    fun currentSceneId(): String

    fun currentSceneParams(): SceneParams

    /** The player's position while it plays, for tapping markers in against the music; null when parked. */
    fun playbackPositionMs(): Long?

    fun describeMedia(
        uri: Uri,
        onReady: (StudioClip) -> Unit,
    )
}
