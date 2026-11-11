package dev.geode.ui

import android.content.Context
import android.text.format.Formatter
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.EXPORT_FPS_OPTIONS
import dev.geode.data.ExportDestination
import dev.geode.data.ExportPrefsStore
import dev.geode.data.GeodePrefsFiles
import dev.geode.data.RecordPrefs
import dev.geode.data.RecordPrefsStore
import dev.geode.data.exportQualityLabel
import dev.geode.engine.bridge.GeodeNative
import dev.geode.export.ExportQuality
import dev.geode.pro.FeatureGate
import dev.geode.pro.PaywallTrigger
import dev.geode.ui.pro.PaywallViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

private const val SECONDS_PER_MINUTE = 60

/**
 * Settings › Export › Record: what a recorded take (a hold on the visual) renders at, and how long
 * it may run. Resolution and Frame rate are the export's own lists; until one is picked they follow
 * Studio › Render's Custom defaults, which is what a take used before these rows existed
 * ([RecordPrefsStore]). Maximum length stops the recording there and saves it like a manual stop.
 *
 * A choice the entitlement does not cover (4K, above 30 fps) is marked Pro and opens the paywall
 * instead of being kept, through the same [FeatureGate.blockingExportFeature] every export asks.
 *
 * The page only stores and draws: the offered lengths, the size a length means and whether the
 * render lowers its bit rate to fit the muxer come from native (`core/output/RecordPlan`).
 */
@Composable
internal fun RecordSection() {
    val context = LocalContext.current
    val paywall: PaywallViewModel = geodeViewModel()
    val gate by paywall.gate.collectAsStateWithLifecycle()
    val store = remember { RecordPrefsStore(GeodePrefsFiles(context).general) }
    var loaded by remember { mutableStateOf<RecordPrefs?>(null) }
    LaunchedEffect(store) {
        loaded =
            withContext(Dispatchers.IO) {
                store.load(ExportPrefsStore(GeodePrefsFiles(context).general).loadFor(ExportDestination.CUSTOM))
            }
    }
    val prefs = loaded ?: return
    // Keeps a choice the gate allows; offers Pro for one it does not, leaving the row as it was.
    val choose: (RecordPrefs) -> Unit = { next ->
        val blocking = gate.blockingExportFeature(next.quality.shortSide, next.fps)
        if (blocking != null) {
            paywall.open(PaywallTrigger.Feature(blocking))
        } else {
            loaded = next
            store.save(next)
        }
    }
    SettingsHint(stringResource(R.string.record_hint))
    SettingsChoiceRow(
        title = stringResource(R.string.record_resolution),
        options = ExportQuality.entries,
        selected = prefs.quality,
        label = { proMarked(exportQualityLabel(it), gate.blockingExportFeature(it.shortSide, prefs.fps) != null) },
        onSelect = { choose(prefs.copy(quality = it)) },
    )
    SettingsChoiceRow(
        title = stringResource(R.string.export_frame_rate),
        options = EXPORT_FPS_OPTIONS,
        selected = prefs.fps,
        label = { proMarked(stringResource(R.string.record_fps, it), gate.blockingExportFeature(prefs.quality.shortSide, it) != null) },
        onSelect = { choose(prefs.copy(fps = it)) },
    )
    SettingsChoiceRow(
        title = stringResource(R.string.record_max_length),
        options = remember { GeodeNative.recordMaxLengths().toList() },
        selected = prefs.maxLengthSec,
        label = { recordLengthLabel(LocalContext.current, it) },
        onSelect = { length ->
            // A length is never Pro, so it is kept whatever the resolution above is.
            prefs.copy(maxLengthSec = length).let {
                loaded = it
                store.save(it)
            }
        },
    )
    SettingsHint(recordSizeHint(prefs))
    if (gate.exportCapMs != null) SettingsHint(stringResource(R.string.record_free_cap))
}

/** [label], said to be Pro when the entitlement does not cover that choice. */
@Composable
private fun proMarked(
    label: String,
    locked: Boolean,
): String = if (locked) stringResource(R.string.customize_a11y_part_of_pro, label) else label

/**
 * The file size the chosen resolution and length mean: the most a take can weigh at a maximum
 * length, the size of each minute with no limit, and a word when the render lowers the quality to
 * keep a long 4K take inside what an MP4 can hold. The frame rate does not change it: the bit rate
 * is the resolution's.
 */
@Composable
private fun recordSizeHint(prefs: RecordPrefs): String {
    val context = LocalContext.current
    val bps = prefs.quality.bitRate
    val limited = prefs.maxLengthSec > 0
    val seconds = if (limited) prefs.maxLengthSec else SECONDS_PER_MINUTE
    val size = Formatter.formatShortFileSize(context, GeodeNative.recordEstimateBytes(bps, seconds))
    return when {
        !limited -> stringResource(R.string.record_size_per_minute, size)
        GeodeNative.recordRateLowered(bps, seconds) -> stringResource(R.string.record_size_lowered, size)
        else -> stringResource(R.string.record_size_at_most, size)
    }
}

/** A maximum length as the row shows it: "No limit", "30 s", "5 min". */
internal fun recordLengthLabel(
    context: Context,
    seconds: Int,
): String =
    when {
        seconds <= 0 -> context.getString(R.string.record_length_none)
        seconds < SECONDS_PER_MINUTE -> context.getString(R.string.record_length_seconds, seconds)
        else -> context.getString(R.string.record_length_minutes, seconds / SECONDS_PER_MINUTE)
    }

/**
 * Why a take [recordedMs] long stops now, as the note the stage shows, or null while it may run
 * on. The decision is native (`geode_record_limit_reached`); this reads the stored maximum and
 * words the note.
 */
internal fun recordLimitNote(
    context: Context,
    store: RecordPrefsStore,
    recordedMs: Long,
): String? {
    val maxSec = store.maxLengthSec()
    if (!GeodeNative.recordLimitReached(recordedMs, maxSec)) return null
    return context.getString(R.string.record_stopped_at_max, recordLengthLabel(context, maxSec))
}
