plugins {
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.kotlin.compose) apply false
    alias(libs.plugins.ktlint) apply false
    alias(libs.plugins.detekt) apply false
    alias(libs.plugins.ksp) apply false
    alias(libs.plugins.hilt) apply false
}

// Release versioning, one rule for every APK and AAB. N is the GitHub release number, so
// pre-release N is named <base>.N and every one installs over the one before (versionCode
// only grows with N). Local and PR builds have no N and are named <base>-dev.
val releaseNumber = providers.gradleProperty("releaseNumber").orNull?.takeIf { it.isNotBlank() }?.toInt()
val versionBase = providers.gradleProperty("geode.versionBase").get()
val versionCodeBase = providers.gradleProperty("geode.versionCodeBase").get().toInt()
extra["geodeReleaseNumber"] = releaseNumber
extra["geodeVersionName"] = releaseNumber?.let { "$versionBase.$it" } ?: "$versionBase-dev"
extra["geodeVersionCode"] = versionCodeBase + (releaseNumber ?: 0)

tasks.register("checkAll") {
    group = "verification"
    description = "Runs check in every module."
    dependsOn(subprojects.filter { it.buildFile.exists() }.map { "${it.path}:check" })
}
