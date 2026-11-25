package dev.geode.pro

/** [PromptStore] for tests. */
class InMemoryPromptStore(
    var sessionCount: Int = 0,
    var visuals: Long = 0L,
    var shown: Boolean = false,
    var stoppedAt: Long = 0L,
) : PromptStore {
    override fun sessions(): Int = sessionCount

    override fun saveSessions(count: Int) {
        sessionCount = count
    }

    override fun visualsMs(): Long = visuals

    override fun saveVisualsMs(ms: Long) {
        visuals = ms
    }

    override fun milestoneShown(): Boolean = shown

    override fun saveMilestoneShown() {
        shown = true
    }

    override fun stoppedAtMs(): Long = stoppedAt

    override fun saveStoppedAt(ms: Long) {
        stoppedAt = ms
    }
}
