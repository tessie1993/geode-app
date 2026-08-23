package dev.geode.engine.audio

data class AnalysisBranch(
    val name: String,
    val windowFrames: Int,
    val hopFrames: Int,
) {
    init {
        require(windowFrames > 0 && windowFrames and (windowFrames - 1) == 0) {
            "$name: windowFrames must be a power of two, was $windowFrames"
        }
        require(hopFrames > 0) { "$name: hopFrames must be positive, was $hopFrames" }
        require(windowFrames % 2 == 0) { "$name: an odd window has no centre sample" }
    }

    companion object {
        val TRANSIENT = AnalysisBranch("transient", windowFrames = 512, hopFrames = 256)
        val GENERAL = AnalysisBranch("general", windowFrames = 1024, hopFrames = 512)
        val PITCH = AnalysisBranch("pitch", windowFrames = 4096, hopFrames = 512)
        val HARMONY = AnalysisBranch("harmony", windowFrames = 8192, hopFrames = 512)

        val STACK = listOf(TRANSIENT, GENERAL, PITCH, HARMONY)
    }
}
