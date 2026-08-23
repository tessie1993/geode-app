pluginManagement {
    includeBuild("build-logic")

    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
// Resolves Java toolchains from a public index so a machine that lacks the JDK this build asks
// for downloads it instead of failing. Both the daemon JVM criteria in
// gradle/gradle-daemon-jvm.properties and the compile toolchain in geode.kotlin-common depend on
// this being here: updateDaemonJvm refuses to write its per-platform download URLs without it.
plugins {
    id("org.gradle.toolchains.foojay-resolver-convention") version "1.0.0"
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}
rootProject.name = "geode"
include(":app")

include(
    ":engine:audio-core",
    ":engine:gl",
    ":engine:scenes",
    ":engine:audio-android",
    ":engine:runtime",
)
