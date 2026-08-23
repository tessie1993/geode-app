package dev.geode.engine.gl

data class GlVersion(val major: Int, val minor: Int) : Comparable<GlVersion> {
    override fun compareTo(other: GlVersion): Int = compareValuesBy(this, other, GlVersion::major, GlVersion::minor)

    override fun toString(): String = "$major.$minor"

    companion object {
        private val ES_VERSION = Regex("""^OpenGL ES(?:-C[ML])? (\d+)\.(\d+)""")

        fun parse(versionString: String): GlVersion? =
            ES_VERSION.find(versionString)?.let { match ->
                GlVersion(match.groupValues[1].toInt(), match.groupValues[2].toInt())
            }
    }
}
