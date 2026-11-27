package dev.geode.diag

import java.io.InputStream

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
        const val MAX_FRAMES = 24

        /**
         * Streams the tombstone: `threads` is a map written in hash order, so the crashing thread can sit after any
         * amount of other threads. Only its value and the small top-level fields are buffered; the rest is skipped.
         */
        fun parse(input: InputStream): Tombstone = Reader(ProtoStream(input)).read()

        private class Reader(
            private val s: ProtoStream,
        ) {
            private var tid = -1L
            private var signal: String? = null
            private var abort: String? = null
            private var crashing: Pair<String?, List<Frame>>? = null

            fun read(): Tombstone {
                s.fields(Long.MAX_VALUE, { number, value -> if (number == TID) tid = value }, ::field)
                return Tombstone(signal, abort?.takeIf { it.isNotBlank() }, crashing?.first, crashing?.second.orEmpty())
            }

            private fun field(
                number: Int,
                len: Long,
            ): Boolean =
                when {
                    number == SIGNAL -> s.bytes(len, FIELD_MAX_BYTES)?.also { signal = signalOf(Proto(it)) } != null
                    number == ABORT -> s.bytes(len, FIELD_MAX_BYTES)?.also { abort = it.toString(Charsets.UTF_8) } != null
                    number == THREADS && crashing == null && tid >= 0 -> entry(len)
                    else -> s.skip(len)
                }

            /** A `threads` map entry (key 1: tid, value 2: Thread); the value is read only when the key is [tid]. */
            private fun entry(len: Long): Boolean {
                var key = -1L
                return s.fields(s.position + len, { number, value -> if (number == 1) key = value }) { number, vlen ->
                    if (number == 2 && key == tid) {
                        s.bytes(vlen, THREAD_MAX_BYTES)?.also { crashing = threadOf(Proto(it)) } != null
                    } else {
                        s.skip(vlen)
                    }
                }
            }
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

        private fun threadOf(body: Proto): Pair<String?, List<Frame>> {
            var name: String? = null
            val frames = ArrayList<Frame>()
            body.forEach { f ->
                when (f.number) {
                    THREAD_NAME -> name = f.string()
                    THREAD_FRAMES -> if (frames.size < MAX_FRAMES) frames += frameOf(f.message())
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

        private const val FIELD_MAX_BYTES = 64 * 1024
        private const val THREAD_MAX_BYTES = 1 shl 20
    }
}

/** Reads a protobuf from a stream front to back; every read returns null or false at the end or on a malformed value. */
private class ProtoStream(
    private val input: InputStream,
) {
    var position = 0L
        private set

    /**
     * Walks the fields up to [end]; [onLength] reads or skips a length-delimited value and says whether to go on.
     * False when the walk stopped on a malformed or cut-off field.
     */
    fun fields(
        end: Long,
        onVarint: (number: Int, value: Long) -> Unit,
        onLength: (number: Int, len: Long) -> Boolean,
    ): Boolean {
        while (position < end) {
            val key = varint() ?: return end == Long.MAX_VALUE
            val number = (key ushr 3).toInt()
            val ok =
                when ((key and WIRE_MASK).toInt()) {
                    WIRE_VARINT -> varint()?.also { onVarint(number, it) } != null
                    WIRE_FIXED64 -> skip(FIXED64_BYTES)
                    WIRE_FIXED32 -> skip(FIXED32_BYTES)
                    WIRE_LENGTH -> (varint() ?: -1L).let { len -> len in 0..end - position && onLength(number, len) }
                    else -> false
                }
            if (!ok || position > end) return false
        }
        return true
    }

    fun varint(): Long? {
        var result = 0L
        var shift = 0
        while (shift < Long.SIZE_BITS) {
            val b = input.read()
            if (b < 0) return null
            position++
            result = result or ((b and 0x7f).toLong() shl shift)
            if (b and 0x80 == 0) return result
            shift += 7
        }
        return null
    }

    fun skip(n: Long): Boolean {
        var left = n
        while (left > 0) {
            val skipped = input.skip(left)
            if (skipped > 0) {
                left -= skipped
            } else {
                if (input.read() < 0) return false
                left--
            }
        }
        position += n
        return true
    }

    /** The first [cap] bytes of a [len]-byte value, the rest skipped. */
    fun bytes(
        len: Long,
        cap: Int,
    ): ByteArray? {
        val buf = ByteArray(minOf(len, cap.toLong()).toInt())
        var read = 0
        while (read < buf.size) {
            val n = input.read(buf, read, buf.size - read)
            if (n < 0) return null
            read += n
        }
        position += read
        return buf.takeIf { skip(len - read) }
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
}

private const val WIRE_MASK = 7L
private const val WIRE_VARINT = 0
private const val WIRE_FIXED64 = 1
private const val WIRE_LENGTH = 2
private const val WIRE_FIXED32 = 5
private const val FIXED64_BYTES = 8L
private const val FIXED32_BYTES = 4L
