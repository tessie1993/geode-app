package dev.geode.billing

import android.content.Context
import com.android.billingclient.api.AcknowledgePurchaseParams
import com.android.billingclient.api.BillingClient
import com.android.billingclient.api.BillingClient.BillingResponseCode
import com.android.billingclient.api.BillingClient.ProductType
import com.android.billingclient.api.BillingClientStateListener
import com.android.billingclient.api.BillingFlowParams
import com.android.billingclient.api.BillingResult
import com.android.billingclient.api.PendingPurchasesParams
import com.android.billingclient.api.ProductDetails
import com.android.billingclient.api.Purchase
import com.android.billingclient.api.PurchasesUpdatedListener
import com.android.billingclient.api.QueryProductDetailsParams
import com.android.billingclient.api.QueryPurchasesParams
import dev.geode.RingLog
import dev.geode.pro.BillingError
import dev.geode.pro.BillingRepository
import dev.geode.pro.ProProduct
import dev.geode.pro.ProductLoad
import dev.geode.pro.PurchaseStatus
import dev.geode.pro.RestoreResult
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlin.coroutines.resume

/**
 * [BillingRepository] on the Play Billing Library (PBL 9).
 *
 * Client-only, because the app has no backend: every exchange is IPC to
 * the Play Store app, which does the networking. That shapes three things here:
 * - Purchases are verified on the device ([PurchaseVerifier]) and acknowledged from the device.
 *   An unacknowledged purchase is refunded by Play after three days, so acknowledgement is retried
 *   on every [refresh] until it sticks.
 * - The entitlement is cached ([EntitlementStore]) with the signed purchase, and outlives an
 *   unreachable Play Store indefinitely: only a successful purchase query that no longer lists
 *   Pro as purchased takes it away ([CachedEntitlement.after]).
 * - `queryPurchasesAsync` on each foreground ([refresh]) is the only way to learn about purchases
 *   made elsewhere: on another device, by promo code, or a pending payment that has since cleared.
 *
 * Callbacks arrive on the main thread; all state changes happen there.
 */
class PlayBillingRepository(
    context: Context,
    private val activities: ForegroundActivityTracker,
    private val store: EntitlementStore,
    private val verifier: PurchaseVerifier,
    private val clock: WallClock,
) : BillingRepository,
    PurchasesUpdatedListener {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val connectLock = Mutex()

    private val client: BillingClient =
        BillingClient
            .newBuilder(context.applicationContext)
            .setListener(this)
            .enablePendingPurchases(PendingPurchasesParams.newBuilder().enableOneTimeProducts().build())
            // PBL 8+: the library re-establishes a dropped service connection itself before the
            // next call, so nothing here reconnects on onBillingServiceDisconnected.
            .enableAutoServiceReconnection()
            .build()

    private val isPro = MutableStateFlow(store.load().vouchesForPro(verifier::isGenuine))
    private val product = MutableStateFlow<ProductLoad>(ProductLoad.Loading)
    private val status = MutableStateFlow<PurchaseStatus>(PurchaseStatus.Idle)

    override fun isProStream(): Flow<Boolean> = isPro.asStateFlow()

    override fun getProductStream(): Flow<ProductLoad> = product.asStateFlow()

    override fun getPurchaseStatusStream(): Flow<PurchaseStatus> = status.asStateFlow()

    override suspend fun refresh() {
        val connected = connect()
        if (connected != BillingResponseCode.OK) {
            // Keep whatever the cache vouches for; only the product goes unavailable.
            product.value = ProductLoad.Unavailable(billingErrorFor(connected))
            return
        }
        loadProduct()
        queryOwned()
    }

    override suspend fun restorePurchases(): RestoreResult {
        val connected = connect()
        if (connected != BillingResponseCode.OK) return RestoreResult.Failed(billingErrorFor(connected))
        return when (val owned = queryOwned()) {
            null -> RestoreResult.Failed(BillingError.UNKNOWN)
            else ->
                when {
                    owned.any { it.purchaseState == Purchase.PurchaseState.PURCHASED } && isPro.value -> RestoreResult.Restored
                    owned.any { it.purchaseState == Purchase.PurchaseState.PENDING } -> RestoreResult.PendingFound
                    else -> RestoreResult.NothingToRestore
                }
        }
    }

    override suspend fun launchPurchase() {
        if (status.value == PurchaseStatus.InProgress) return
        status.value = PurchaseStatus.InProgress
        try {
            startPurchase()
        } catch (e: CancellationException) {
            // Cancelled before Play's sheet opened (connecting, loading the product): nothing is
            // in flight, and a stuck InProgress would refuse every later purchase. Once the sheet
            // is up the coroutine has already returned, and onPurchasesUpdated settles the status.
            status.value = PurchaseStatus.Idle
            throw e
        }
    }

    private suspend fun startPurchase() {
        val connected = connect()
        if (connected != BillingResponseCode.OK) {
            status.value = PurchaseStatus.Failed(billingErrorFor(connected))
            return
        }
        // Queried for each attempt: a kept ProductDetails goes stale and fails the launch.
        val details = loadProduct()
        val activity = activities.current
        if (details == null || activity == null) {
            val whyNoProduct = (product.value as? ProductLoad.Unavailable)?.error ?: BillingError.PRODUCT_UNAVAILABLE
            status.value = PurchaseStatus.Failed(if (details == null) whyNoProduct else BillingError.UNKNOWN)
            return
        }
        val offerToken = details.oneTimePurchaseOfferDetailsList?.firstOrNull()?.offerToken
        val productParams =
            BillingFlowParams.ProductDetailsParams
                .newBuilder()
                .setProductDetails(details)
                .apply { if (offerToken != null) setOfferToken(offerToken) }
                .build()
        val flow = BillingFlowParams.newBuilder().setProductDetailsParamsList(listOf(productParams)).build()
        val launched = withContext(Dispatchers.Main.immediate) { client.launchBillingFlow(activity, flow) }
        when (launched.responseCode) {
            BillingResponseCode.OK -> Unit // The result arrives in onPurchasesUpdated.
            BillingResponseCode.ITEM_ALREADY_OWNED -> settleAlreadyOwned()
            BillingResponseCode.USER_CANCELED -> status.value = PurchaseStatus.Cancelled
            else -> status.value = PurchaseStatus.Failed(billingErrorFor(launched.responseCode))
        }
    }

    override fun clearPurchaseStatus() {
        if (status.value != PurchaseStatus.InProgress) status.value = PurchaseStatus.Idle
    }

    override fun onPurchasesUpdated(
        result: BillingResult,
        purchases: MutableList<Purchase>?,
    ) {
        when (result.responseCode) {
            BillingResponseCode.OK ->
                scope.launch {
                    handle(purchases.orEmpty(), fullList = false)
                    // The flow ended without naming a Pro purchase to grant or wait for: ask Play.
                    if (status.value == PurchaseStatus.InProgress) settleAlreadyOwned()
                }
            BillingResponseCode.USER_CANCELED -> status.value = PurchaseStatus.Cancelled
            BillingResponseCode.ITEM_ALREADY_OWNED -> scope.launch { settleAlreadyOwned() }
            else -> status.value = PurchaseStatus.Failed(billingErrorFor(result.responseCode))
        }
    }

    private suspend fun settleAlreadyOwned() {
        queryOwned()
        status.value = status.value.settled(isPro.value)
    }

    /** Returns the Pro purchases Play reports, or null when the query itself failed. */
    private suspend fun queryOwned(): List<Purchase>? {
        val params = QueryPurchasesParams.newBuilder().setProductType(ProductType.INAPP).build()
        val (result, purchases) =
            suspendCancellableCoroutine { cont ->
                client.queryPurchasesAsync(params) { r, list -> if (cont.isActive) cont.resume(r to list) }
            }
        if (result.responseCode != BillingResponseCode.OK) {
            // PlayAnswer.NoAnswer: an error says nothing about ownership, so the cache stands.
            RingLog.note(TAG, "queryPurchases: ${result.responseCode} ${result.debugMessage}")
            return null
        }
        val pro = purchases.filter { BillingRepository.PRO_PRODUCT_ID in it.products }
        handle(pro, fullList = true)
        return pro
    }

    /**
     * Grants, acknowledges or reports each purchase. [fullList] means [purchases] is everything
     * Play holds for this user, so a missing Pro purchase is a revocation; an update from
     * [onPurchasesUpdated] carries only what changed and never revokes.
     */
    private suspend fun handle(
        purchases: List<Purchase>,
        fullList: Boolean,
    ) {
        val pro = purchases.filter { BillingRepository.PRO_PRODUCT_ID in it.products }
        val completed =
            pro.filter {
                it.purchaseState == Purchase.PurchaseState.PURCHASED && verifier.isGenuine(it.originalJson, it.signature)
            }
        val genuine = completed.firstOrNull()?.let { SignedPurchase(it.originalJson, it.signature) }
        settle(if (fullList) PlayAnswer.FullList(genuine) else PlayAnswer.Update(genuine))
        completed.filterNot { it.isAcknowledged }.forEach { acknowledge(it) }
        status.value =
            status.value.after(
                completed = completed.isNotEmpty(),
                pending = pro.any { it.purchaseState == Purchase.PurchaseState.PENDING },
                anyPro = pro.isNotEmpty(),
                fullList = fullList,
            )
    }

    /** Folds [answer] into the cache and the live flag; [CachedEntitlement.after] decides what may revoke. */
    private fun settle(answer: PlayAnswer) {
        val previous = store.load()
        val next = previous.after(answer, clock.nowMs())
        if (next != previous) store.save(next)
        isPro.value = next.vouchesForPro(verifier::isGenuine)
    }

    private suspend fun acknowledge(purchase: Purchase) {
        val params = AcknowledgePurchaseParams.newBuilder().setPurchaseToken(purchase.purchaseToken).build()
        val result = suspendCancellableCoroutine { cont -> client.acknowledgePurchase(params) { if (cont.isActive) cont.resume(it) } }
        // Not fatal: the next refresh sees isAcknowledged == false and tries again.
        if (result.responseCode != BillingResponseCode.OK) RingLog.note(TAG, "acknowledge: ${result.responseCode} ${result.debugMessage}")
    }

    private suspend fun loadProduct(): ProductDetails? {
        val params =
            QueryProductDetailsParams
                .newBuilder()
                .setProductList(
                    listOf(
                        QueryProductDetailsParams.Product
                            .newBuilder()
                            .setProductId(BillingRepository.PRO_PRODUCT_ID)
                            .setProductType(ProductType.INAPP)
                            .build(),
                    ),
                ).build()
        val (result, details) =
            suspendCancellableCoroutine { cont ->
                client.queryProductDetailsAsync(params) { r, queried ->
                    if (cont.isActive) cont.resume(r to queried.productDetailsList.firstOrNull())
                }
            }
        val offer = details?.oneTimePurchaseOfferDetailsList?.firstOrNull()
        product.value =
            when {
                result.responseCode != BillingResponseCode.OK -> ProductLoad.Unavailable(billingErrorFor(result.responseCode))
                details == null || offer == null -> ProductLoad.Unavailable(BillingError.PRODUCT_UNAVAILABLE)
                else -> ProductLoad.Ready(ProProduct(offer.formattedPrice, offer.priceAmountMicros, offer.priceCurrencyCode))
            }
        return details
    }

    /** Connects if needed and returns Play's response code. */
    private suspend fun connect(): Int =
        connectLock.withLock {
            if (client.isReady) return@withLock BillingResponseCode.OK
            suspendCancellableCoroutine { cont ->
                client.startConnection(
                    setupListener {
                        if (it == BillingResponseCode.SERVICE_DISCONNECTED) RingLog.note(TAG, "billing service disconnected")
                        if (cont.isActive) cont.resume(it)
                    },
                )
            }
        }

    private companion object {
        const val TAG = "PlayBilling"
    }
}

/**
 * Reports how a connection attempt ended through [onResult], a `BillingResponseCode`. A service
 * that drops before setup finished answers SERVICE_DISCONNECTED, as a failed attempt the user can
 * retry: a caller left waiting on a setup that never reports holds up every later refresh, restore
 * and purchase.
 */
internal fun setupListener(onResult: (Int) -> Unit): BillingClientStateListener =
    object : BillingClientStateListener {
        override fun onBillingSetupFinished(result: BillingResult) = onResult(result.responseCode)

        override fun onBillingServiceDisconnected() = onResult(BillingResponseCode.SERVICE_DISCONNECTED)
    }

/**
 * Where a purchase attempt stands after Play reported purchases: [completed] a paid, verified Pro,
 * [pending] one awaiting payment, [anyPro] any Pro purchase at all. [fullList] is a purchase query,
 * which says nothing about a sheet that is still up. A flow result naming no Pro at all leaves the
 * attempt open for [settled], after Play was asked what the user owns.
 */
internal fun PurchaseStatus.after(
    completed: Boolean,
    pending: Boolean,
    anyPro: Boolean,
    fullList: Boolean,
): PurchaseStatus =
    when {
        completed -> if (this == PurchaseStatus.InProgress || this == PurchaseStatus.Pending) PurchaseStatus.Purchased else this
        pending -> PurchaseStatus.Pending
        fullList -> if (this == PurchaseStatus.Pending) PurchaseStatus.Idle else this
        anyPro -> PurchaseStatus.Failed(BillingError.UNKNOWN)
        else -> this
    }

/**
 * Ends an attempt that a purchase query left open (already owned, or a flow result with no
 * purchase): Pro if the user has it, failed otherwise. A status the query settled itself, such as
 * a pending payment, stands.
 */
internal fun PurchaseStatus.settled(isPro: Boolean): PurchaseStatus =
    when {
        this != PurchaseStatus.InProgress -> this
        isPro -> PurchaseStatus.Purchased
        else -> PurchaseStatus.Failed(BillingError.UNKNOWN)
    }

/** The paywall's wording for a failed `BillingResponseCode`. */
internal fun billingErrorFor(responseCode: Int): BillingError =
    when (responseCode) {
        BillingResponseCode.BILLING_UNAVAILABLE, BillingResponseCode.FEATURE_NOT_SUPPORTED -> BillingError.BILLING_UNAVAILABLE
        BillingResponseCode.SERVICE_UNAVAILABLE,
        BillingResponseCode.NETWORK_ERROR,
        BillingResponseCode.SERVICE_DISCONNECTED,
        -> BillingError.NETWORK
        BillingResponseCode.ITEM_UNAVAILABLE -> BillingError.PRODUCT_UNAVAILABLE
        // DEVELOPER_ERROR included: a misused API is not "not sold in your country".
        else -> BillingError.UNKNOWN
    }
