package dev.geode.billing

import dev.geode.pro.BillingError
import dev.geode.pro.BillingRepository
import dev.geode.pro.ProProduct
import dev.geode.pro.ProductLoad
import dev.geode.pro.PurchaseStatus
import dev.geode.pro.RestoreResult
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** What the fake's next purchase does. The debug panel on the paywall switches between these. */
enum class FakePurchaseOutcome {
    SUCCEED,
    PENDING,
    CANCEL,
    FAIL_NETWORK,
    FAIL_UNAVAILABLE,
}

/**
 * [BillingRepository] with no Play Store behind it: what debug builds run (their `.debug`
 * application id is unknown to Play Console, so real billing could not work there anyway) and what
 * ViewModel tests drive.
 *
 * It writes the same [EntitlementStore] the real one does, so in a debug build "owning" Pro here
 * also reaches the wallpaper and screensaver through [ProSnapshot].
 */
class FakeBillingRepository(
    private val store: EntitlementStore = InMemoryEntitlementStore(),
    private val clock: WallClock = WallClock.SYSTEM,
    /** Simulated Play latency. 0 in tests (virtual time makes any value instant anyway). */
    private val latencyMs: Long = 0L,
    product: ProductLoad = ProductLoad.Ready(SAMPLE_PRODUCT),
) : BillingRepository {
    private val isPro = MutableStateFlow(store.load().owned)
    private val productState = MutableStateFlow<ProductLoad>(ProductLoad.Loading)
    private val status = MutableStateFlow<PurchaseStatus>(PurchaseStatus.Idle)
    private val configuredProduct = MutableStateFlow(product)

    /** What the next [launchPurchase] does. */
    val nextOutcome = MutableStateFlow(FakePurchaseOutcome.SUCCEED)

    /** True while a simulated pending payment is waiting for [completePending]. */
    private val pending = MutableStateFlow(false)
    val hasPending: StateFlow<Boolean> = pending.asStateFlow()

    override fun isProStream(): Flow<Boolean> = isPro.asStateFlow()

    override fun getProductStream(): Flow<ProductLoad> = productState.asStateFlow()

    override fun getPurchaseStatusStream(): Flow<PurchaseStatus> = status.asStateFlow()

    override suspend fun refresh() {
        delay(latencyMs)
        productState.value = configuredProduct.value
    }

    override suspend fun restorePurchases(): RestoreResult {
        delay(latencyMs)
        return when {
            isPro.value -> RestoreResult.Restored
            pending.value -> RestoreResult.PendingFound
            else -> RestoreResult.NothingToRestore
        }
    }

    override suspend fun launchPurchase() {
        if (status.value == PurchaseStatus.InProgress) return
        status.value = PurchaseStatus.InProgress
        try {
            delay(latencyMs)
        } catch (e: CancellationException) {
            // The caller went away mid-purchase (the paywall closed): no attempt is running any
            // more, and leaving InProgress would refuse every later purchase.
            status.value = PurchaseStatus.Idle
            throw e
        }
        status.value =
            when (nextOutcome.value) {
                FakePurchaseOutcome.SUCCEED -> {
                    setOwned(true)
                    PurchaseStatus.Purchased
                }
                FakePurchaseOutcome.PENDING -> {
                    pending.value = true
                    PurchaseStatus.Pending
                }
                FakePurchaseOutcome.CANCEL -> PurchaseStatus.Cancelled
                FakePurchaseOutcome.FAIL_NETWORK -> PurchaseStatus.Failed(BillingError.NETWORK)
                FakePurchaseOutcome.FAIL_UNAVAILABLE -> PurchaseStatus.Failed(BillingError.BILLING_UNAVAILABLE)
            }
    }

    override fun clearPurchaseStatus() {
        if (status.value != PurchaseStatus.InProgress) status.value = PurchaseStatus.Idle
    }

    /** Simulates the slow payment clearing, as Play would report it on the next refresh. */
    fun completePending() {
        if (!pending.value) return
        pending.value = false
        setOwned(true)
        status.value = PurchaseStatus.Purchased
    }

    /** Simulates the product failing to load, e.g. not yet created in Play Console. */
    fun setProduct(load: ProductLoad) {
        configuredProduct.value = load
        productState.value = load
    }

    /** Grants or revokes Pro outright: the debug toggle, and a refund in tests. */
    fun setOwned(owned: Boolean) {
        store.save(CachedEntitlement(owned = owned, verifiedAtMs = clock.nowMs()))
        isPro.value = owned
    }

    companion object {
        val SAMPLE_PRODUCT: ProProduct = ProProduct(formattedPrice = "$9.99", priceMicros = 9_990_000L, currencyCode = "USD")
    }
}
