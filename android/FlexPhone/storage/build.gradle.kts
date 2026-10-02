// Modulo de Flex Storage del telefono: el SERVIDOR de Flex Cloud que comparte
// una carpeta administrada del movil con Flex OS Ultra. Kotlin/JVM puro, sin
// una sola dependencia (ni de Android ni de terceros): se compila y se prueba
// en el PC, igual que :protocol. La app Android (:app) solo le pone delante
// un servicio, una carpeta y el Keystore.
//
// Bytecode 17 (lo que exige Android), con el JDK que haya en la maquina.
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    kotlin("jvm") version "2.0.21"
}
dependencies {
    testImplementation(kotlin("test"))
}
kotlin {
    compilerOptions { jvmTarget.set(JvmTarget.JVM_17) }
}
java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}
tasks.withType<JavaCompile>().configureEach { options.release.set(17) }
tasks.test {
    useJUnitPlatform()
    testLogging {
        events("passed", "failed", "skipped")
        showStandardStreams = true
    }
}
// Classpath para arrancar el servidor de pruebas desde fuera de Gradle
// (tests/host/phone_e2e.sh lo usa para enfrentar el gestor del P4 a este
// servidor de verdad).
tasks.register("printTestClasspath") {
    dependsOn("testClasses")
    doLast {
        val cp = sourceSets["test"].runtimeClasspath.files.joinToString(File.pathSeparator)
        println("CLASSPATH=$cp")
    }
}
