// #############################################################
//  Flex Phone -- proyecto Android + modulo de protocolo
// #############################################################
//  Repositorios de plugins. `google()` hace falta para el Android
//  Gradle Plugin; Maven Central para Kotlin. Declararlos aqui (y no
//  en un bloque `plugins` de la raiz) hace que solo se consulten
//  cuando un modulo pide de verdad esos plugins: `:protocol` no
//  necesita ninguno de Android y por eso se construye sin ellos.
pluginManagement {
    repositories {
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("com\\.google.*")
                includeGroupByRegex("androidx.*")
            }
        }
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.PREFER_SETTINGS)
    repositories {
        google()
        mavenCentral()
    }
}

// #############################################################
//  Flex Phone -- proyecto Android + modulo de protocolo
//  ------------------------------------------------------------
//  DOS MODULOS A PROPOSITO:
//
//    :protocol  Kotlin/JVM PURO. Es la implementacion de Flex Link
//               y de FBP/1 del lado Android. No depende de Android
//               para nada, asi que se compila y se PRUEBA en
//               cualquier maquina con un JDK -- sin SDK, sin
//               emulador y sin placa. Ahi viven los vectores
//               dorados que comprueban que estos bytes son
//               EXACTAMENTE los que produce el firmware.
//
//    :app       La aplicacion Android (Compose, servicios, BLE,
//               Browser Relay). Necesita el SDK de Android.
//
//  POR QUE :app SE INCLUYE SOLO SI HAY SDK
//  ---------------------------------------
//  El plugin de Android (AGP) se descarga del repositorio Maven de
//  Google. Declararlo incondicionalmente hace que Gradle intente
//  resolverlo SIEMPRE, y en una maquina sin SDK -- o sin acceso a
//  ese repositorio -- eso rompe hasta `gradle :protocol:test`, que
//  no tiene nada que ver con Android.
//
//  Con esta guarda:
//    · sin SDK  ->  `gradle :protocol:test` funciona y verifica el
//                   protocolo entero;
//    · con SDK  ->  se incluye :app y `gradle :app:assembleDebug`
//                   construye el APK.
//
//  El SDK se detecta como lo hace el propio AGP: local.properties
//  (sdk.dir) o la variable de entorno ANDROID_HOME / ANDROID_SDK_ROOT.
// #############################################################
rootProject.name = "FlexPhone"

include(":protocol")

val sdkFromEnv = System.getenv("ANDROID_HOME") ?: System.getenv("ANDROID_SDK_ROOT")
val localProps = file("local.properties")
val sdkFromProps = if (localProps.exists()) {
    java.util.Properties().apply { localProps.inputStream().use { load(it) } }.getProperty("sdk.dir")
} else null
val androidSdk = (sdkFromProps ?: sdkFromEnv)?.takeIf { it.isNotBlank() && file(it).isDirectory }

if (androidSdk != null) {
    include(":app")
    logger.lifecycle("Flex Phone: SDK de Android en $androidSdk -> se incluye :app")
} else {
    logger.lifecycle(
        "Flex Phone: no se encontro el SDK de Android; solo se construye :protocol.\n" +
        "            Para compilar el APK, pon sdk.dir en local.properties o exporta\n" +
        "            ANDROID_HOME, y vuelve a ejecutar. Ver docs/FLEX-PHONE.md."
    )
}
