// Raiz del proyecto Flex Phone.
//
// VERSIONES FIJADAS Y COMPATIBLES ENTRE SI. No se usa ningun rango ni
// "latest": una combinacion incoherente de AGP / Kotlin / Compose es
// de los fallos mas caros de diagnosticar, porque el error sale en el
// compilador de Compose y no dice cual de las tres piezas sobra.
//
//   Gradle 8.14.x  ·  AGP 8.5.2  ·  Kotlin 2.0.21
//   Compose BOM 2024.09.03
//   Compose Compiler: lo aporta el propio plugin de Kotlin 2.0
//   (org.jetbrains.kotlin.plugin.compose), que sustituye al antiguo
//   composeOptions.kotlinCompilerExtensionVersion.
//
// Los plugins de Android NO se declaran aqui: se declaran en
// app/build.gradle.kts, que solo se configura si hay SDK (ver
// settings.gradle.kts). Asi `gradle :protocol:test` no depende del
// repositorio Maven de Google.
