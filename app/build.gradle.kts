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
    // r28 is the first NDK that aligns shared objects to 16 KB pages by default,
    // and the version the committed libprojectM-4.so was built with.
    ndkVersion = "28.0.13004108"

    defaultConfig {
        applicationId = "dev.geode"
        minSdk = 26
        targetSdk = 36
        versionCode = 31
        versionName = "1.7.0"
        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                // The bridge is plain C over the engine's C API; the engine
                // links libc++ statically, so nothing here needs an STL and
                // none gets packaged.
                arguments += listOf("-DANDROID_STL=none")
            }
        }

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    signingConfigs {
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
            versionNameSuffix = "-debug"
            isDebuggable = true
        }
    }

    // Builds libmilkdropjni.so from src/main/cpp against the prebuilt
    // libprojectM-4.so in src/main/jniLibs; see src/main/cpp/CMakeLists.txt.
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
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
                    "16 KB page-size check failed — libprojectM-4.so is rebuilt through " +
                        ".github/workflows/native-libs.yml, libmilkdropjni.so by src/main/cpp/CMakeLists.txt:\n" +
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
    implementation(project(":engine:runtime"))

    implementation(libs.core.splashscreen)
    implementation(libs.media3.exoplayer)
    implementation(libs.media3.common)
    implementation(libs.media3.session)
    implementation(libs.media3.transformer)
    implementation(libs.media3.effect)
    implementation(libs.documentfile)
    implementation(libs.jtransforms)
    implementation(platform(libs.compose.bom))
    implementation(libs.compose.ui)
    implementation(libs.compose.material3)
    implementation(libs.compose.material3.adaptive)
    implementation(libs.hilt.android)
    implementation(libs.hilt.navigation.compose)
    ksp(libs.hilt.compiler)
    implementation(libs.compose.material.icons.extended)
    implementation(libs.activity.compose)
    implementation(libs.lifecycle.viewmodel.compose)
    implementation(libs.lifecycle.runtime.compose)
    implementation(libs.kotlinx.coroutines.android)

    testImplementation(libs.junit)
    testImplementation(libs.kotlinx.coroutines.test)

    androidTestImplementation(libs.androidx.test.ext.junit)
    androidTestImplementation(libs.androidx.test.runner)
}
