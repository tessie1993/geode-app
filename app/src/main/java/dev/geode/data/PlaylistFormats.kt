package dev.geode.data

enum class PlaylistFormat { M3U, PLS, XSPF }

data class PlaylistEntry(
    val location: String,
    val title: String = "",
    val durationMs: Long = PlaylistFormats.UNKNOWN_DURATION,
)

sealed interface PlaylistParse {
    data class Parsed(
        val format: PlaylistFormat,
        val name: String,
        val entries: List<PlaylistEntry>,
    ) : PlaylistParse

    data class Unreadable(val why: String) : PlaylistParse
}

data class PlaylistResolution(
    val uris: List<String>,
    val missing: List<PlaylistEntry>,
    val ambiguous: List<PlaylistEntry>,
)

object PlaylistFormats {
    const val UNKNOWN_DURATION = -1L

    private const val DETECT_WINDOW = 512

    private const val EXTINF = "#EXTINF:"
    private const val PLAYLIST_DIRECTIVE = "#PLAYLIST:"

    private val docType = Regex("<!DOCTYPE", RegexOption.IGNORE_CASE)
    private val trackListTag = Regex("<(?:\\w+:)?trackList\\b[^>]*>(.*)</(?:\\w+:)?trackList>", RegexOption.DOT_MATCHES_ALL)
    private val trackTag = Regex("<(?:\\w+:)?track\\b[^>]*>(.*?)</(?:\\w+:)?track>", RegexOption.DOT_MATCHES_ALL)
    private val numericEntity = Regex("&#(\\d+);")

    private val locationTag = childTag("location")
    private val titleTag = childTag("title")
    private val durationTag = childTag("duration")

    private fun childTag(tag: String): Regex = Regex("<(?:\\w+:)?$tag\\b[^>]*>(.*?)</(?:\\w+:)?$tag>", RegexOption.DOT_MATCHES_ALL)

    private val percentRun = Regex("(?:%[0-9A-Fa-f]{2})+")

    fun parse(
        fileName: String,
        text: String,
    ): PlaylistParse {
        val body = text.removePrefix("\uFEFF")
        return when (formatOf(fileName, body)) {
            null -> PlaylistParse.Unreadable("not an M3U, PLS or XSPF playlist")
            PlaylistFormat.M3U -> parseM3u(fileName, body.lines())
            PlaylistFormat.PLS -> parsePls(fileName, body.lines())
            PlaylistFormat.XSPF -> parseXspf(fileName, body)
        }
    }

    fun resolve(
        entries: List<PlaylistEntry>,
        urisByFileName: Map<String, List<String>>,
    ): PlaylistResolution {
        val uris = ArrayList<String>(entries.size)
        val missing = ArrayList<PlaylistEntry>()
        val ambiguous = ArrayList<PlaylistEntry>()
        for (entry in entries) {
            val candidates = urisByFileName[baseName(entry.location)].orEmpty()
            if (candidates.isEmpty()) {
                missing += entry
                continue
            }
            uris += candidates.first()
            if (candidates.size > 1) ambiguous += entry
        }
        return PlaylistResolution(uris, missing, ambiguous)
    }

    fun baseName(location: String): String {
        val path =
            if (location.contains("://")) {
                location.substringBefore('?').substringBefore('#')
            } else {
                location
            }
        return percentDecoded(path).substringAfterLast('/').substringAfterLast('\\').lowercase()
    }

    private fun formatOf(
        fileName: String,
        text: String,
    ): PlaylistFormat? {
        val head = text.take(DETECT_WINDOW).trimStart()
        return when {
            head.startsWith("#EXTM3U", ignoreCase = true) -> PlaylistFormat.M3U
            head.startsWith("<") && head.contains("playlist", ignoreCase = true) -> PlaylistFormat.XSPF
            head.contains("[playlist]", ignoreCase = true) -> PlaylistFormat.PLS
            else -> byExtension(fileName)
        }
    }

    private fun byExtension(fileName: String): PlaylistFormat? =
        when (fileName.substringAfterLast('.', "").lowercase()) {
            "m3u", "m3u8" -> PlaylistFormat.M3U
            "pls" -> PlaylistFormat.PLS
            "xspf" -> PlaylistFormat.XSPF
            else -> null
        }

    private fun parseM3u(
        fileName: String,
        lines: List<String>,
    ): PlaylistParse {
        var name = ""
        var title = ""
        var durationMs = UNKNOWN_DURATION
        val entries = ArrayList<PlaylistEntry>()
        for (line in lines.map(String::trim).filter(String::isNotEmpty)) {
            if (!line.startsWith("#")) {
                entries += PlaylistEntry(line, title, durationMs)
                title = ""
                durationMs = UNKNOWN_DURATION
            } else if (line.startsWith(PLAYLIST_DIRECTIVE, ignoreCase = true)) {
                name = line.substring(PLAYLIST_DIRECTIVE.length).trim()
            } else if (line.startsWith(EXTINF, ignoreCase = true)) {
                val info = extInf(line.substring(EXTINF.length))
                title = info.first
                durationMs = info.second
            }
        }
        return PlaylistParse.Parsed(PlaylistFormat.M3U, name.ifBlank { stemOf(fileName) }, entries)
    }

    private fun extInf(rest: String): Pair<String, Long> {
        val comma = rest.indexOf(',')
        val seconds = (if (comma < 0) rest else rest.substring(0, comma)).trim().toLongOrNull()
        val title = if (comma < 0) "" else rest.substring(comma + 1).trim()
        return title to secondsToMs(seconds)
    }

    private fun parsePls(
        fileName: String,
        lines: List<String>,
    ): PlaylistParse {
        val files = HashMap<Int, String>()
        val titles = HashMap<Int, String>()
        val lengths = HashMap<Int, Long>()
        for (line in lines) {
            val eq = line.indexOf('=')
            val key = if (eq > 0) line.substring(0, eq).trim().lowercase() else ""
            val index = key.dropWhile { !it.isDigit() }.toIntOrNull() ?: continue
            val value = line.substring(eq + 1).trim()
            when {
                key.startsWith("file") -> files[index] = value
                key.startsWith("title") -> titles[index] = value
                key.startsWith("length") -> lengths[index] = value.toLongOrNull() ?: UNKNOWN_DURATION
            }
        }
        val entries =
            files.keys.sorted().map { i ->
                PlaylistEntry(files.getValue(i), titles[i].orEmpty(), secondsToMs(lengths[i]))
            }
        return PlaylistParse.Parsed(PlaylistFormat.PLS, stemOf(fileName), entries)
    }

    private fun parseXspf(
        fileName: String,
        text: String,
    ): PlaylistParse {
        val list = trackListTag.find(text)
        return when {
            docType.containsMatchIn(text) ->
                PlaylistParse.Unreadable("refused: a playlist with a DOCTYPE can name files the user did not pick")
            list == null -> PlaylistParse.Unreadable("no <trackList> element")
            else ->
                PlaylistParse.Parsed(
                    PlaylistFormat.XSPF,
                    xspfName(text.substring(0, list.range.first), fileName),
                    trackTag.findAll(list.groupValues[1]).mapNotNull(::xspfTrack).toList(),
                )
        }
    }

    private fun xspfName(
        header: String,
        fileName: String,
    ): String = childText(header, titleTag).ifBlank { stemOf(fileName) }

    private fun xspfTrack(match: MatchResult): PlaylistEntry? {
        val body = match.groupValues[1]
        val location = childText(body, locationTag)
        val duration = childText(body, durationTag).toLongOrNull()
        return if (location.isEmpty()) {
            null
        } else {
            PlaylistEntry(
                location,
                childText(body, titleTag),
                if (duration != null && duration >= 0) duration else UNKNOWN_DURATION,
            )
        }
    }

    private fun childText(
        xml: String,
        tag: Regex,
    ): String = decodeEntities(tag.find(xml)?.groupValues?.get(1).orEmpty()).trim()

    private fun secondsToMs(seconds: Long?): Long = if (seconds != null && seconds >= 0) seconds * 1000L else UNKNOWN_DURATION

    private fun stemOf(fileName: String): String = fileName.substringAfterLast('/').substringAfterLast('\\').substringBeforeLast('.')

    private fun percentDecoded(text: String): String =
        percentRun.replace(text) { run ->
            val bytes = run.value.chunked(3).map { escape -> escape.substring(1).toInt(16).toByte() }
            String(bytes.toByteArray(), Charsets.UTF_8)
        }

    private fun decodeEntities(text: String): String =
        numericEntity
            .replace(text) { m ->
                val code = m.groupValues[1].toIntOrNull()
                if (code != null && code in 1..Char.MAX_VALUE.code) code.toChar().toString() else m.value
            }.replace("&lt;", "<")
            .replace("&gt;", ">")
            .replace("&quot;", "\"")
            .replace("&apos;", "'")
            .replace("&amp;", "&")
}
