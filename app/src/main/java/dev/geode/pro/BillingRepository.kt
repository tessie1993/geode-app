package dev.geode.pro

import kotlinx.coroutines.flow.Flow

/**
 * The one-time Pro unlock as the rest of the app sees it.
 *
 * Two implementations: `PlayBillingRepository`, which talks to the Play Store app over its bound
 * service (IPC, so the app still needs no network permission of its own), and
 * `FakeBillingRepository`, which debug builds and tests use instead. Nothing outside the billing
 * package knows which one it has.
 */
interface BillingRepository {
    /**
     * True while the user owns Pro. Starts from the cached entitlement so a Pro user is never shown
     * as free while the Play Store is slow or unreachable, then follows what Play reports.
     */
    fun isProStream(): Flow<Boolean>

    /** The Pro product as Play prices it for this user, or why it cannot be offered. */
    fun getProductStream(): Flow<ProductLoad>

    /** Where the current purchase attempt stands. */
    fun getPurchaseStatusStream(): Flow<PurchaseStatus>

    /**
     * Connects if needed, then re-reads the product and the user's purchases. Called when the app
     * comes to the foreground: that is how a purchase completed elsewhere (another device, a
     * pending payment that cleared, a promo code) reaches the app.
     */
    suspend fun refresh()

    /** Re-reads owned purchases on demand and reports what was found. */
    suspend fun restorePurchases(): RestoreResult

    /** Opens Play's purchase sheet. The outcome arrives through [getPurchaseStatusStream]. */
    suspend fun launchPurchase()

    /** Returns a finished, failed or cancelled attempt to [PurchaseStatus.Idle]. */
    fun clearPurchaseStatus()

    companion object {
        /** The one-time product id. Create it in Play Console under Monetize > One-time products. */
        const val PRO_PRODUCT_ID: String = "muzviz_pro"
    }
}

/** The Pro product, priced by Play in the user's own currency. */
data class ProProduct(
    val formattedPrice: String,
    val priceMicros: Long,
    val currencyCode: String,
)

sealed interface ProductLoad {
    data object Loading : ProductLoad

    data class Ready(
        val product: ProProduct,
    ) : ProductLoad

    data class Unavailable(
        val error: BillingError,
    ) : ProductLoad
}

sealed interface PurchaseStatus {
    data object Idle : PurchaseStatus

    /** The purchase sheet is up, or its result is being processed. */
    data object InProgress : PurchaseStatus

    /**
     * Paid with a slow method (cash, bank transfer). Pro unlocks when Play reports it complete,
     * which may be after the app has been closed and reopened.
     */
    data object Pending : PurchaseStatus

    data object Purchased : PurchaseStatus

    data object Cancelled : PurchaseStatus

    data class Failed(
        val error: BillingError,
    ) : PurchaseStatus
}

/** Billing failures, collapsed to the distinctions the paywall words differently. */
enum class BillingError {
    /** No usable Play Store: not installed, too old, signed out, or blocked (e.g. a kids mode). */
    BILLING_UNAVAILABLE,

    /** Play could not reach its servers. Transient. */
    NETWORK,

    /** The product is not set up or not available in this country. */
    PRODUCT_UNAVAILABLE,

    /** Anything else, including Play's own internal errors. Retrying is reasonable. */
    UNKNOWN,
}

sealed interface RestoreResult {
    data object Restored : RestoreResult

    data object NothingToRestore : RestoreResult

    data object PendingFound : RestoreResult

    data class Failed(
        val error: BillingError,
    ) : RestoreResult
}
