package dev.geode.data

import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.bridge.SceneParamsCodec
import dev.geode.render.scene.SceneParams

/**
 * A take read back for replay or a render: the look in force at a take time, and every style switch. Two kinds
 * answer it: a take's native choices on song time ([TakeChoices], #457) and the older look automation
 * ([PerformanceTake.Timeline]). Times are take time, ms from the take's start ([PerformanceTake]'s
 * `trackOffsetMs`), which is what a take clip's source time is.
 */
interface TakeReplay : AutoCloseable {
    val isEmpty: Boolean

    val durationMs: Long

    /** The play's seed the take was performed with, so a render makes the same kinds of choices; null when unknown. */
    val seed: Long? get() = null

    fun stateAt(ms: Long): PerformanceTake.State?

    fun styleChanges(): List<PerformanceTake.StyleChange>

    fun lastEventMs(): Long

    /** Frees what the replay holds natively; a no-op for the Kotlin kind. */
    override fun close() = Unit
}

/**
 * A take's choices on song time (#457): style, MilkDrop preset and every param from the song position where each
 * changed, and the play's seed. Everything about them is native (`core/signal/timeline/TakeTimeline`): what is
 * kept while recording (only changes, a drag thinned, a seek back punched in, a cap), the file, and the replay.
 * This class only holds the handle and marshals values; [close] it.
 *
 * [offsetMs] is the take's start on the song (the clip's take time 0), so [stateAt] takes take time like every
 * [TakeReplay].
 */
class TakeChoices private constructor(
    private var handle: Long,
    private val offsetMs: Long,
    override val durationMs: Long,
) : TakeReplay {
    private val frame = FloatArray(SceneParamsCodec.FIELDS)
    private var lastIndex = -1
    private var lastState: PerformanceTake.State? = null

    override val isEmpty: Boolean get() = stateIndex(0L) < 0

    override val seed: Long? get() = if (handle != 0L) GeodeNative.takeTimelineSeed(handle) else null

    /** Whether "Render again" is offered: a track take with choices (native decides; #458's takes are not). */
    val rendersAgain: Boolean get() = handle != 0L && GeodeNative.takeTimelineRendersAgain(handle)

    override fun stateAt(ms: Long): PerformanceTake.State? {
        val index = stateIndex(ms + offsetMs)
        if (index < 0) return null
        // The style strings cross once per choice; the params, every call.
        val cached = lastState?.takeIf { index == lastIndex }
        val (sceneId, milkPath) = if (cached != null) cached.sceneId to cached.milkPath else styleOf(index)
        val state = PerformanceTake.State(sceneId, SceneParamsCodec.unpack(frame, SceneParams.DEFAULT), milkPath)
        lastIndex = index
        lastState = state
        return state
    }

    override fun styleChanges(): List<PerformanceTake.StyleChange> {
        if (handle == 0L) return emptyList()
        return GeodeNative.takeTimelineStyleChanges(handle).map { index ->
            val (sceneId, milkPath) = styleOf(index)
            PerformanceTake.StyleChange(GeodeNative.takeTimelineChoiceAtMs(handle, index) - offsetMs, sceneId, milkPath)
        }
    }

    override fun lastEventMs(): Long = if (handle != 0L) GeodeNative.takeTimelineEndMs(handle) - offsetMs else 0L

    override fun close() {
        if (handle != 0L) GeodeNative.takeTimelineDestroy(handle)
        handle = 0L
    }

    private fun stateIndex(songMs: Long): Int = if (handle != 0L) GeodeNative.takeTimelineStateAt(handle, songMs, frame) else -1

    private fun styleOf(index: Int): Pair<String, String?> {
        val style = GeodeNative.takeTimelineChoiceStyle(handle, index)
        val tab = style.indexOf('\t')
        val sceneId = if (tab < 0) style else style.substring(0, tab)
        val milkPath = if (tab < 0) "" else style.substring(tab + 1)
        return sceneId to milkPath.ifEmpty { null }
    }

    /**
     * Records a take's choices while it runs: every look the controller samples goes to native, which keeps what
     * the rules say. [songTime] false for a take over the mic or another app's audio (#458), on wall time.
     */
    class Recorder(
        seed: Long,
        songTime: Boolean,
    ) : AutoCloseable {
        private var handle = GeodeNative.takeTimelineCreate(seed, songTime)
        private val frame = FloatArray(SceneParamsCodec.FIELDS)

        fun note(
            atMs: Long,
            sceneId: String,
            params: SceneParams,
            milkPath: String?,
        ) {
            if (handle == 0L) return
            SceneParamsCodec.pack(params, frame)
            GeodeNative.takeTimelineNote(handle, atMs, sceneId, milkPath.orEmpty(), frame)
        }

        /** The choices as the file the take keeps beside its project. */
        fun encode(): String = if (handle != 0L) GeodeNative.takeTimelineEncode(handle) else ""

        override fun close() {
            if (handle != 0L) GeodeNative.takeTimelineDestroy(handle)
            handle = 0L
        }
    }

    companion object {
        /** A take's choices file read back, or null when it is damaged (native refuses it rather than half reading). */
        fun read(
            text: String,
            offsetMs: Long,
            durationMs: Long,
        ): TakeChoices? {
            val handle = GeodeNative.takeTimelineDecode(text)
            return if (handle == 0L) null else TakeChoices(handle, offsetMs, durationMs)
        }
    }
}
