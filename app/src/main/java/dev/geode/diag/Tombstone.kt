package dev.geode.diag

/**
 * What a crash report needs from a native crash's tombstone: the signal, the abort message and the
 * crashing thread's frames. Android hands the tombstone over as a protobuf
 * (`system/core/debuggerd/proto/tombstone.proto`, [android.app.ApplicationExitInfo.getTraceInputStream]
 * on API 31+), so this reads just those fields; anything it cannot read is left out, never thrown.
 */
internal data class Tombstone(
    val signal: String?,
    val abort: String?,
    val thread: String?,
    val frames: List<Frame>,
) {
    data class Frame(
        val file: String,
        val function: String,
    )

    companion object {
        fun parse(bytes: ByteArray): Tombstone {
            var tid = -1L
            var signal: String? = null
            var abort: String? = null
            val threads = ArrayList<Proto>()
            Proto(bytes).forEach { f ->
                when (f.number) {
                    TID -> tid = f.varint
                    SIGNAL -> signal = signalOf(f.message())
                    ABORT -> abort = f.string()
                    THREADS -> threads += f.message()
                }
            }
            val crashing = threads.firstNotNullOfOrNull { entry -> threadOf(entry, tid) }
            return Tombstone(signal, abort?.takeIf { it.isNotBlank() }, crashing?.first, crashing?.second.orEmpty())
        }

        private fun signalOf(signal: Proto): String? {
            var name: String? = null
            var code: String? = null
            signal.forEach { f ->
                when (f.number) {
                    SIGNAL_NAME -> name = f.string()
                    CODE_NAME -> code = f.string()
                }
            }
            return name?.let { if (code.isNullOrBlank()) it else "$it ($code)" }
        }

        /** A `threads` map entry (key 1: tid, value 2: Thread): the thread's name and frames when it is [tid]. */
        private fun threadOf(
            entry: Proto,
            tid: Long,
        ): Pair<String?, List<Frame>>? {
            var key = -1L
            var thread: Proto? = null
            entry.forEach { f ->
                when (f.number) {
                    1 -> key = f.varint
                    2 -> thread = f.message()
                }
            }
            val body = thread?.takeIf { key == tid } ?: return null
            var name: String? = null
            val frames = ArrayList<Frame>()
            body.forEach { f ->
                when (f.number) {
                    THREAD_NAME -> name = f.string()
                    THREAD_FRAMES -> frames += frameOf(f.message())
                }
            }
            return name to frames
        }

        private fun frameOf(frame: Proto): Frame {
            var function = ""
            var file = ""
            frame.forEach { f ->
                when (f.number) {
                    FRAME_FUNCTION -> function = f.string()
                    FRAME_FILE -> file = f.string()
                }
            }
            return Frame(file, function)
        }

        // tombstone.proto field numbers.
        private const val TID = 6
        private const val SIGNAL = 10
        private const val ABORT = 14
        private const val THREADS = 16
        private const val SIGNAL_NAME = 2
        private const val CODE_NAME = 4
        private const val THREAD_NAME = 2
        private const val THREAD_FRAMES = 4
        private const val FRAME_FUNCTION = 4
        private const val FRAME_FILE = 6
    }
}

/** The protobuf wire format, just enough to walk a message's fields; a truncated or malformed field ends the walk. */
internal class Proto(
    private val bytes: ByteArray,
    private val start: Int = 0,
    private val end: Int = bytes.size,
) {
    class Field(
        private val bytes: ByteArray,
        val number: Int,
        val varint: Long,
        private val from: Int,
        private val to: Int,
    ) {
        fun string(): String = String(bytes, from, to - from, Charsets.UTF_8)

        fun message(): Proto = Proto(bytes, from, to)
    }

    /** Where the last [varint] read ended. */
    private var next = 0

    fun forEach(action: (Field) -> Unit) {
        var p = start
        while (p < end) {
            val key = varint(p) ?: return
            p = next
            val number = (key ushr 3).toInt()
            when ((key and WIRE_MASK).toInt()) {
                WIRE_VARINT -> {
                    val v = varint(p) ?: return
                    action(Field(bytes, number, v, 0, 0))
                    p = next
                }
                WIRE_FIXED64 -> p += 8
                WIRE_LENGTH -> {
                    val len = varint(p) ?: return
                    p = next
                    if (len < 0 || len > end - p) return
                    action(Field(bytes, number, 0, p, p + len.toInt()))
                    p += len.toInt()
                }
                WIRE_FIXED32 -> p += 4
                else -> return
            }
        }
    }

    private fun varint(from: Int): Long? {
        var result = 0L
        var shift = 0
        var p = from
        while (p < end && shift < Long.SIZE_BITS) {
            val b = bytes[p++].toInt()
            result = result or ((b and 0x7f).toLong() shl shift)
            if (b and 0x80 == 0) {
                next = p
                return result
            }
            shift += 7
        }
        return null
    }

    private companion object {
        const val WIRE_MASK = 7L
        const val WIRE_VARINT = 0
        const val WIRE_FIXED64 = 1
        const val WIRE_LENGTH = 2
        const val WIRE_FIXED32 = 5
    }
}
