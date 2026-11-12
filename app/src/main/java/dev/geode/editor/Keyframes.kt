package dev.geode.editor

/*
 * Keyframes and the curve editor — Product Spec §9.
 *
 * Any parameter can be animated: transform, opacity, colour, scene settings, FX amounts. A track
 * is a list of keys in time order plus the curve leaving each key.
 *
 * Only the saved shape is left. The owner dropped video editing (ADR 0007, #227), so nothing edits a
 * sheet or draws one: the Kotlin evaluation (valueAt, valuesAt and the bezier solve) went with the
 * Studio project render, its only caller, and a project saved with keys keeps them on disk unread.
 */

@JvmInline
value class ParamId(
    val value: String,
)

@JvmInline
value class KeyframeId(
    val value: String,
)

/** The shapes a parameter value can take. The kind is fixed per track — see [KeyframeTrack.kind]. */
enum class ParamKind {
    SCALAR,
    VECTOR2,
    COLOUR,
    TOGGLE,
    CHOICE,
}

sealed interface ParamValue {
    val kind: ParamKind

    /** Opacity, FX amount, rotation — anything that is one number. */
    data class Scalar(
        val value: Float,
    ) : ParamValue {
        override val kind: ParamKind = ParamKind.SCALAR
    }

    /** Position, scale, any two-axis transform channel. */
    data class Vector2(
        val x: Float,
        val y: Float,
    ) : ParamValue {
        override val kind: ParamKind = ParamKind.VECTOR2
    }

    /**
     * Components are kept in whatever space the renderer already works in, and blended straight.
     * Converting to a perceptual space here would double-convert against the engine's own colour
     * handling, which is a worse error than a slightly dark midpoint.
     */
    data class Colour(
        val r: Float,
        val g: Float,
        val b: Float,
        val a: Float = 1f,
    ) : ParamValue {
        override val kind: ParamKind = ParamKind.COLOUR
    }

    data class Toggle(
        val on: Boolean,
    ) : ParamValue {
        override val kind: ParamKind = ParamKind.TOGGLE
    }

    /** A choice out of a fixed list: palette index, blend mode, symmetry count. */
    data class Choice(
        val index: Int,
    ) : ParamValue {
        override val kind: ParamKind = ParamKind.CHOICE
    }
}

/** A cubic bezier with its end points pinned at (0,0) and (1,1): the curve editor's handles. */
data class BezierCurve(
    val c1x: Float,
    val c1y: Float,
    val c2x: Float,
    val c2y: Float,
)

/** The named presets of the curve editor, each one an ordinary bezier the user can then drag. */
enum class EaseShape(
    val curve: BezierCurve,
) {
    IN(BezierCurve(0.42f, 0f, 1f, 1f)),
    OUT(BezierCurve(0f, 0f, 0.58f, 1f)),
    IN_OUT(BezierCurve(0.42f, 0f, 0.58f, 1f)),
    SMOOTH(BezierCurve(0.4f, 0f, 0.2f, 1f)),
}

/** How the value leaves a key on its way to the next one. */
sealed interface Interpolation {
    /** Stay put and jump at the next key: the right behaviour for scene swaps and switches. */
    data object Hold : Interpolation

    data object Linear : Interpolation

    data class Ease(
        val shape: EaseShape,
    ) : Interpolation

    data class Custom(
        val curve: BezierCurve,
    ) : Interpolation
}

data class Keyframe(
    val id: KeyframeId,
    val atMs: Long,
    val value: ParamValue,
    val interpolation: Interpolation = Interpolation.Linear,
)

/**
 * The animation of one parameter.
 *
 * [clipId] scopes the track to a clip (it moves and dies with that clip); a null [clipId] is a
 * programme-level track that runs for the whole piece. Keys are kept in time order and unique by
 * time.
 */
data class KeyframeTrack(
    val paramId: ParamId,
    val keys: List<Keyframe> = emptyList(),
    val clipId: ClipId? = null,
    val enabled: Boolean = true,
) {
    /** Null until the first key decides what this track animates. */
    val kind: ParamKind? get() = keys.firstOrNull()?.value?.kind

    val isEmpty: Boolean get() = keys.isEmpty()

    fun key(id: KeyframeId): Keyframe? = keys.firstOrNull { it.id == id }
}

/** Every animated parameter in the project, programme-level and clip-scoped alike. */
data class KeyframeSheet(
    val tracks: List<KeyframeTrack> = emptyList(),
) {
    fun track(
        paramId: ParamId,
        clipId: ClipId? = null,
    ): KeyframeTrack? = tracks.firstOrNull { it.paramId == paramId && it.clipId == clipId }

    /** A track is identified by its parameter and its scope; adding one replaces that pair. */
    fun withTrack(track: KeyframeTrack): KeyframeSheet =
        copy(tracks = tracks.filterNot { it.paramId == track.paramId && it.clipId == track.clipId } + track)

    fun withTracks(incoming: List<KeyframeTrack>): KeyframeSheet = incoming.fold(this) { sheet, track -> sheet.withTrack(track) }

    fun removeTrack(
        paramId: ParamId,
        clipId: ClipId? = null,
    ): KeyframeSheet = copy(tracks = tracks.filterNot { it.paramId == paramId && it.clipId == clipId })
}
