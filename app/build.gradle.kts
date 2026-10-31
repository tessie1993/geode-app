import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Properties
import java.util.zip.ZipFile

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
    id("geode.kotlin-common")
    alias(libs.plugins.ksp)
    alias(libs.plugins.hilt)
}

val keystoreProps =
    Properties().apply {
        val f = rootProject.file("keystore.properties")
        if (f.exists()) f.inputStream().use { load(it) }
    }

fun releaseSecret(
    propKey: String,
    envKey: String,
): String? = keystoreProps.getProperty(propKey) ?: System.getenv(envKey)

val releaseStorePath = releaseSecret("storeFile", "GEODE_KEYSTORE")
val releaseStorePassword = releaseSecret("storePassword", "GEODE_KEYSTORE_PASSWORD")
val releaseKeyAlias = releaseSecret("keyAlias", "GEODE_KEY_ALIAS")
val releaseKeyPassword = releaseSecret("keyPassword", "GEODE_KEY_PASSWORD")
val hasReleaseSigning =
    releaseStorePath != null &&
        releaseStorePassword != null &&
        releaseKeyAlias != null &&
        releaseKeyPassword != null

android {
    namespace = "dev.geode"
    // 37 is the floor the Compose 1.12 / lifecycle 2.11 / hilt-navigation 1.4
    // AARs declare; targetSdk stays where it is.
    compileSdk = 37
    // r28 is the first NDK that aligns shared objects to 16 KB pages by default.
    ndkVersion = "28.0.13004108"

    defaultConfig {
        applicationId = "com.muzviz.studiopro"
        minSdk = 26
        targetSdk = 36
        // Set once in gradle.properties and the root build.gradle.kts (release versioning).
        versionCode = rootProject.extra["geodeVersionCode"] as Int
        versionName = rootProject.extra["geodeVersionName"] as String
        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_shared")
            }
        }

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        // Orchestrator runs every test in its own instrumentation and wipes the app's data
        // between them, so a journey that writes prefs or the PlayerSession singleton can't
        // leak state into the next one. Tests that need a past first run seed it themselves.
        testInstrumentationRunnerArguments["clearPackageData"] = "true"

        // The public half of the Play licence key (Play Console > Monetization setup > Licensing),
        // used to check purchase signatures on the device. Public, not a secret; empty skips the
        // check. Supplied as the muzviz.playLicenseKey property or PLAY_LICENSE_KEY env variable.
        val playLicenseKey =
            (findProperty("muzviz.playLicenseKey") as String?)
                ?: System.getenv("PLAY_LICENSE_KEY").orEmpty()
        buildConfigField("String", "PLAY_LICENSE_KEY", "\"${playLicenseKey.trim()}\"")
    }

    signingConfigs {
        // One committed debug key, so every CI debug APK installs over the previous one
        // (a fresh runner would otherwise mint its own key and the update would fail with
        // INSTALL_FAILED_UPDATE_INCOMPATIBLE). Debug only and not secret; release never uses it.
        getByName("debug") {
            storeFile = file("debug.keystore")
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
        if (hasReleaseSigning) {
            create("release") {
                storeFile = file(releaseStorePath!!)
                storePassword = releaseStorePassword
                keyAlias = releaseKeyAlias
                keyPassword = releaseKeyPassword
                enableV1Signing = false
                enableV2Signing = true
                enableV3Signing = true
                enableV4Signing = false
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
            if (hasReleaseSigning) signingConfig = signingConfigs.getByName("release")
            buildConfigField("boolean", "USE_FAKE_BILLING", "false")
            // Symbol tables for libgeode.so and libprojectM-4.so go into the bundle, so Play
            // Console symbolicates native crashes and ANRs instead of warning that it cannot.
            ndk { debugSymbolLevel = "SYMBOL_TABLE" }
        }
        debug {
            isMinifyEnabled = false
            // A debug build installs ALONGSIDE a release one instead of replacing it.
            // Safe to suffix because nothing hardcodes the id: the only thing keyed on
            // it is the FileProvider authority, declared as ${applicationId}.presets,
            // which follows the suffix on its own. The geode:// deep link is scheme
            // based rather than id based, so both builds register it and Android shows
            // a chooser when both are installed - the cost of having both at once.
            applicationIdSuffix = ".debug"
            // A numbered pre-release keeps its release name (1.8.1.N) exactly.
            versionNameSuffix = if (rootProject.extra["geodeReleaseNumber"] == null) "-debug" else null
            isDebuggable = true
            // Play does not know the .debug application id, so debug builds bill against
            // FakeBillingRepository unless -Pmuzviz.fakeBilling=false asks for the real client.
            val fakeBilling = (findProperty("muzviz.fakeBilling") as String?)?.toBoolean() ?: true
            buildConfigField("boolean", "USE_FAKE_BILLING", fakeBilling.toString())
        }
    }

    // Builds libgeode.so and libprojectM-4.so from the root CMakeLists.txt.
    externalNativeBuild {
        cmake {
            path = file("../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = false
        }
    }

    bundle {
        language {
            enableSplit = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    lint {
        checkReleaseBuilds = true
        abortOnError = true
        fatal += "HardcodedText"
    }

    testOptions {
        execution = "ANDROIDX_TEST_ORCHESTRATOR"
        unitTests {
            // Give unit tests the real resource table rather than a stub, so a test
            // may read a string or an xml the code under test reaches for.
            isIncludeAndroidResources = true
            // An android.* call that nothing has mocked returns a zero/null default
            // instead of throwing "not mocked". Without this, touching an Android
            // type in passing forces an otherwise pure test onto a device.
            isReturnDefaultValues = true
        }
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }
}

val checkNativePageAlignment =
    tasks.register("checkNativePageAlignment") {
        description = "Fails if a packaged .so is not 16 KB page aligned."
        val outputs = layout.buildDirectory.dir("outputs")
        doLast {
            val archives =
                outputs
                    .get()
                    .asFile
                    .walkTopDown()
                    .filter { it.isFile && (it.extension == "apk" || it.extension == "aab") }
                    .toList()
            if (archives.isEmpty()) return@doLast
            val bad = mutableListOf<String>()
            for (archive in archives) {
                ZipFile(archive).use { zip ->
                    zip
                        .entries()
                        .asSequence()
                        .filter { it.name.endsWith(".so") }
                        .forEach { entry ->
                            val bytes = zip.getInputStream(entry).use { it.readBytes() }
                            val align = maxLoadAlignment(bytes)
                            if (align in 1 until 16384) bad += "${archive.name}!${entry.name} aligned to $align"
                        }
                }
            }
            if (bad.isNotEmpty()) {
                throw GradleException(
                    "16 KB page-size check failed — every packaged .so is built by the root CMakeLists.txt " +
                        "with GEODE_PAGE_FLAGS:\n" +
                        bad.joinToString("\n"),
                )
            }
        }
    }

fun maxLoadAlignment(bytes: ByteArray): Long {
    if (bytes.size < 0x40 || bytes[0] != 0x7F.toByte() || bytes[4].toInt() != 2) return 0
    val buf = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
    val phoff = buf.getLong(0x20)
    val phentsize = buf.getShort(0x36).toInt()
    val phnum = buf.getShort(0x38).toInt()
    var max = 0L
    for (i in 0 until phnum) {
        val at = (phoff + i.toLong() * phentsize).toInt()
        if (at + 0x38 > bytes.size) return max
        if (buf.getInt(at) == 1) max = maxOf(max, buf.getLong(at + 0x30))
    }
    return max
}

listOf("assembleRelease", "bundleRelease").forEach { name ->
    tasks.matching { it.name == name }.configureEach { finalizedBy(checkNativePageAlignment) }
}

dependencies {
    // What the removed :engine:runtime aggregator re-exported to :app via api(...).
    implementation(project(":engine:scenes"))
    implementation(project(":engine:audio-android"))

    implementation(libs.core.splashscreen)
    implementation(libs.media3.exoplayer)
    implementation(libs.media3.common)
    implementation(libs.media3.session)
    implementation(libs.media3.transformer)
    implementation(libs.media3.effect)
    implementation(libs.documentfile)
    implementation(platform(libs.compose.bom))
    implementation(libs.compose.ui)
    implementation(libs.compose.material3)
    implementation(libs.compose.material3.adaptive)
    implementation(libs.hilt.android)
    implementation(libs.hilt.navigation.compose)
    ksp(libs.hilt.compiler)
    implementation(libs.compose.material.icons.extended)
    implementation(libs.activity.compose)
    implementation(libs.glance.appwidget)
    implementation(libs.lifecycle.viewmodel.compose)
    implementation(libs.lifecycle.runtime.compose)
    implementation(libs.kotlinx.coroutines.android)
    implementation(libs.billing)
    implementation(libs.profileinstaller)

    testImplementation(libs.junit)
    testImplementation(libs.kotlinx.coroutines.test)
    testImplementation(libs.org.json)

    androidTestImplementation(libs.androidx.test.ext.junit)
    androidTestImplementation(libs.androidx.test.runner)
    androidTestImplementation(libs.androidx.test.core)
    androidTestImplementation(libs.androidx.test.rules)
    androidTestImplementation(libs.androidx.test.ext.truth)
    androidTestImplementation(libs.androidx.test.espresso.core)
    androidTestUtil(libs.androidx.test.orchestrator)
    androidTestUtil(libs.androidx.test.services)
    androidTestImplementation(platform(libs.compose.bom))
    androidTestImplementation(libs.compose.ui.test.junit4)
    // The empty ComponentActivity createComposeRule() launches; debug only, never shipped.
    debugImplementation(libs.compose.ui.test.manifest)
}
