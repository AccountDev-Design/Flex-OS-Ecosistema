package com.flexos.flexphone.cloud

import java.io.File
import java.net.InetAddress

/**
 * Servidor de Flex Cloud del telefono para pruebas de extremo a extremo.
 *
 *   java -cp <classpath de pruebas> com.flexos.flexphone.cloud.DevServerKt \
 *        <carpeta> <idP4> <claveHex64> [cuotaBytes] [libreBytes]
 *
 * Escucha en 127.0.0.1 (puerto libre), ya emparejado con <idP4> y esa clave, y
 * escribe {"port":N} en una linea cuando esta listo. Se para al cerrarse su
 * entrada estandar. Lo usa tests/host/phone_e2e.sh: el gestor de Flex Cloud del
 * P4 (FlexOS_Cloud.cpp, compilado en el PC) contra ESTE servidor de verdad.
 */
fun main(args: Array<String>) {
    require(args.size >= 3) { "uso: <carpeta> <idP4> <claveHex64> [cuotaBytes] [libreBytes]" }
    val dir = File(args[0])
    val key = StorageCrypto.unhex(args[2], 32) ?: error("clave no valida")
    val quota = args.getOrNull(3)?.toLong() ?: (5L shl 30)
    val free = args.getOrNull(4)?.toLong()
    val os = if (free != null) ObjectStore(File(dir, "data"), { free }) else ObjectStore(File(dir, "data"))
    val store = CloudStore(os, File(dir, "meta"), CloudConfig(quotaBytes = quota, deviceMarginBytes = 0), log = { System.err.println("[telefono] $it") })
    store.recover()
    val repo = MemoryPairingRepo(P4Pairing(args[1], "Flex OS (pruebas)", key, "127.0.0.1:8080", System.currentTimeMillis()))
    val server = CloudServer(store, SessionManager(), repo, { PhoneInfo("a55-e2e", "Galaxy A55 5G", "SM-A556B") }, log = { System.err.println("[telefono] $it") })
    val port = server.start(InetAddress.getLoopbackAddress(), 0, fallbackAny = true)
    println("{\"port\":$port}")
    System.out.flush()
    // Vive hasta que se cierre la entrada estandar (la prueba termina).
    while (System.`in`.read() >= 0) { /* nada */ }
    server.stop()
}
