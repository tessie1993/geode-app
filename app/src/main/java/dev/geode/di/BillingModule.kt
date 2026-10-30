package dev.geode.di

import android.content.Context
import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.android.qualifiers.ApplicationContext
import dagger.hilt.components.SingletonComponent
import dev.geode.BuildConfig
import dev.geode.billing.EntitlementStore
import dev.geode.billing.FakeBillingRepository
import dev.geode.billing.ForegroundActivityTracker
import dev.geode.billing.PlayBillingRepository
import dev.geode.billing.PrefsEntitlementStore
import dev.geode.billing.PrefsTrialStore
import dev.geode.billing.PurchaseVerifier
import dev.geode.billing.StoredTrialRepository
import dev.geode.billing.WallClock
import dev.geode.pro.BillingRepository
import dev.geode.pro.TrialRepository
import javax.inject.Singleton

/**
 * Billing wiring. Release builds talk to Play; debug builds get [FakeBillingRepository], whose
 * outcomes the paywall's debug panel switches. `BuildConfig.USE_FAKE_BILLING` decides, and
 * `-Pmuzviz.fakeBilling=false` turns the real client on in a debug build for licence testing.
 */
@Module
@InstallIn(SingletonComponent::class)
object BillingModule {
    /** Simulated Play latency, so the paywall's loading states are visible in a debug build. */
    private const val FAKE_LATENCY_MS = 900L

    @Provides
    @Singleton
    fun provideEntitlementStore(
        @ApplicationContext context: Context,
    ): EntitlementStore = PrefsEntitlementStore(PrefsEntitlementStore.open(context))

    @Provides
    @Singleton
    fun provideFakeBilling(store: EntitlementStore): FakeBillingRepository = FakeBillingRepository(store, WallClock.SYSTEM, FAKE_LATENCY_MS)

    @Provides
    @Singleton
    fun provideBillingRepository(
        @ApplicationContext context: Context,
        activities: ForegroundActivityTracker,
        store: EntitlementStore,
        fake: dagger.Lazy<FakeBillingRepository>,
    ): BillingRepository =
        if (BuildConfig.USE_FAKE_BILLING) {
            fake.get()
        } else {
            PlayBillingRepository(context, activities, store, PurchaseVerifier(BuildConfig.PLAY_LICENSE_KEY), WallClock.SYSTEM)
        }

    @Provides
    @Singleton
    fun provideTrialRepository(
        @ApplicationContext context: Context,
    ): TrialRepository = StoredTrialRepository(PrefsTrialStore(PrefsEntitlementStore.open(context)), WallClock.SYSTEM)
}
