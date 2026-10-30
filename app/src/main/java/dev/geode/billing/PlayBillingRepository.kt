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
 * Client-only, because the app has no backend and no network permission: every exchange is IPC to
 * the Play Store app, which does the networking. That shapes three things here:
 * - Purchases are verified on the device ([PurchaseVerifier]) and acknowledged from the device.
 *   An unacknowledged purchase is refunded by Play after three days, so acknowledgement is retried
 *   on every [refresh] until it sticks.
 * - The entitlement is cached ([EntitlementStore]) and outlives an unreachable Play Store for
 *   [CachedEntitlement.GRACE_MS].
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
            // next call, so onBillingServiceDisconnected below only logs.
            .enableAutoServiceReconnection()
            .build()

    private val isPro = MutableStateFlow(store.load().vouchesForPro(clock.nowMs()))
    private val product = MutableStateFlow<ProductLoad>(ProductLoad.Loading)
    private val status = MutableStateFlow<PurchaseStatus>(PurchaseStatus.Idle)
    private var productDetails: ProductDetails? = null

    override fun isProStream(): Flow<Boolean> = isPro.asStateFlow()

    override fun getProductStream(): Flow<ProductLoad> = product.asStateFlow()

    override fun getPurchaseStatusStream(): Flow<PurchaseStatus> = status.asStateFlow()

    override suspend fun refresh() {
        val connected = connect()
        if (connected.responseCode != BillingResponseCode.OK) {
            // Keep whatever the cache vouches for; only the product goes unavailable.
            product.value = ProductLoad.Unavailable(connected.toError())
            return
        }
        loadProduct()
        queryOwned()
    }

    override suspend fun restorePurchases(): RestoreResult {
        val connected = connect()
        if (connected.responseCode != BillingResponseCode.OK) return RestoreResult.Failed(connected.toError())
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
        val connected = connect()
        if (connected.responseCode != BillingResponseCode.OK) {
            status.value = PurchaseStatus.Failed(connected.toError())
            return
        }
        val details = productDetails ?: loadProduct()
        val activity = activities.current
        if (details == null || activity == null) {
            status.value = PurchaseStatus.Failed(if (details == null) BillingError.PRODUCT_UNAVAILABLE else BillingError.UNKNOWN)
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
            else -> status.value = PurchaseStatus.Failed(launched.toError())
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
            BillingResponseCode.OK -> scope.launch { handle(purchases.orEmpty(), fullList = false) }
            BillingResponseCode.USER_CANCELED -> status.value = PurchaseStatus.Cancelled
            BillingResponseCode.ITEM_ALREADY_OWNED -> scope.launch { settleAlreadyOwned() }
            else -> status.value = PurchaseStatus.Failed(result.toError())
        }
    }

    private suspend fun settleAlreadyOwned() {
        queryOwned()
        status.value = if (isPro.value) PurchaseStatus.Purchased else PurchaseStatus.Failed(BillingError.UNKNOWN)
    }

    /** Returns the Pro purchases Play reports, or null when the query itself failed. */
    private suspend fun queryOwned(): List<Purchase>? {
        val params = QueryPurchasesParams.newBuilder().setProductType(ProductType.INAPP).build()
        val (result, purchases) =
            suspendCancellableCoroutine { cont ->
                client.queryPurchasesAsync(params) { r, list -> if (cont.isActive) cont.resume(r to list) }
            }
        if (result.responseCode != BillingResponseCode.OK) {
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
        when {
            completed.isNotEmpty() -> {
                grant(owned = true)
                completed.filterNot { it.isAcknowledged }.forEach { acknowledge(it) }
                if (status.value == PurchaseStatus.InProgress || status.value == PurchaseStatus.Pending) {
                    status.value = PurchaseStatus.Purchased
                }
            }
            pro.any { it.purchaseState == Purchase.PurchaseState.PENDING } -> {
                if (fullList) grant(owned = false)
                status.value = PurchaseStatus.Pending
            }
            fullList -> {
                grant(owned = false)
                if (status.value == PurchaseStatus.Pending) status.value = PurchaseStatus.Idle
            }
            pro.isNotEmpty() -> status.value = PurchaseStatus.Failed(BillingError.UNKNOWN)
        }
    }

    private fun grant(owned: Boolean) {
        store.save(CachedEntitlement(owned = owned, verifiedAtMs = clock.nowMs()))
        isPro.value = owned
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
        productDetails = details
        product.value =
            when {
                result.responseCode != BillingResponseCode.OK -> ProductLoad.Unavailable(result.toError())
                details == null || offer == null -> ProductLoad.Unavailable(BillingError.PRODUCT_UNAVAILABLE)
                else -> ProductLoad.Ready(ProProduct(offer.formattedPrice, offer.priceAmountMicros, offer.priceCurrencyCode))
            }
        return details
    }

    private suspend fun connect(): BillingResult =
        connectLock.withLock {
            if (client.isReady) return@withLock OK
            suspendCancellableCoroutine { cont ->
                client.startConnection(
                    object : BillingClientStateListener {
                        override fun onBillingSetupFinished(result: BillingResult) {
                            if (cont.isActive) cont.resume(result)
                        }

                        override fun onBillingServiceDisconnected() {
                            RingLog.note(TAG, "billing service disconnected; the library reconnects on the next call")
                        }
                    },
                )
            }
        }

    private fun BillingResult.toError(): BillingError =
        when (responseCode) {
            BillingResponseCode.BILLING_UNAVAILABLE, BillingResponseCode.FEATURE_NOT_SUPPORTED -> BillingError.BILLING_UNAVAILABLE
            BillingResponseCode.SERVICE_UNAVAILABLE,
            BillingResponseCode.NETWORK_ERROR,
            BillingResponseCode.SERVICE_DISCONNECTED,
            -> BillingError.NETWORK
            BillingResponseCode.ITEM_UNAVAILABLE, BillingResponseCode.DEVELOPER_ERROR -> BillingError.PRODUCT_UNAVAILABLE
            else -> BillingError.UNKNOWN
        }

    private companion object {
        const val TAG = "PlayBilling"
        val OK: BillingResult = BillingResult.newBuilder().setResponseCode(BillingResponseCode.OK).build()
    }
}
