package dev.geode.ui.pro

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.lifecycle.compose.LifecycleStartEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.billing.FakePurchaseOutcome
import dev.geode.pro.BillingError
import dev.geode.pro.PaywallTrigger
import dev.geode.pro.ProFeature
import dev.geode.pro.ProductLoad
import dev.geode.pro.PurchaseStatus
import dev.geode.pro.RestoreResult
import dev.geode.pro.TrialState
import dev.geode.ui.geodeViewModel
import kotlinx.coroutines.delay
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

/**
 * Draws the paywall whenever something has asked for it, over everything else.
 *
 * A [Dialog] rather than an overlay in the app's own layout: the paywall can be opened from inside
 * other dialogs (the export settings), and only a newer window reliably sits above an older one.
 * Also refreshes billing on every foreground, which is how a purchase completed elsewhere arrives.
 */
@Composable
fun PaywallHost(reducedMotion: Boolean) {
    val viewModel: PaywallViewModel = geodeViewModel()
    val state by viewModel.uiState.collectAsStateWithLifecycle()
    LifecycleStartEffect(viewModel) {
        viewModel.onForeground()
        onStopOrDispose { }
    }
    if (!state.visible) return
    Dialog(
        onDismissRequest = viewModel::dismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false),
    ) {
        BackHandler(onBack = viewModel::dismiss)
        PaywallScreen(
            state = state,
            reducedMotion = reducedMotion,
            onPurchase = viewModel::purchase,
            onRestore = viewModel::restorePurchases,
            onRetry = viewModel::retry,
            onStartTrial = viewModel::startTrial,
            onClose = viewModel::dismiss,
            debugActions =
                DebugActions(
                    onOwnedChange = viewModel::setDebugOwned,
                    onOutcomeChange = viewModel::setDebugOutcome,
                    onCompletePending = viewModel::completeDebugPending,
                ),
        )
    }
}

class DebugActions(
    val onOwnedChange: (Boolean) -> Unit,
    val onOutcomeChange: (FakePurchaseOutcome) -> Unit,
    val onCompletePending: () -> Unit,
)

/** The paywall itself: stateless, so previews and screenshot tests can drive every state. */
@Composable
fun PaywallScreen(
    state: PaywallUiState,
    reducedMotion: Boolean,
    onPurchase: () -> Unit,
    onRestore: () -> Unit,
    onRetry: () -> Unit,
    onStartTrial: () -> Unit,
    onClose: () -> Unit,
    debugActions: DebugActions? = null,
) {
    Box(
        Modifier
            .fillMaxSize()
            .background(ProColors.background),
    ) {
        Column(
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .systemBarsPadding(),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            VisualHero(reducedMotion = reducedMotion, modifier = Modifier.fillMaxWidth().height(HERO_HEIGHT))
            Column(
                Modifier
                    .widthIn(max = CONTENT_MAX_WIDTH)
                    .fillMaxWidth()
                    .padding(horizontal = 20.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                AnimatedContent(
                    targetState = state.gate.isPro,
                    transitionSpec = { fadeIn() togetherWith fadeOut() },
                    label = "paywall-owned",
                ) { owned ->
                    if (owned) {
                        OwnedContent(onDone = onClose)
                    } else {
                        OfferContent(state, onPurchase, onRestore, onRetry, onStartTrial)
                    }
                }
                if (state.debug != null && debugActions != null) {
                    Spacer(Modifier.height(24.dp))
                    DebugPanel(state.debug, debugActions)
                }
                Spacer(Modifier.height(32.dp))
            }
        }
        IconButton(
            onClick = onClose,
            modifier =
                Modifier
                    .align(Alignment.TopEnd)
                    .systemBarsPadding()
                    .padding(8.dp)
                    .background(ProColors.scrim, CircleShape),
        ) {
            Icon(Icons.Filled.Close, contentDescription = stringResource(R.string.pro_close), tint = Color.White)
        }
    }
}

@Composable
private fun OfferContent(
    state: PaywallUiState,
    onPurchase: () -> Unit,
    onRestore: () -> Unit,
    onRetry: () -> Unit,
    onStartTrial: () -> Unit,
) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        ProBadge()
        Spacer(Modifier.height(12.dp))
        Text(
            stringResource(titleFor(state.trigger)),
            style = MaterialTheme.typography.headlineMedium,
            fontWeight = FontWeight.Bold,
            color = Color.White,
            textAlign = TextAlign.Center,
            modifier = Modifier.semantics { heading() },
        )
        Spacer(Modifier.height(8.dp))
        Text(
            stringResource(if (state.trigger == PaywallTrigger.TrialEnded) R.string.pro_subtitle_trial_ended else R.string.pro_subtitle),
            style = MaterialTheme.typography.bodyLarge,
            color = ProColors.muted,
            textAlign = TextAlign.Center,
        )
        Spacer(Modifier.height(24.dp))
        ComparisonTable(highlighted = state.highlighted)
        Spacer(Modifier.height(16.dp))
        StatusMessage(state)
        Spacer(Modifier.height(16.dp))
        PurchaseButton(state, onPurchase, onRetry)
        Spacer(Modifier.height(8.dp))
        Text(
            stringResource(R.string.pro_price_note),
            style = MaterialTheme.typography.bodySmall,
            color = ProColors.muted,
            textAlign = TextAlign.Center,
        )
        TrialRow(state, onStartTrial)
        TextButton(
            onClick = onRestore,
            enabled = !state.isBusy,
            modifier = Modifier.heightIn(min = MIN_TOUCH),
        ) { Text(stringResource(R.string.pro_restore), color = ProColors.accentLight) }
    }
}

@Composable
private fun PurchaseButton(
    state: PaywallUiState,
    onPurchase: () -> Unit,
    onRetry: () -> Unit,
) {
    val product = state.product
    if (product is ProductLoad.Unavailable) {
        OutlinedButton(
            onClick = onRetry,
            modifier = Modifier.fillMaxWidth().heightIn(min = CTA_HEIGHT),
        ) { Text(stringResource(R.string.pro_retry), color = Color.White) }
        return
    }
    Button(
        onClick = onPurchase,
        enabled = state.canPurchase,
        shape = RoundedCornerShape(16.dp),
        colors =
            ButtonDefaults.buttonColors(
                containerColor = Color.Transparent,
                disabledContainerColor = ProColors.surface,
                contentColor = Color.White,
                disabledContentColor = ProColors.muted,
            ),
        contentPadding = ButtonDefaults.ContentPadding,
        modifier =
            Modifier
                .fillMaxWidth()
                .heightIn(min = CTA_HEIGHT)
                .background(if (state.canPurchase) ProColors.ctaBrush else ProColors.disabledBrush, RoundedCornerShape(16.dp)),
    ) {
        when {
            state.purchase == PurchaseStatus.InProgress || product == ProductLoad.Loading -> {
                CircularProgressIndicator(Modifier.size(20.dp), color = Color.White, strokeWidth = 2.dp)
                if (product == ProductLoad.Loading) {
                    Spacer(Modifier.size(12.dp))
                    Text(stringResource(R.string.pro_price_loading))
                }
            }
            product is ProductLoad.Ready ->
                Text(
                    stringResource(R.string.pro_buy, product.product.formattedPrice),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                )
            else -> Unit
        }
    }
}

@Composable
private fun TrialRow(
    state: PaywallUiState,
    onStartTrial: () -> Unit,
) {
    when (val trial = state.trial) {
        TrialState.Available ->
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Spacer(Modifier.height(12.dp))
                OutlinedButton(
                    onClick = onStartTrial,
                    enabled = state.canStartTrial,
                    shape = RoundedCornerShape(16.dp),
                    modifier = Modifier.fillMaxWidth().heightIn(min = MIN_TOUCH),
                ) { Text(stringResource(R.string.pro_trial_start), color = Color.White) }
                Text(
                    stringResource(R.string.pro_trial_note),
                    style = MaterialTheme.typography.bodySmall,
                    color = ProColors.muted,
                    textAlign = TextAlign.Center,
                    modifier = Modifier.padding(top = 4.dp),
                )
            }
        is TrialState.Active -> {
            val minutes = rememberMinutesLeft(trial.endsAtMs)
            Text(
                pluralStringResource(R.plurals.pro_trial_active, minutes, minutes),
                style = MaterialTheme.typography.labelLarge,
                color = ProColors.accentLight,
                modifier = Modifier.padding(top = 12.dp),
            )
        }
        is TrialState.Ended -> Unit
    }
}

@Composable
private fun rememberMinutesLeft(endsAtMs: Long): Int {
    val now by produceState(System.currentTimeMillis(), endsAtMs) {
        while (true) {
            value = System.currentTimeMillis()
            delay(MINUTE_TICK_MS)
        }
    }
    return (((endsAtMs - now).coerceAtLeast(0L) + MINUTE_MS - 1) / MINUTE_MS).toInt()
}

@Composable
private fun StatusMessage(state: PaywallUiState) {
    val message =
        when (val purchase = state.purchase) {
            PurchaseStatus.Pending -> R.string.pro_status_pending
            PurchaseStatus.Cancelled -> R.string.pro_status_cancelled
            is PurchaseStatus.Failed -> errorText(purchase.error)
            else ->
                when (val restore = state.restore) {
                    RestoreUiState.Running -> R.string.pro_restore_running
                    is RestoreUiState.Done -> restoreText(restore.result)
                    RestoreUiState.Idle -> (state.product as? ProductLoad.Unavailable)?.let { errorText(it.error) }
                }
        }
    // Always present, so TalkBack announces each new message from the same live region.
    Box(
        Modifier
            .fillMaxWidth()
            .semantics { liveRegion = LiveRegionMode.Polite },
        contentAlignment = Alignment.Center,
    ) {
        if (message != null) {
            val isError = state.purchase is PurchaseStatus.Failed || state.product is ProductLoad.Unavailable
            Text(
                stringResource(message),
                style = MaterialTheme.typography.bodyMedium,
                color = if (isError) ProColors.error else ProColors.accentLight,
                textAlign = TextAlign.Center,
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .background(ProColors.surface, RoundedCornerShape(12.dp))
                        .padding(12.dp),
            )
        }
    }
}

private fun errorText(error: BillingError): Int =
    when (error) {
        BillingError.BILLING_UNAVAILABLE -> R.string.pro_error_unavailable
        BillingError.NETWORK -> R.string.pro_error_network
        BillingError.PRODUCT_UNAVAILABLE -> R.string.pro_error_product
        BillingError.UNKNOWN -> R.string.pro_error_unknown
    }

private fun restoreText(result: RestoreResult): Int =
    when (result) {
        RestoreResult.Restored -> R.string.pro_restore_done
        RestoreResult.NothingToRestore -> R.string.pro_restore_nothing
        RestoreResult.PendingFound -> R.string.pro_restore_pending
        is RestoreResult.Failed -> errorText(result.error)
    }

private fun titleFor(trigger: PaywallTrigger?): Int =
    when (trigger) {
        PaywallTrigger.Onboarding -> R.string.pro_title_onboarding
        PaywallTrigger.TrialEnded -> R.string.pro_title_trial_ended
        is PaywallTrigger.Feature ->
            when (trigger.feature) {
                ProFeature.EXPORT_4K -> R.string.pro_title_export_4k
                ProFeature.EXPORT_60FPS -> R.string.pro_title_export_60fps
                ProFeature.NO_WATERMARK -> R.string.pro_title_watermark
                ProFeature.STUDIO -> R.string.pro_title_studio
                ProFeature.LIVE_WALLPAPER -> R.string.pro_title_wallpaper
                ProFeature.SCREENSAVER -> R.string.pro_title_screensaver
                ProFeature.PREMIUM_STYLES -> R.string.pro_title_styles
                ProFeature.ADVANCED_CUSTOMIZATION -> R.string.pro_title_customization
            }
        PaywallTrigger.Settings, null -> R.string.pro_title_default
    }

/** One row of the comparison: a label, and what each tier gets (null: a lock or a check). */
private class CompareRow(
    val label: Int,
    val free: Int?,
    val pro: Int?,
    val features: Set<ProFeature>,
)

private val COMPARE_ROWS =
    listOf(
        CompareRow(R.string.pro_row_styles, R.string.pro_row_styles_free, R.string.pro_row_styles_pro, setOf(ProFeature.PREMIUM_STYLES)),
        CompareRow(
            R.string.pro_row_resolution,
            R.string.pro_row_resolution_free,
            R.string.pro_row_resolution_pro,
            setOf(ProFeature.EXPORT_4K),
        ),
        CompareRow(R.string.pro_row_fps, R.string.pro_row_fps_free, R.string.pro_row_fps_pro, setOf(ProFeature.EXPORT_60FPS)),
        CompareRow(
            R.string.pro_row_watermark,
            R.string.pro_row_watermark_free,
            R.string.pro_row_watermark_pro,
            setOf(ProFeature.NO_WATERMARK),
        ),
        CompareRow(R.string.pro_row_studio, null, null, setOf(ProFeature.STUDIO)),
        CompareRow(R.string.pro_row_wallpaper, null, null, setOf(ProFeature.LIVE_WALLPAPER, ProFeature.SCREENSAVER)),
        CompareRow(R.string.pro_row_customization, null, null, setOf(ProFeature.ADVANCED_CUSTOMIZATION)),
    )

@Composable
private fun ComparisonTable(highlighted: ProFeature?) {
    Column(
        Modifier
            .fillMaxWidth()
            .background(ProColors.surface, RoundedCornerShape(20.dp))
            .border(1.dp, ProColors.hairline, RoundedCornerShape(20.dp))
            .padding(vertical = 8.dp),
    ) {
        Row(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 8.dp)
                .clearAndSetSemantics { },
        ) {
            Text(
                stringResource(R.string.pro_compare_feature),
                Modifier.weight(LABEL_WEIGHT),
                style = headerStyle(),
                color = ProColors.muted,
            )
            Text(
                stringResource(R.string.pro_compare_free),
                Modifier.weight(1f),
                style = headerStyle(),
                color = ProColors.muted,
                textAlign = TextAlign.Center,
            )
            Text(
                stringResource(R.string.pro_compare_pro),
                Modifier.weight(1f),
                style = headerStyle(),
                color = ProColors.accentLight,
                textAlign = TextAlign.Center,
            )
        }
        COMPARE_ROWS.forEach { row -> CompareRowView(row, highlighted = highlighted != null && highlighted in row.features) }
    }
}

@Composable
private fun headerStyle() = MaterialTheme.typography.labelMedium

@Composable
private fun CompareRowView(
    row: CompareRow,
    highlighted: Boolean,
) {
    val label = stringResource(row.label)
    val free = row.free?.let { stringResource(it) } ?: stringResource(R.string.pro_not_included)
    val pro = row.pro?.let { stringResource(it) } ?: stringResource(R.string.pro_included)
    val spoken = stringResource(R.string.pro_row_spoken, label, free, pro)
    Row(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 8.dp, vertical = 2.dp)
            .background(if (highlighted) ProColors.highlight else Color.Transparent, RoundedCornerShape(12.dp))
            .heightIn(min = MIN_TOUCH)
            .padding(horizontal = 8.dp)
            .clearAndSetSemantics { contentDescription = spoken },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            label,
            Modifier.weight(LABEL_WEIGHT),
            style = MaterialTheme.typography.bodyMedium,
            color = Color.White,
            fontWeight = if (highlighted) FontWeight.SemiBold else FontWeight.Normal,
        )
        Box(Modifier.weight(1f), contentAlignment = Alignment.Center) {
            if (row.free != null) {
                Text(free, style = MaterialTheme.typography.bodySmall, color = ProColors.muted, textAlign = TextAlign.Center)
            } else {
                Icon(Icons.Filled.Lock, contentDescription = null, tint = ProColors.muted, modifier = Modifier.size(18.dp))
            }
        }
        Box(Modifier.weight(1f), contentAlignment = Alignment.Center) {
            if (row.pro != null) {
                Text(
                    pro,
                    style = MaterialTheme.typography.bodySmall,
                    color = Color.White,
                    fontWeight = FontWeight.SemiBold,
                    textAlign = TextAlign.Center,
                )
            } else {
                Icon(Icons.Filled.Check, contentDescription = null, tint = ProColors.accentLight, modifier = Modifier.size(20.dp))
            }
        }
    }
}

@Composable
private fun OwnedContent(onDone: () -> Unit) {
    Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.padding(top = 8.dp)) {
        Box(
            Modifier
                .size(72.dp)
                .background(ProColors.ctaBrush, CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            Icon(Icons.Filled.Check, contentDescription = null, tint = Color.White, modifier = Modifier.size(40.dp))
        }
        Spacer(Modifier.height(16.dp))
        Text(
            stringResource(R.string.pro_owned_title),
            style = MaterialTheme.typography.headlineMedium,
            fontWeight = FontWeight.Bold,
            color = Color.White,
            modifier = Modifier.semantics { heading() },
        )
        Spacer(Modifier.height(8.dp))
        Text(
            stringResource(R.string.pro_owned_body),
            style = MaterialTheme.typography.bodyLarge,
            color = ProColors.muted,
            textAlign = TextAlign.Center,
        )
        Spacer(Modifier.height(24.dp))
        Button(
            onClick = onDone,
            shape = RoundedCornerShape(16.dp),
            colors = ButtonDefaults.buttonColors(containerColor = ProColors.accent, contentColor = Color.White),
            modifier = Modifier.fillMaxWidth().heightIn(min = CTA_HEIGHT),
        ) { Text(stringResource(R.string.pro_done), style = MaterialTheme.typography.titleMedium) }
    }
}

@Composable
private fun DebugPanel(
    debug: DebugBillingUiState,
    actions: DebugActions,
) {
    Column(
        Modifier
            .fillMaxWidth()
            .border(1.dp, ProColors.error.copy(alpha = 0.6f), RoundedCornerShape(16.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text(stringResource(R.string.pro_debug_title), style = MaterialTheme.typography.titleSmall, color = ProColors.error)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.pro_debug_owned), Modifier.weight(1f), color = Color.White)
            Switch(checked = debug.owned, onCheckedChange = actions.onOwnedChange)
        }
        Text(stringResource(R.string.pro_debug_next), style = MaterialTheme.typography.labelMedium, color = ProColors.muted)
        Row(
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
        ) {
            FakePurchaseOutcome.entries.forEach { outcome ->
                FilterChip(
                    selected = debug.nextOutcome == outcome,
                    onClick = { actions.onOutcomeChange(outcome) },
                    label = { Text(outcome.name.lowercase().replace('_', ' '), style = MaterialTheme.typography.labelSmall) },
                )
            }
        }
        if (debug.hasPending) {
            OutlinedButton(onClick = actions.onCompletePending) { Text(stringResource(R.string.pro_debug_complete_pending)) }
        }
    }
}

/**
 * The hero: a stand-in for the visualizer drawn in Compose, a ring of spectrum bars over drifting
 * light. Decorative, so hidden from accessibility services, and still under reduced motion.
 * A live GL preview would need a second renderer just for this screen; this costs one Canvas.
 */
@Composable
private fun VisualHero(
    reducedMotion: Boolean,
    modifier: Modifier = Modifier,
) {
    val phase =
        if (reducedMotion) {
            STILL_PHASE
        } else {
            val transition = rememberInfiniteTransition(label = "paywall-hero")
            val animated by transition.animateFloat(
                initialValue = 0f,
                targetValue = TWO_PI,
                animationSpec = infiniteRepeatable(tween(HERO_LOOP_MS, easing = LinearEasing), RepeatMode.Restart),
                label = "paywall-hero-phase",
            )
            animated
        }
    Canvas(modifier.clearAndSetSemantics { }) {
        drawRect(Brush.verticalGradient(listOf(ProColors.heroTop, ProColors.background)))
        val blobs = listOf(ProColors.accent to 0f, ProColors.cyan to 2.1f, ProColors.pink to 4.2f)
        blobs.forEach { (color, offset) ->
            val center =
                Offset(
                    size.width * (0.5f + 0.32f * cos(phase + offset)),
                    size.height * (0.45f + 0.22f * sin(phase * 2f + offset)),
                )
            drawCircle(
                Brush.radialGradient(listOf(color.copy(alpha = 0.55f), Color.Transparent), center, size.minDimension * 0.55f),
                radius = size.minDimension * 0.55f,
                center = center,
            )
        }
        val ringCenter = Offset(size.width / 2f, size.height * 0.5f)
        val inner = size.minDimension * 0.22f
        for (i in 0 until HERO_BARS) {
            val angle = TWO_PI * i / HERO_BARS
            val energy = 0.5f + 0.5f * sin(phase * 3f + i * 0.45f) * cos(phase * 2f - i * 0.21f)
            val length = size.minDimension * (0.05f + 0.16f * energy)
            val dir = Offset(cos(angle), sin(angle))
            drawLine(
                brush = Brush.linearGradient(listOf(ProColors.cyan, ProColors.pink)),
                start = ringCenter + dir * inner,
                end = ringCenter + dir * (inner + length),
                strokeWidth = size.minDimension * 0.012f,
                cap = StrokeCap.Round,
            )
        }
    }
}

/** The paywall's own palette, deliberately independent of the app theme being restyled. */
internal object ProColors {
    val background = Color(0xFF07060D)
    val heroTop = Color(0xFF1B1035)
    val surface = Color(0xFF14121D)
    val scrim = Color(0x66000000)
    val hairline = Color(0x22FFFFFF)
    val accent = Color(0xFF8B5CF6)
    val accentLight = Color(0xFFC4B5FD)
    val cyan = Color(0xFF22D3EE)
    val pink = Color(0xFFEC4899)
    val muted = Color(0xFFA1A1B5)
    val error = Color(0xFFFCA5A5)
    val highlight = Color(0x338B5CF6)
    val ctaBrush = Brush.horizontalGradient(listOf(accent, pink))
    val disabledBrush = Brush.horizontalGradient(listOf(surface, surface))
}

private val HERO_HEIGHT = 240.dp
private val CONTENT_MAX_WIDTH = 560.dp
private val CTA_HEIGHT = 56.dp
private val MIN_TOUCH = 48.dp
private const val LABEL_WEIGHT = 1.6f
private const val HERO_BARS = 56
private const val HERO_LOOP_MS = 14_000
private const val TWO_PI = (2.0 * PI).toFloat()
private const val STILL_PHASE = 0.8f
private const val MINUTE_MS = 60_000L
private const val MINUTE_TICK_MS = 15_000L
