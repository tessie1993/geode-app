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
import dev.geode.data.RecordPrefs
import dev.geode.data.RecordPrefsStore
import dev.geode.data.StudioPrefsStore
import dev.geode.data.exportQualityLabel
import dev.geode.engine.bridge.GeodeNative
import dev.geode.export.ExportQuality
import dev.geode.geodeContainer
import dev.geode.pro.FeatureGate
import dev.geode.pro.PaywallTrigger
import dev.geode.ui.pro.PaywallViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

private const val SECONDS_PER_MINUTE = 60

/**
 * Settings › Export › Record: what a recording (a hold on the visual) is encoded at, and how long it
 * may run. Resolution and Frame rate are the export's own lists; until one is picked they follow
 * Studio › Render's Custom defaults ([RecordPrefsStore]); the file is never sharper than the screen.
 * Maximum length stops the recording there and saves it like a manual stop.
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
    val store = remember { RecordPrefsStore(context.geodeContainer.prefsFiles.general) }
    var loaded by remember { mutableStateOf<RecordPrefs?>(null) }
    LaunchedEffect(store) {
        loaded =
            withContext(Dispatchers.IO) {
                store.load(ExportPrefsStore(context.geodeContainer.prefsFiles.general).loadFor(ExportDestination.CUSTOM))
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
 * The file size the chosen resolution and length mean: the most a recording can weigh at a maximum
 * length, the size of each minute with no limit, and a word when the quality is lowered to keep a
 * long 4K recording inside what an MP4 can hold. The frame rate does not change it: the bit rate
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
 * Settings › Export › Basics: Render's "add it to My videos?" ([StudioPrefsStore.keepAsProject]).
 * Its "don't ask again" used to be final; here it can be asked again, or answered either way.
 */
@Composable
internal fun KeepRendersRow() {
    val context = LocalContext.current
    val store = remember { StudioPrefsStore(context.geodeContainer.prefsFiles.general) }
    var keep by remember { mutableStateOf(store.keepAsProject()) }
    SettingsChoiceRow(
        title = stringResource(R.string.settings_keep_renders),
        options = listOf(null, true, false),
        selected = keep,
        label = {
            stringResource(
                when (it) {
                    null -> R.string.settings_keep_renders_ask
                    true -> R.string.settings_keep_renders_always
                    false -> R.string.settings_keep_renders_never
                },
            )
        },
        onSelect = {
            keep = it
            store.saveKeepAsProject(it)
        },
    )
}
