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

/** Where the trial's two facts live; an interface so tests can keep them in memory. */
interface TrialStore {
    fun startedAtMs(): Long?

    fun endNoticeShown(): Boolean

    fun saveStartedAt(ms: Long)

    fun saveEndNoticeShown()
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

    private companion object {
        const val KEY_STARTED_AT = "trial_started_at"
        const val KEY_END_NOTICE = "trial_end_notice_shown"
    }
}

/**
 * [TrialRepository] over a [TrialStore]. The stream re-emits at the moment the hour runs out, so
 * every gate closes on time without anything polling.
 */
class StoredTrialRepository(
    private val store: TrialStore,
    private val clock: WallClock,
) : TrialRepository {
    private val facts = MutableStateFlow(store.startedAtMs() to store.endNoticeShown())

    @OptIn(ExperimentalCoroutinesApi::class)
    override fun getTrialStream(): Flow<TrialState> =
        facts.transformLatest { (startedAt, noticeShown) ->
            val state = TrialState.at(startedAt, noticeShown, clock.nowMs())
            emit(state)
            if (state is TrialState.Active) {
                delay((state.endsAtMs - clock.nowMs()).coerceAtLeast(0L))
                emit(TrialState.at(startedAt, noticeShown, clock.nowMs().coerceAtLeast(state.endsAtMs)))
            }
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
}
