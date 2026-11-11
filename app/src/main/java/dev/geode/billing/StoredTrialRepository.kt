package dev.geode.billing

import android.content.SharedPreferences
import dev.geode.pro.TrialRepository
import dev.geode.pro.TrialState
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.transformLatest

/** Read once per call; a wall clock so the trial is an hour of real time, open or not. */
fun interface WallClock {
    fun nowMs(): Long

    companion object {
        val SYSTEM: WallClock = WallClock { System.currentTimeMillis() }
    }
}

/** Where the trial's facts live; an interface so tests can keep them in memory. */
interface TrialStore {
    fun startedAtMs(): Long?

    fun endNoticeShown(): Boolean

    /** The latest wall-clock time the trial was read at, 0 if never: what a clock moved back is measured against. */
    fun lastSeenMs(): Long

    fun saveStartedAt(ms: Long)

    fun saveEndNoticeShown()

    fun saveLastSeen(ms: Long)
}

class PrefsTrialStore(
    private val prefs: SharedPreferences,
) : TrialStore {
    override fun startedAtMs(): Long? = prefs.getLong(KEY_STARTED_AT, 0L).takeIf { it > 0L }

    override fun endNoticeShown(): Boolean = prefs.getBoolean(KEY_END_NOTICE, false)

    override fun saveStartedAt(ms: Long) {
        prefs.edit().putLong(KEY_STARTED_AT, ms).apply()
    }

    override fun saveEndNoticeShown() {
        prefs.edit().putBoolean(KEY_END_NOTICE, true).apply()
    }

    override fun lastSeenMs(): Long = prefs.getLong(KEY_LAST_SEEN, 0L)

    override fun saveLastSeen(ms: Long) {
        prefs.edit().putLong(KEY_LAST_SEEN, ms).apply()
    }

    private companion object {
        const val KEY_STARTED_AT = "trial_started_at"
        const val KEY_END_NOTICE = "trial_end_notice_shown"
        const val KEY_LAST_SEEN = "trial_last_seen"
    }
}

/**
 * [TrialRepository] over a [TrialStore]. The stream re-reads the wall clock at least every
 * [RECHECK_MS] while the trial runs and at the moment the hour runs out, so every gate closes on
 * time without anything else polling. One `delay` to the end was not enough: it counts awake time
 * only, so a phone that slept through the hour kept the trial for the rest of it.
 */
class StoredTrialRepository(
    private val store: TrialStore,
    private val clock: WallClock,
) : TrialRepository {
    private val facts = MutableStateFlow(store.startedAtMs() to store.endNoticeShown())

    @OptIn(ExperimentalCoroutinesApi::class)
    override fun getTrialStream(): Flow<TrialState> =
        facts.transformLatest { (startedAt, noticeShown) ->
            var state = read(startedAt, noticeShown)
            emit(state)
            while (state is TrialState.Active) {
                delay((state.endsAtMs - clock.nowMs()).coerceIn(1L, RECHECK_MS))
                val next = read(startedAt, noticeShown)
                if (next != state) emit(next)
                state = next
            }
        }

    /** The state now, by [TrialState.at]; keeps the clock's high-water mark as it goes. */
    private fun read(
        startedAt: Long?,
        noticeShown: Boolean,
    ): TrialState {
        val now = clock.nowMs()
        val lastSeen = store.lastSeenMs()
        val state = TrialState.at(startedAt, noticeShown, now, lastSeen)
        if (startedAt != null) {
            val seen = TrialState.lastSeenAfter(state, startedAt, now, lastSeen)
            if (seen > lastSeen) store.saveLastSeen(seen)
        }
        return state
    }

    override suspend fun startTrial() {
        if (facts.value.first != null) return
        val now = clock.nowMs()
        store.saveStartedAt(now)
        facts.value = now to facts.value.second
    }

    override suspend fun markEndNoticeShown() {
        store.saveEndNoticeShown()
        facts.value = facts.value.first to true
    }

    private companion object {
        const val RECHECK_MS = 60_000L
    }
}
