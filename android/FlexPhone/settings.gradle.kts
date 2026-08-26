// #############################################################
//  Flex Phone -- proyecto Android + modulo de protocolo
//  ------------------------------------------------------------
//  DOS MODULOS A PROPOSITO:
//
//    :protocol  Kotlin/JVM PURO. Es la implementacion de Flex Link
//               del lado Android. No depende de Android para nada,
//               asi que se compila y se PRUEBA en cualquier maquina
//               con un JDK -- sin SDK, sin emulador y sin placa.
//               Ahi viven los vectores dorados que comprueban que
//               estos bytes son EXACTAMENTE los mismos que produce
//               FlexOS_FlexLink.cpp en el ESP32.
//
//    :app       La aplicacion Android (Compose, servicios, BLE,
//               Browser Relay). Necesita el SDK de Android.
//
//  La separacion no es cosmetica: el protocolo es la parte que MAS
//  se puede equivocar en silencio (un byte movido y el P4 descarta
//  la trama sin decir por que), y es justo la que se puede
//  verificar de verdad sin hardware.
// #############################################################
rootProject.name = "FlexPhone"
include(":protocol")
include(":app")
