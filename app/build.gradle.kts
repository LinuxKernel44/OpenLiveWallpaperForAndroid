import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

// Release signing: read from KEYSTORE_* env vars or ~/.keystores/openlivewallpaper/keystore.properties (never in the repo).
val keystoreProps = Properties().apply {
    val f = File(System.getProperty("user.home"), ".keystores/openlivewallpaper/keystore.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}
fun prop(env: String, key: String): String? = System.getenv(env) ?: keystoreProps.getProperty(key)

android {
    namespace = "com.openlivewallpaper.app"
    compileSdk = 37
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "com.openlivewallpaper.app"
        minSdk = 29
        targetSdk = 37
        versionCode = 1
        versionName = "1.0.0"
        ndk { abiFilters += listOf("arm64-v8a") }
        externalNativeBuild { cmake { arguments += listOf("-DANDROID_STL=c++_static"); cppFlags += "-std=c++17" } }
    }

    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" } }

    signingConfigs {
        val sf = prop("KEYSTORE_FILE", "storeFile")
        if (sf != null) {
            create("release") {
                storeFile = file(sf)
                storePassword = prop("KEYSTORE_PASSWORD", "storePassword")
                keyAlias = prop("KEY_ALIAS", "keyAlias")
                keyPassword = prop("KEY_PASSWORD", "keyPassword")
            }
        }
    }

    buildTypes {
        debug { signingConfigs.findByName("release")?.let { signingConfig = it } }
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfigs.findByName("release")?.let { signingConfig = it }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    buildFeatures { compose = true; buildConfig = true }
    androidResources { noCompress += listOf("jpg", "spv", "bin") }
    packaging { jniLibs { useLegacyPackaging = false } }
}

kotlin { compilerOptions { jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17) } }

dependencies {
    implementation(platform(libs.compose.bom))
    implementation(libs.compose.ui)
    implementation(libs.compose.foundation)
    implementation(libs.compose.material3)
    implementation(libs.activity.compose)
    implementation(libs.coroutines)
}
