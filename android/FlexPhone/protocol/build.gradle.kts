// Modulo de protocolo: Kotlin/JVM puro, sin una sola dependencia de
// Android. Es lo que permite ejecutar sus pruebas en el PC.
//
// SE FIJA EL BYTECODE A 17, NO EL JDK. Android exige clases de nivel
// 17, pero exigir ademas un JDK 17 instalado rompe la compilacion en
// cualquier maquina que tenga uno mas nuevo (aqui hay un JDK 21). Con
// `jvmTarget`/`release` se compila con el JDK que haya y se EMITE
// bytecode 17, que es lo que de verdad importa para que el modulo
// valga tal cual dentro de la app Android.
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    kotlin("jvm") version "2.0.21"
}
repositories { mavenCentral() }
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
