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

    // v3 adds the musical-structure and stereo/chroma fields of AudioFeatures that v2
    // silently zero-filled on load (tempoStability, barPhase, beatInBar, downbeat,
    // downbeatConfidence, novelty, sectionBoundary, buildup, drop, arrival, harmonicity,
    // warmup, beatStrength, beatPhase, transient, pulseConfidence, macroEnergy, kick,
    // snare, hat, stereoWidth, stereoCorrelation, stereoPan, chroma, chromaConfidence).
    // A v2 file fails the version check below and is treated as a cache miss.
    // v4 records the decoded sample rate after hopRateHz, so the drum channels re-derived on
    // load use the track's rate instead of an assumed 48 kHz.
    private const val VERSION = 4
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
                    val sampleRateHz = d.readInt()
                    if (sampleRateHz <= 0) return@runCatching null
                    val key = d.readUTF()
                    val frameCount = d.readInt()
                    val bandCount = d.readInt()
                    val waveSize = d.readInt()
                    val chromaSize = d.readInt()
                    if (frameCount < 0 || frameCount > 1_000_000) return@runCatching null
                    if (bandCount < 0 || bandCount > 4_096) return@runCatching null
                    if (waveSize < 0 || waveSize > 65_536) return@runCatching null
                    if (chromaSize < 0 || chromaSize > 4_096) return@runCatching null
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
                        val beatStrength = d.readFloat()
                        val beatPhase = d.readFloat()
                        val transient = d.readFloat()
                        val pulseConfidence = d.readFloat()
                        val macroEnergy = d.readFloat()
                        val kick = d.readFloat()
                        val snare = d.readFloat()
                        val hat = d.readFloat()
                        val stereoWidth = d.readFloat()
                        val stereoCorrelation = d.readFloat()
                        val stereoPan = d.readFloat()
                        val chroma = FloatArray(chromaSize) { d.readShort() / 8192f }
                        val chromaConfidence = d.readFloat()
                        val tempoStability = d.readFloat()
                        val barPhase = d.readFloat()
                        // readByte() sign-extends; mask to undo the unsigned 0..255 range written below.
                        val beatInBar = d.readByte().toInt() and 0xFF
                        val downbeat = d.readByte().toInt() != 0
                        val downbeatConfidence = d.readFloat()
                        val novelty = d.readFloat()
                        val sectionBoundary = d.readByte().toInt() != 0
                        val buildup = d.readFloat()
                        val drop = d.readByte().toInt() != 0
                        val arrival = d.readByte().toInt() != 0
                        val harmonicity = d.readFloat()
                        val warmup = d.readFloat()
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
                                    beatStrength = beatStrength,
                                    beatPhase = beatPhase,
                                    transient = transient,
                                    pulseConfidence = pulseConfidence,
                                    macroEnergy = macroEnergy,
                                    kick = kick,
                                    snare = snare,
                                    hat = hat,
                                    stereoWidth = stereoWidth,
                                    stereoCorrelation = stereoCorrelation,
                                    stereoPan = stereoPan,
                                    chroma = chroma,
                                    chromaConfidence = chromaConfidence,
                                    tempoStability = tempoStability,
                                    barPhase = barPhase,
                                    beatInBar = beatInBar,
                                    downbeat = downbeat,
                                    downbeatConfidence = downbeatConfidence,
                                    novelty = novelty,
                                    sectionBoundary = sectionBoundary,
                                    buildup = buildup,
                                    drop = drop,
                                    arrival = arrival,
                                    harmonicity = harmonicity,
                                    warmup = warmup,
                                ),
                            )
                    }
                    f.setLastModified(System.currentTimeMillis())
                    FeatureTimeline(frames, hopMs, key, hopRateHz, sampleRateHz)
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
                d.writeInt(t.sampleRateHz)
                d.writeUTF(t.key)
                d.writeInt(t.frames.size)
                val bandCount =
                    t.frames[0]
                        .features.bands.size
                val waveSize =
                    t.frames[0]
                        .features.waveform.size
                val chromaSize =
                    t.frames[0]
                        .features.chroma.size
                d.writeInt(bandCount)
                d.writeInt(waveSize)
                d.writeInt(chromaSize)
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
                    d.writeFloat(fe.beatStrength)
                    d.writeFloat(fe.beatPhase)
                    d.writeFloat(fe.transient)
                    d.writeFloat(fe.pulseConfidence)
                    d.writeFloat(fe.macroEnergy)
                    d.writeFloat(fe.kick)
                    d.writeFloat(fe.snare)
                    d.writeFloat(fe.hat)
                    d.writeFloat(fe.stereoWidth)
                    d.writeFloat(fe.stereoCorrelation)
                    d.writeFloat(fe.stereoPan)
                    for (i in 0 until chromaSize) {
                        d.writeShort(((fe.chroma.getOrElse(i) { 0f }) * 8192f).toInt().coerceIn(-32768, 32767))
                    }
                    d.writeFloat(fe.chromaConfidence)
                    d.writeFloat(fe.tempoStability)
                    d.writeFloat(fe.barPhase)
                    d.writeByte(fe.beatInBar.coerceIn(0, 255))
                    d.writeByte(if (fe.downbeat) 1 else 0)
                    d.writeFloat(fe.downbeatConfidence)
                    d.writeFloat(fe.novelty)
                    d.writeByte(if (fe.sectionBoundary) 1 else 0)
                    d.writeFloat(fe.buildup)
                    d.writeByte(if (fe.drop) 1 else 0)
                    d.writeByte(if (fe.arrival) 1 else 0)
                    d.writeFloat(fe.harmonicity)
                    d.writeFloat(fe.warmup)
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
