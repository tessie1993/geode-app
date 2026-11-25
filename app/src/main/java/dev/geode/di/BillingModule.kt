package dev.geode.di

import android.content.Context
import dagger.Lazy
import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.android.qualifiers.ApplicationContext
import dagger.hilt.components.SingletonComponent
import dev.geode.billing.BillingBuild
import dev.geode.billing.EntitlementStore
import dev.geode.billing.FakeBillingRepository
import dev.geode.billing.ForegroundActivityTracker
import dev.geode.billing.PlayBillingRepository
import dev.geode.billing.PrefsEntitlementStore
import dev.geode.billing.PrefsPromptStore
import dev.geode.billing.PrefsShareBonusRepository
import dev.geode.billing.PrefsTrialStore
import dev.geode.billing.PurchaseVerifier
import dev.geode.billing.StoredTrialRepository
import dev.geode.billing.WallClock
import dev.geode.pro.BillingRepository
import dev.geode.pro.PaywallNavigator
import dev.geode.pro.PaywallPrompts
import dev.geode.pro.ShareBonusRepository
import dev.geode.pro.TrialRepository
import javax.inject.Singleton

/**
 * Billing wiring. Release builds talk to Play; debug and sideload builds get [FakeBillingRepository],
 * locked until a tester enters the tester code on the paywall, after which its debug panel switches
 * the outcomes. [BillingBuild.usesFake] decides (the
 * `USE_FAKE_BILLING` flag, and never in the `release` build type), and `-Pmuzviz.fakeBilling=false` turns
 * the real client on in a debug build.
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
    fun provideFakeBilling(store: EntitlementStore): FakeBillingRepository =
        FakeBillingRepository(store, WallClock.SYSTEM, FAKE_LATENCY_MS, requiresTesterCode = true)

    @Provides
    @Singleton
    fun provideBillingRepository(
        @ApplicationContext context: Context,
        activities: ForegroundActivityTracker,
        store: EntitlementStore,
        fake: Lazy<FakeBillingRepository>,
    ): BillingRepository =
        if (BillingBuild.usesFake) {
            fake.get()
        } else {
            PlayBillingRepository(context, activities, store, PurchaseVerifier.forThisBuild(), WallClock.SYSTEM)
        }

    @Provides
    @Singleton
    fun providePaywallPrompts(
        @ApplicationContext context: Context,
        navigator: PaywallNavigator,
    ): PaywallPrompts = PaywallPrompts(PrefsPromptStore(PrefsEntitlementStore.open(context)), navigator, System::currentTimeMillis)

    @Provides
    @Singleton
    fun provideShareBonusRepository(
        @ApplicationContext context: Context,
    ): ShareBonusRepository = PrefsShareBonusRepository(PrefsEntitlementStore.open(context))

    @Provides
    @Singleton
    fun provideTrialRepository(
        @ApplicationContext context: Context,
    ): TrialRepository = StoredTrialRepository(PrefsTrialStore(PrefsEntitlementStore.open(context)), WallClock.SYSTEM)
}
