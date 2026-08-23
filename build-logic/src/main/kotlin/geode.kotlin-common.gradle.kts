plugins {
    id("org.jlleitschuh.gradle.ktlint")
    id("io.gitlab.arturbosch.detekt")
    id("geode.provenance")
}

ktlint {
    android.set(true)
}

// Detekt lives here rather than in app/build.gradle.kts so that it actually covers the code.
// It was applied to :app alone, which meant the whole engine - every scene, the GL capability
// tier, the analysis graph, roughly all of the code that is hard to get right - was outside
// static analysis entirely, while the gate still reported green. Each module carries its own
// baseline of what was already there on the day it was brought under the tool; the point is
// that nothing NEW can regress, which is the same bargain config/detekt/detekt.yml strikes
// with its thresholds.
extensions.configure<io.gitlab.arturbosch.detekt.extensions.DetektExtension> {
    config.setFrom(rootProject.file("config/detekt/detekt.yml"))
    baseline = file("detekt-baseline.xml")
    buildUponDefaultConfig = true
}

tasks.withType<org.jetbrains.kotlin.gradle.tasks.KotlinCompile>().configureEach {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}
