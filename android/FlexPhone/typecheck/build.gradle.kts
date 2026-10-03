// #############################################################
//  Comprobacion de tipos del pegamento Android de Flex Storage
//  ------------------------------------------------------------
//  Compila los archivos de :app que conectan Flex Cloud con Android
//  (app/src/main/kotlin/com/flexos/flexphone/flexcloud/) contra las
//  clases REALES del framework (android-all de Robolectric, API 35,
//  desde Maven Central), kotlinx-coroutines y el modulo :storage; y la
//  pantalla Compose (ui/screens/FlexCloud.kt con Common.kt) contra
//  Compose Multiplatform de escritorio 1.7.0, que publica las MISMAS
//  APIs androidx.compose.* (material3 1.3, la version del BOM de la app)
//  en Maven Central.
//  Existe porque en una maquina sin el SDK de Android :app no se
//  construye: asi, un error de nombres o de tipos en ese pegamento
//  aparece aqui y no en el Android Studio de quien lo compile.
//
//    gradle -PflexTypecheck :typecheck:compileKotlin
//
//  No produce nada que vaya al APK. Lo exclusivo de Android que no esta
//  en esas bibliotecas (R, MainActivity, LocalContext, NavController y
//  la cabecera FlexTopBar) se resuelve con dobles minimos (stubs/).
// #############################################################
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    kotlin("jvm") version "2.0.21"
    id("org.jetbrains.kotlin.plugin.compose") version "2.0.21"
}
dependencies {
    compileOnly("org.robolectric:android-all:15-robolectric-12650502")
    compileOnly("org.jetbrains.kotlinx:kotlinx-coroutines-core-jvm:1.8.1")
    compileOnly(project(":storage"))
    compileOnly(project(":protocol"))
    val cmp = "1.7.0"
    compileOnly("org.jetbrains.compose.runtime:runtime-desktop:$cmp")
    compileOnly("org.jetbrains.compose.ui:ui-desktop:$cmp")
    compileOnly("org.jetbrains.compose.foundation:foundation-desktop:$cmp")
    compileOnly("org.jetbrains.compose.material3:material3-desktop:$cmp")
}
kotlin {
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_17)
        allWarningsAsErrors.set(false)
    }
    sourceSets["main"].kotlin.apply {
        srcDir("../app/src/main/kotlin")
        srcDir("stubs")
        include("com/flexos/flexphone/flexcloud/**")
        include("com/flexos/flexphone/ui/screens/FlexCloud.kt")
        include("com/flexos/flexphone/ui/screens/Common.kt")
        include("com/flexos/flexphone/domain/FlexPhoneState.kt")
        include("com/flexos/flexphone/domain/Settings.kt")
        include("com/flexos/flexphone/typecheck_stubs/**")
    }
}
java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}
