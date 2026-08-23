package dev.geode.analysis

import android.content.Context
import android.net.Uri
import dev.geode.util.bestEffort
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.File
import java.security.MessageDigest

object AnalysisCache {
    private const val MAGIC = 0x4D564143

    private const val VERSION = 2
    private const val MAX_ENTRIES = 15

    private fun dir(context: Context): File = File(context.filesDir, "analysis").apply { mkdirs() }

    private fun fileFor(
        context: Context,
        uri: Uri,
    ): File {
        val (size, mtime) = contentStamp(context, uri)
        return File(dir(context), cacheKey(uri.toString(), size, mtime) + ".mvac")
    }

    internal fun cacheKey(
        uriString: String,
        sizeBytes: Long,
        lastModifiedMs: Long,
        identity: String = AnalysisIdentity.CURRENT,
    ): String {
        val digest =
            MessageDigest
                .getInstance("SHA-1")
                .digest("$uriString|$sizeBytes|$lastModifiedMs|$identity".toByteArray())
        return digest.joinToString("") { "%02x".format(it) }
    }

    private fun contentStamp(
        context: Context,
        uri: Uri,
    ): Pair<Long, Long> =
        runCatching {
            when (uri.scheme) {
                null, "file" -> {
                    val f = File(uri.path ?: return@runCatching 0L to 0L)
                    f.length() to f.lastModified()
                }
                "content" ->
                    context.contentResolver.query(uri, null, null, null, null)?.use { c ->
                        if (!c.moveToFirst()) return@use 0L to 0L

                        fun col(name: String): Long {
                            val i = c.getColumnIndex(name)
                            return if (i >= 0 && !c.isNull(i)) c.getLong(i) else 0L
                        }
                        val size = col(android.provider.OpenableColumns.SIZE)
                        val mtime =
                            col(android.provider.DocumentsContract.Document.COLUMN_LAST_MODIFIED)
                                .takeIf { it != 0L }
                                ?: (col(android.provider.MediaStore.MediaColumns.DATE_MODIFIED) * 1000L)
                        size to mtime
                    } ?: (0L to 0L)
                else -> 0L to 0L
            }
        }.getOrDefault(0L to 0L)

    fun load(
        context: Context,
        uri: Uri,
        beatSensitivity: Float,
        beatMinIntervalMs: Float,
    ): FeatureTimeline? {
        val f = fileFor(context, uri)
        if (!f.exists()) return null
        val loaded =
            runCatching {
                DataInputStream(f.inputStream().buffered()).use { d ->
                    if (d.readInt() != MAGIC || d.readInt() != VERSION) return@runCatching null
                    val hopMs = d.readLong()
                    val hopRateHz = d.readFloat()
                    val key = d.readUTF()
                    val frameCount = d.readInt()
                    val bandCount = d.readInt()
                    val waveSize = d.readInt()
                    if (frameCount < 0 || frameCount > 1_000_000) return@runCatching null
                    if (bandCount < 0 || bandCount > 4_096) return@runCatching null
                    if (waveSize < 0 || waveSize > 65_536) return@runCatching null
                    val frames = ArrayList<TimelineFrame>(frameCount)
                    repeat(frameCount) {
                        val timeMs = d.readLong()
                        val bands = FloatArray(bandCount) { d.readShort() / 8192f }
                        val wave = FloatArray(waveSize) { d.readShort() / 32767f }
                        val rms = d.readFloat()
                        val bass = d.readFloat()
                        val mid = d.readFloat()
                        val treble = d.readFloat()
                        val onset = d.readFloat()
                        val bpm = d.readFloat()
                        val centroid = d.readFloat()
                        val beat = d.readByte().toInt() != 0
                        val flux = d.readFloat()
                        frames +=
                            TimelineFrame(
                                timeMs,
                                AudioFeatures(
                                    bands = bands,
                                    waveform = wave,
                                    rms = rms,
                                    bass = bass,
                                    mid = mid,
                                    treble = treble,
                                    onset = onset,
                                    beat = beat,
                                    bpm = bpm,
                                    centroid = centroid,
                                    flux = flux,
                                ),
                            )
                    }
                    f.setLastModified(System.currentTimeMillis())
                    FeatureTimeline(frames, hopMs, key, hopRateHz)
                }
            }.getOrNull()
        if (loaded == null) {
            bestEffort(TAG, "f.delete()") { f.delete() }
            return null
        }
        return loaded
            .withBeatSensitivity(beatSensitivity, beatMinIntervalMs)
            .withDrumChannels()
    }

    fun save(
        context: Context,
        uri: Uri,
        t: FeatureTimeline,
    ) {
        if (t.frames.isEmpty()) return
        val f = fileFor(context, uri)
        runCatching {
            DataOutputStream(f.outputStream().buffered()).use { d ->
                d.writeInt(MAGIC)
                d.writeInt(VERSION)
                d.writeLong(t.hopMs)
                d.writeFloat(t.hopRateHz)
                d.writeUTF(t.key)
                d.writeInt(t.frames.size)
                val bandCount =
                    t.frames[0]
                        .features.bands.size
                val waveSize =
                    t.frames[0]
                        .features.waveform.size
                d.writeInt(bandCount)
                d.writeInt(waveSize)
                for (fr in t.frames) {
                    d.writeLong(fr.timeMs)
                    val fe = fr.features
                    for (i in 0 until bandCount) {
                        d.writeShort(((fe.bands.getOrElse(i) { 0f }) * 8192f).toInt().coerceIn(-32768, 32767))
                    }
                    for (i in 0 until waveSize) {
                        d.writeShort(((fe.waveform.getOrElse(i) { 0f }) * 32767f).toInt().coerceIn(-32768, 32767))
                    }
                    d.writeFloat(fe.rms)
                    d.writeFloat(fe.bass)
                    d.writeFloat(fe.mid)
                    d.writeFloat(fe.treble)
                    d.writeFloat(fe.onset)
                    d.writeFloat(fe.bpm)
                    d.writeFloat(fe.centroid)
                    d.writeByte(if (fe.beat) 1 else 0)
                    d.writeFloat(fe.flux)
                }
            }
            evict(context)
        }.onFailure { runCatching { f.delete() } }
    }

    private fun evict(context: Context) {
        val files = dir(context).listFiles()?.sortedBy { it.lastModified() } ?: return
        var excess = files.size - MAX_ENTRIES
        for (f in files) {
            if (excess <= 0) break
            if (f.delete()) excess--
        }
    }

    fun sizeBytes(context: Context): Long = dir(context).listFiles()?.sumOf { it.length() } ?: 0L

    fun entryCount(context: Context): Int = dir(context).listFiles()?.size ?: 0

    fun clear(context: Context) {
        dir(context).listFiles()?.forEach { it.delete() }
    }
}

private const val TAG = "AnalysisCache"
