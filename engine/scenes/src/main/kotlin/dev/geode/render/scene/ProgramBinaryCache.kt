package dev.geode.render.scene

import android.content.Context
import android.opengl.GLES30
import android.os.Process
import android.util.Log
import dev.geode.engine.gl.GlProber
import dev.geode.util.bestEffort
import java.io.DataOutputStream
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit

/**
 * The identity of a linked program as far as the cache is concerned: a hash of the exact
 * vertex and fragment source that produced it.
 *
 * Hashing the *source* rather than a style id is what makes the live GLSL editor safe. An
 * edited fragment shader hashes to a different key, so it gets its own entry and can never
 * be served in place of — or overwrite — the built-in version of the same style. Editing a
 * shader in the tree invalidates exactly that shader's entry for the same reason.
 */
@JvmInline
value class ProgramKey internal constructor(
    val hex: String,
)

/**
 * Persistent `glGetProgramBinary`/`glProgramBinary` cache for [GlUtil.buildProgram].
 *
 * ### Why
 *
 * The registry already defers compilation so a cold start does not pay ~130 `glLinkProgram`
 * calls up front, but that only moves the cost: every style still pays a full compile the
 * first time it is selected, and pays it *again* on every cold start and every EGL context
 * loss — which on Android is routine (backgrounding, display off, wallpaper preview vs
 * home-screen instance; see the quality bar §4.2). A style change during playback rebuilds
 * programs on the GL thread while a transition is animating, so that compile is a visible
 * hitch, not just a startup cost. This turns it into a file read.
 *
 * ### Where the I/O happens
 *
 * - **Reads are on the GL thread, by necessity and by choice.** `glProgramBinary` is a GL
 *   call that needs the bytes in hand, so the read cannot be moved off-thread without
 *   knowing the key in advance, and nothing knows which style the user will pick next.
 *   What matters is *when*: a read only ever happens at the exact point a `glCompileShader`
 *   + `glLinkProgram` pair would otherwise have run, and reading a few hundred KB from
 *   app-private storage is one to two orders of magnitude cheaper than the compile it
 *   replaces. This never adds I/O to a frame that was not already going to stall far worse.
 * - **A miss costs two `isFile` stats**, which is noise next to the compile that follows.
 * - **Writes are off the GL thread.** `glGetProgramBinary` must run on the GL thread (it is
 *   a GL call, and it runs immediately after the link, while the program is fresh), but the
 *   bytes are then handed to a single background thread that owns all file creation,
 *   promotion, eviction and pruning. The GL thread never waits for it. So do eviction
 *   sweeps and the pruning of superseded driver namespaces.
 * - **[prime] does its directory setup synchronously**, once per process: a handful of stats
 *   and two `mkdir`s. Doing it in the background would mean either blocking the GL thread on
 *   a latch or missing the cache for whichever programs are built first — and at a cold start
 *   those are exactly the ones worth catching. Two more syscalls per process go to the crash
 *   sentinel; see [Store.On.sentinel].
 *
 * ### Correctness rules this file exists to keep
 *
 * A program binary is opaque and portable to nothing: not another GPU, not another driver
 * build, not another version of this cache's own format. Every one of those is folded into
 * the directory name, so a system update simply lands in a different namespace and the old
 * one is deleted rather than being misread. And because drivers are entitled to reject a
 * binary they themselves produced, [load] treats *any* failure — a refused format, a short
 * file, a failed link — as a plain cache miss. The caller compiles from source and re-caches.
 * None of this is ever surfaced to the user; the cache is an accelerator or it is nothing.
 */
object ProgramBinaryCache {
    private const val TAG = "ProgramCache"

    /**
     * Bumped whenever the entry layout or the key derivation changes. It is folded into both
     * the namespace hash and every entry header, so an app update that changes either one
     * cannot read a single byte written by the previous version.
     */
    private const val SCHEMA = 1

    private const val MAGIC = 0x47454F44 // "GEOD"

    private const val ROOT_DIR = "gl-program-binaries"
    private const val PROBATION_DIR = "new"
    private const val PROVEN_DIR = "kept"
    private const val SENTINEL_FILE = "loading"
    private const val DISABLED_FILE = "disabled"
    private const val TEMP_PREFIX = "tmp-"

    private const val HEADER_BYTES = 4 * Int.SIZE_BYTES

    /** 128 bits of SHA-256 over the source. Collision here would mean serving the wrong
     * program, so the margin is deliberately enormous rather than merely sufficient. */
    private const val KEY_BYTES = 16

    /** 64 bits is plenty to separate a handful of driver builds from each other, and keeps
     * the directory name readable in a bug report. */
    private const val NAMESPACE_BYTES = 8

    /**
     * Roughly sixty styles, several of which build eight to ten programs each, against
     * typical mobile binaries of 100–400 KB. This budget holds the styles a user actually
     * revisits without pretending to hold every program the app can build. It lives in
     * `cacheDir`, so the worst case of overshooting is that Android reclaims it and the next
     * cold start recompiles once.
     */
    private const val MAX_TOTAL_BYTES = 24L * 1024L * 1024L

    private const val MAX_ENTRIES = 256

    /** A single binary this large is a driver doing something pathological; letting one in
     * would spend the entire budget on it. */
    private const val MAX_ENTRY_BYTES = 4 * 1024 * 1024

    /** Queued writes hold their payload in memory, so the queue is bounded and overflow is
     * discarded — a dropped cache write costs one recompile and nothing else. */
    private const val WRITE_QUEUE_LIMIT = 32

    /** A burst of writes normally sweeps once, when the queue drains. This is the backstop
     * for the pathological case where it never does. */
    private const val SWEEP_AFTER_WRITES = 32

    /** Same reasoning as `GlProber.ERROR_DRAIN_LIMIT`: a driver stuck returning the same
     * error must not spin the GL thread. */
    private const val ERROR_DRAIN_LIMIT = 32

    private val HEX = "0123456789abcdef".toCharArray()

    private sealed interface Store {
        /** No [prime] has run yet, or [install] had not happened when one did. */
        data object Unprimed : Store

        /** Not available this run, with the one line worth logging about why. */
        data class Off(
            val why: String,
        ) : Store

        /**
         * @param probation entries written but never yet read back. Evicted first: a binary
         *   that has not survived a single cold start has not proven it is worth keeping,
         *   which is exactly the shape of the one-shot entries a live-editor session leaves
         *   behind. Without this split an editing session would evict the built-in styles.
         * @param proven entries that have been loaded at least once since they were written,
         *   promoted by a rename on the hit.
         * @param sentinel created immediately before the first `glProgramBinary` of the
         *   process and deleted immediately after it returns. Finding it at [prime] time
         *   means a previous run went down *inside* the driver's binary loader — the one
         *   failure mode a `GL_LINK_STATUS` check cannot catch — so the namespace is wiped
         *   and marked permanently unusable rather than crashing again on every launch. The
         *   window is a single GL call wide, so an unrelated kill landing in it is unlikely;
         *   when it does, the cost is a lost accelerator on that driver, never a wrong frame.
         *   This rests on the app being one process: the manifest declares no
         *   `android:process`, so every wallpaper engine instance shares [sentinelSpent]. A
         *   second process would see a live sentinel as a crash and would need a file lock.
         * @param formats the formats this driver admits to accepting, from
         *   `GL_PROGRAM_BINARY_FORMATS`. An entry naming anything else is discarded unread.
         */
        class On(
            val namespace: File,
            val probation: File,
            val proven: File,
            val sentinel: File,
            val formats: IntArray,
        ) : Store
    }

    @Volatile
    private var root: File? = null

    @Volatile
    private var state: Store = Store.Unprimed

    /**
     * Whether the first `glProgramBinary` of this process has already been guarded by the
     * sentinel. Volatile because several wallpaper engine instances render on separate GL
     * threads; a benign double-arm just costs two more syscalls.
     */
    @Volatile
    private var sentinelSpent = false

    /** Only ever touched on the writer thread. */
    private var writesSinceSweep = 0

    private val writer: ThreadPoolExecutor by lazy {
        ThreadPoolExecutor(
            1,
            1,
            0L,
            TimeUnit.MILLISECONDS,
            ArrayBlockingQueue(WRITE_QUEUE_LIMIT),
            { runnable ->
                Thread({
                    Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND)
                    runnable.run()
                }, "geode-program-cache").apply { isDaemon = true }
            },
            ThreadPoolExecutor.DiscardPolicy(),
        )
    }

    /**
     * Hands the cache the only thing it needs from Android: a place to put files.
     *
     * `cacheDir` rather than `filesDir`, and for the same reason `DeviceGl` uses
     * `noBackupFilesDir`: this is GPU-specific data that must never ride Android Backup onto
     * different silicon. `cacheDir` is excluded from backup *and* reclaimable by the OS under
     * storage pressure, which is the correct contract for something that is pure
     * regenerable speedup.
     *
     * Called from [GlUtil.resolveIncludes] because that is the one place a [Context] already
     * reaches this package on every path that goes on to build a program. Safe to call from
     * any thread, any number of times; only the application context is retained.
     */
    fun install(context: Context) {
        if (root != null) return
        root = File(context.applicationContext.cacheDir, ROOT_DIR)
    }

    /**
     * Resolves the driver namespace and opens it. **GL thread, current context required.**
     *
     * Idempotent and near-free after the first call, so [keyFor] simply calls it rather than
     * making every renderer remember to. A renderer that wants the directory work done
     * before the first program build can call it from `onSurfaceCreated`.
     */
    fun prime() {
        if (state !is Store.Unprimed) return
        val dir = root ?: return // install() has not happened yet; try again on the next build.
        state = openStore(dir)
        when (val opened = state) {
            is Store.Unprimed -> Unit
            is Store.Off -> Log.i(TAG, "program binary cache off: ${opened.why}")
            is Store.On -> {
                Log.i(TAG, "program binary cache at ${opened.namespace.name}, ${opened.formats.size} format(s)")
                submit("prune stale driver namespaces") { prune(dir, opened) }
            }
        }
    }

    /**
     * The cache key for a program, or null when there is nothing to cache into — in which
     * case the caller must not pay for the retrievable hint either.
     *
     * **GL thread, current context required** (it primes the cache on first use).
     */
    fun keyFor(
        vertexSrc: String,
        fragmentSrc: String,
    ): ProgramKey? {
        prime()
        if (state !is Store.On) return null
        val digest = MessageDigest.getInstance("SHA-256")
        // Length-framed, not concatenated: (vertex + "x", fragment) and (vertex, "x" +
        // fragment) must not hash alike, and a pair that differs only in where the boundary
        // falls is exactly what a generated #define prefix produces.
        digest.updateInt(SCHEMA)
        digest.updateFramed(vertexSrc)
        digest.updateFramed(fragmentSrc)
        return ProgramKey(digest.digest().hex(KEY_BYTES))
    }

    /**
     * Asks the driver to keep this program's binary retrievable. **Must be called after
     * `glCreateProgram` and before `glLinkProgram`** — the hint is only honoured at link
     * time, and drivers are free to throw the binary away without it.
     *
     * No-ops when there is nowhere to store the result, so a device with no binary formats
     * never pays the hint's memory cost.
     */
    fun markRetrievable(program: Int) {
        if (state !is Store.On) return
        GLES30.glProgramParameteri(program, GLES30.GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GLES30.GL_TRUE)
    }

    /**
     * Returns a linked program restored from disk, or 0 for a miss. **GL thread.**
     *
     * Every failure below is a miss, deliberately: a rejected format, a truncated or
     * corrupt file, a driver that refuses its own binary. The caller compiles from source,
     * which is what it would have done anyway, and the dead entry is deleted so the next run
     * does not pay for it again.
     */
    fun load(key: ProgramKey): Int {
        val live = state as? Store.On ?: return 0
        val promoted = File(live.proven, key.hex)
        val fromProbation = !promoted.isFile
        val file = if (fromProbation) File(live.probation, key.hex) else promoted
        if (!file.isFile) return 0

        val entry = readEntry(file, live)
        if (entry == null) {
            submit("drop an unreadable program binary") { file.delete() }
            return 0
        }

        val program = GLES30.glCreateProgram()
        if (program == 0) return 0
        loadGuarded(live, program, entry)
        // Drained after, not before: nothing in the render path polls glGetError (the
        // quality bar §4.2 says not to), so the only job here is to keep a rejection from
        // surfacing later as somebody else's mystery error.
        drainErrors()

        val status = IntArray(1)
        GLES30.glGetProgramiv(program, GLES30.GL_LINK_STATUS, status, 0)
        if (status[0] == 0) {
            GLES30.glDeleteProgram(program)
            Log.w(TAG, "driver rejected its own program binary; recompiling from source")
            submit("drop a rejected program binary") { file.delete() }
            return 0
        }

        touch(live, file, fromProbation)
        return program
    }

    /**
     * Reads the binary out of a freshly linked program and queues it for writing.
     * **GL thread**, immediately after a successful link.
     *
     * The `glGetProgramBinary` call has to be here — it is a GL call — but everything after
     * it is a byte array handed to the writer thread.
     */
    fun store(
        key: ProgramKey,
        program: Int,
    ) {
        val live = state as? Store.On ?: return
        val capacity = IntArray(1)
        GLES30.glGetProgramiv(program, GLES30.GL_PROGRAM_BINARY_LENGTH, capacity, 0)
        val size = capacity[0]
        // Zero is a conforming answer from a driver that declined the retrievable hint.
        if (size <= 0 || size > MAX_ENTRY_BYTES) return

        val buffer = ByteBuffer.allocateDirect(size).order(ByteOrder.nativeOrder())
        val written = IntArray(1)
        val format = IntArray(1)
        GLES30.glGetProgramBinary(program, size, written, 0, format, 0, buffer)
        drainErrors()
        val length = written[0]
        if (length <= 0 || length > size) return

        val payload = ByteArray(length)
        buffer.position(0)
        buffer.get(payload)
        val binaryFormat = format[0]
        submit("write a program binary") { write(live, key, binaryFormat, payload) }
    }

    // --- GL thread helpers -------------------------------------------------------------

    private class Entry(
        val format: Int,
        val payload: ByteBuffer,
        val length: Int,
    )

    private fun readEntry(
        file: File,
        live: Store.On,
    ): Entry? {
        val bytes = runCatching { file.readBytes() }.getOrNull() ?: return null
        if (bytes.size <= HEADER_BYTES) return null
        val header = ByteBuffer.wrap(bytes, 0, HEADER_BYTES).order(ByteOrder.BIG_ENDIAN)
        val magic = header.int
        val schema = header.int
        val format = header.int
        val length = header.int
        if (magic != MAGIC || schema != SCHEMA) return null
        if (length <= 0 || bytes.size - HEADER_BYTES != length) return null
        // A format the driver no longer lists is not a corrupt entry, it is one written by a
        // driver that has since been replaced under a namespace hash that did not change.
        if (!live.formats.contains(format)) return null
        // glProgramBinary takes a Buffer; a direct one keeps the JNI layer from having to
        // shadow-copy the payload on its way to the driver.
        val payload = ByteBuffer.allocateDirect(length).order(ByteOrder.nativeOrder())
        payload.put(bytes, HEADER_BYTES, length)
        payload.position(0)
        return Entry(format, payload, length)
    }

    /** See [Store.On.sentinel]: this is the only GL-thread file I/O in the cache, and it
     * happens twice, once per process. */
    private fun loadGuarded(
        live: Store.On,
        program: Int,
        entry: Entry,
    ) {
        val arming = !sentinelSpent
        if (arming) {
            sentinelSpent = true
            bestEffort(TAG, "arm the program binary sentinel") { live.sentinel.createNewFile() }
        }
        GLES30.glProgramBinary(program, entry.format, entry.payload, entry.length)
        if (arming) {
            bestEffort(TAG, "clear the program binary sentinel") { live.sentinel.delete() }
        }
    }

    private fun drainErrors() {
        repeat(ERROR_DRAIN_LIMIT) {
            if (GLES30.glGetError() == GLES30.GL_NO_ERROR) return
        }
    }

    private fun openStore(dir: File): Store {
        val identity = GlProber.identity()
        val formats = binaryFormats()
        if (formats.isEmpty()) {
            // Conforming and legal: GL_NUM_PROGRAM_BINARY_FORMATS may be 0, which means this
            // driver offers no way to save a program. Nothing here is an error, there is
            // simply nothing to do.
            return Store.Off("driver exposes no program binary formats")
        }
        // GL_VERSION is where a driver update shows up; vendor and renderer usually stay
        // put. All three, because a binary is portable to none of them, and SCHEMA on top so
        // an app update that changes the entry layout lands somewhere else entirely.
        val digest = MessageDigest.getInstance("SHA-256")
        digest.updateInt(SCHEMA)
        digest.updateFramed(identity.vendor)
        digest.updateFramed(identity.renderer)
        digest.updateFramed(identity.versionString)
        val namespace = File(dir, "ns-${digest.digest().hex(NAMESPACE_BYTES)}")

        if (File(namespace, DISABLED_FILE).isFile) {
            return Store.Off("a previous run did not survive glProgramBinary on this driver")
        }
        val sentinel = File(namespace, SENTINEL_FILE)
        if (sentinel.isFile) {
            Log.w(TAG, "a previous run died inside glProgramBinary; disabling the cache for this driver")
            bestEffort(TAG, "quarantine the program binary namespace") {
                namespace.deleteRecursively()
                namespace.mkdirs()
                File(namespace, DISABLED_FILE).createNewFile()
            }
            return Store.Off("quarantined after a crash inside the driver's binary loader")
        }

        val probation = File(namespace, PROBATION_DIR)
        val proven = File(namespace, PROVEN_DIR)
        if (!ensureDir(probation) || !ensureDir(proven)) return Store.Off("could not create $namespace")
        return Store.On(namespace, probation, proven, sentinel, formats)
    }

    private fun ensureDir(dir: File): Boolean = runCatching { dir.mkdirs() || dir.isDirectory }.getOrDefault(false)

    private fun binaryFormats(): IntArray {
        val count = IntArray(1)
        GLES30.glGetIntegerv(GLES30.GL_NUM_PROGRAM_BINARY_FORMATS, count, 0)
        drainErrors()
        if (count[0] <= 0) return IntArray(0)
        val formats = IntArray(count[0])
        GLES30.glGetIntegerv(GLES30.GL_PROGRAM_BINARY_FORMATS, formats, 0)
        drainErrors()
        return formats
    }

    // --- writer thread -----------------------------------------------------------------

    private fun submit(
        what: String,
        block: () -> Unit,
    ) {
        bestEffort(TAG, "queue: $what") {
            writer.execute { bestEffort(TAG, what, block) }
        }
    }

    private fun touch(
        live: Store.On,
        file: File,
        fromProbation: Boolean,
    ) {
        val now = System.currentTimeMillis()
        submit("promote a reused program binary") {
            val landed = if (fromProbation) File(live.proven, file.name).also { file.renameTo(it) } else file
            // The directory *is* the index: name for the key, mtime for last use, parent for
            // whether it has proven itself. No side file to keep in sync, so a run that dies
            // mid-session leaves a cache that is still exactly true.
            landed.setLastModified(now)
        }
    }

    private fun write(
        live: Store.On,
        key: ProgramKey,
        format: Int,
        payload: ByteArray,
    ) {
        // Another run already proved this one; re-writing it would demote it to probation.
        if (File(live.proven, key.hex).isFile) return
        // A partial write that dies here leaks its temp file rather than a half entry; the
        // next launch's prune() collects it.
        val temp = File(live.namespace, "$TEMP_PREFIX${key.hex}-${System.nanoTime()}")
        DataOutputStream(temp.outputStream().buffered()).use { out ->
            out.writeInt(MAGIC)
            out.writeInt(SCHEMA)
            out.writeInt(format)
            out.writeInt(payload.size)
            out.write(payload)
        }
        // Rename, never write in place: wallpaper engine instances render on their own GL
        // threads and can be loading the same key while this runs. A reader must see a whole
        // entry or no entry at all.
        if (!temp.renameTo(File(live.probation, key.hex))) {
            temp.delete()
            return
        }
        writesSinceSweep++
        if (writer.queue.isEmpty() || writesSinceSweep >= SWEEP_AFTER_WRITES) {
            writesSinceSweep = 0
            sweep(live)
        }
    }

    private fun sweep(live: Store.On) {
        val probation = live.probation.listFiles().orEmpty()
        val proven = live.proven.listFiles().orEmpty()
        var bytes = probation.sumOf { it.length() } + proven.sumOf { it.length() }
        var count = probation.size + proven.size
        if (bytes <= MAX_TOTAL_BYTES && count <= MAX_ENTRIES) return
        // Unproven entries oldest-first, then proven ones: a binary that has been read back
        // at least once has demonstrated it saves a compile, and is the last thing to go.
        val order = probation.sortedBy { it.lastModified() } + proven.sortedBy { it.lastModified() }
        for (file in order) {
            if (bytes <= MAX_TOTAL_BYTES && count <= MAX_ENTRIES) return
            val size = file.length()
            if (file.delete()) {
                bytes -= size
                count--
            }
        }
    }

    /**
     * Deletes every namespace but the live one, plus any temp file a killed process left
     * behind. A driver update changes the namespace, and without this the binaries it
     * orphaned would sit in `cacheDir` forever.
     */
    private fun prune(
        dir: File,
        live: Store.On,
    ) {
        for (entry in dir.listFiles().orEmpty()) {
            if (entry.name == live.namespace.name) continue
            bestEffort(TAG, "drop program binaries for a previous driver") { entry.deleteRecursively() }
        }
        for (entry in live.namespace.listFiles().orEmpty()) {
            if (!entry.name.startsWith(TEMP_PREFIX)) continue
            bestEffort(TAG, "drop an abandoned partial write") { entry.delete() }
        }
    }

    // --- small helpers -----------------------------------------------------------------

    /** Length-prefixed so no concatenation of fields can be mistaken for another. */
    private fun MessageDigest.updateFramed(text: String) {
        val bytes = text.toByteArray(Charsets.UTF_8)
        updateInt(bytes.size)
        update(bytes)
    }

    private fun MessageDigest.updateInt(value: Int) {
        update(
            byteArrayOf(
                (value ushr 24).toByte(),
                (value ushr 16).toByte(),
                (value ushr 8).toByte(),
                value.toByte(),
            ),
        )
    }

    private fun ByteArray.hex(bytes: Int): String {
        val builder = StringBuilder(bytes * 2)
        for (index in 0 until minOf(bytes, size)) {
            val value = this[index].toInt() and 0xFF
            builder.append(HEX[value ushr 4]).append(HEX[value and 0x0F])
        }
        return builder.toString()
    }
}
