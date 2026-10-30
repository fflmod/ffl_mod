plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "io.github.fflmod.fflmod"
    compileSdk = 34

    defaultConfig {
        applicationId = "io.github.fflmod.fflmod"
        minSdk = 24
        targetSdk = 34
        versionCode = 10
        versionName = "1.0"
    }

    // 公開用の署名。鍵は GitHub の Secrets から release ワークフローが渡す（リポジトリには入れない）。
    // 鍵が無いとき（手元や build ワークフロー）は debug の署名（その場限りの鍵）になる。
    val ksFile = System.getenv("FFLMOD_KEYSTORE")
    signingConfigs {
        if (ksFile != null) {
            create("release") {
                storeFile = file(ksFile)
                storeType = "PKCS12"
                storePassword = System.getenv("FFLMOD_KEYSTORE_PASSWORD")
                keyAlias = "fflmod-manager"
                keyPassword = System.getenv("FFLMOD_KEYSTORE_PASSWORD")
            }
        }
    }
    buildTypes {
        getByName("release") {
            isMinifyEnabled = false
            if (ksFile != null) signingConfig = signingConfigs.getByName("release")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    // APK の署名（Google の apksig）
    implementation("com.android.tools.build:apksig:8.5.2")
}
