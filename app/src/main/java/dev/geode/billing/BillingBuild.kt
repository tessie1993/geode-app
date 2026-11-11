package dev.geode.billing

import dev.geode.BuildConfig

/**
 * Which billing this build runs. Fake billing grants Pro from a debug panel and trusts an unsigned
 * cache, so it takes two things to turn it on: the `USE_FAKE_BILLING` flag and a debuggable build.
 * A release build type that set the flag by mistake still bills against Play.
 */
object BillingBuild {
    val usesFake: Boolean get() = usesFake(BuildConfig.USE_FAKE_BILLING, BuildConfig.DEBUG)

    fun usesFake(
        flag: Boolean,
        debuggable: Boolean,
    ): Boolean = flag && debuggable
}
