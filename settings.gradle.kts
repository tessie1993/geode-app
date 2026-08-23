pluginManagement {
    includeBuild("build-logic")

    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
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
