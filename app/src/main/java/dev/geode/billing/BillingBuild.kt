package dev.geode.billing

import dev.geode.BuildConfig

/**
 * Which billing this build runs. Fake billing grants Pro from a debug panel and trusts an unsigned
 * cache, so it takes two things to turn it on: the `USE_FAKE_BILLING` flag and a build type other than
 * `release`. The Play build still bills against Play if it set the flag by mistake.
 */
object BillingBuild {
    val usesFake: Boolean get() = usesFake(BuildConfig.USE_FAKE_BILLING, BuildConfig.BUILD_TYPE != "release")

    fun usesFake(
        flag: Boolean,
        notPlayBuild: Boolean,
    ): Boolean = flag && notPlayBuild
}
