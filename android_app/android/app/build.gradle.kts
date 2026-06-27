plugins {
    id("com.android.application")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

android {
    namespace = "com.audiostream.audiostream_app"
    compileSdk = flutter.compileSdkVersion
    ndkVersion = flutter.ndkVersion

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // ── Release signing ───────────────────────────────────────────────────────
    // In CI: set KEYSTORE_FILE, KEYSTORE_PASSWORD, KEY_ALIAS, KEY_PASSWORD
    // environment variables (via GitHub Secrets).  Locally it falls back to
    // the debug key so `flutter run --release` still works.
    signingConfigs {
        val keystoreFile = System.getenv("KEYSTORE_FILE")?.let { file(it) }
        val keystorePassword = System.getenv("KEYSTORE_PASSWORD")
        val keyAlias = System.getenv("KEY_ALIAS")
        val keyPassword = System.getenv("KEY_PASSWORD")

        if (keystoreFile != null && keystoreFile.exists() &&
            !keystorePassword.isNullOrEmpty() &&
            !keyAlias.isNullOrEmpty() &&
            !keyPassword.isNullOrEmpty()) {

            create("release") {
                storeFile = keystoreFile
                storePassword = keystorePassword
                this.keyAlias = keyAlias
                this.keyPassword = keyPassword
            }
        }
    }

    defaultConfig {
        applicationId = "com.audiostream.audiostream_app"
        // For more information, see: https://flutter.dev/to/review-gradle-config.
        minSdk = 26
        targetSdk = flutter.targetSdkVersion
        versionCode = flutter.versionCode
        versionName = flutter.versionName
        externalNativeBuild {
            cmake {
                arguments("-DANDROID_STL=c++_shared")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }

    buildTypes {
        release {
            // Use the release signing config when available (CI), otherwise
            // fall back to debug so local builds still work.
            val releaseConfig = signingConfigs.findByName("release")
            signingConfig = releaseConfig ?: signingConfigs.getByName("debug")
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

flutter {
    source = "../.."
}
