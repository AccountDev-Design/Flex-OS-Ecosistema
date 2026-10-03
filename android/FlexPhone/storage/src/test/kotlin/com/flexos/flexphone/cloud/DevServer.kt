package com.flexos.flexphone.cloud

import java.io.File
import java.net.InetAddress

/**
 * Servidor de Flex Cloud del telefono para pruebas de extremo a extremo.
 *
 *   java -cp <classpath de pruebas> com.flexos.flexphone.cloud.DevServerKt \
 *        <carpeta> <idP4|-> <claveHex64|-> [cuotaBytes] [libreBytes]
 *
 * Escucha en 127.0.0.1 (puerto libre) y escribe {"port":N} en una linea cuando
 * esta listo. Con <idP4> y una clave arranca YA emparejado (tests/host/
 * phone_e2e.sh: el gestor de Flex Cloud del P4 contra ESTE servidor). Con "-"
 * arranca sin emparejar y se comporta como la app: por la entrada estandar
 *
 *   attach <ip:puerto del P4> <oferta>
 *
 * empareja con AttachClient sobre el MISMO almacen de emparejamiento que usa su
 * servidor (como Flex Phone) y escribe {"sas":...} y luego {"paired":...}. Lo
 * usa tests/web/storage_e2e.test.js. Se para al cerrarse su entrada estandar.
 */
fun main(args: Array<String>) {
    require(args.size >= 3) { "uso: <carpeta> <idP4|-> <claveHex64|-> [cuotaBytes] [libreBytes]" }
    val dir = File(args[0])
    val quota = args.getOrNull(3)?.toLong() ?: (5L shl 30)
    val free = args.getOrNull(4)?.toLong()
    val os = if (free != null) ObjectStore(File(dir, "data"), { free }) else ObjectStore(File(dir, "data"))
    val store = CloudStore(os, File(dir, "meta"), CloudConfig(quotaBytes = quota, deviceMarginBytes = 0), log = { System.err.println("[telefono] $it") })
    store.recover()
    val repo = if (args[2] == "-") MemoryPairingRepo() else {
        val key = StorageCrypto.unhex(args[2], 32) ?: error("clave no valida")
        MemoryPairingRepo(P4Pairing(args[1], "Flex OS (pruebas)", key, "127.0.0.1:8080", System.currentTimeMillis()))
    }
    val phone = PhoneInfo("a55-e2e", "Galaxy A55 5G", "SM-A556B")
    val server = CloudServer(store, SessionManager(), repo, { phone }, log = { System.err.println("[telefono] $it") })
    val port = server.start(InetAddress.getLoopbackAddress(), 0, fallbackAny = true)
    println("{\"port\":$port}")
    System.out.flush()
    // Vive hasta que se cierre la entrada estandar (la prueba termina).
    val input = System.`in`.bufferedReader()
    while (true) {
        val line = input.readLine() ?: break
        val p = line.trim().split(' ')
        if (p.size == 3 && p[0] == "attach") {
            // Como la app: en otro hilo, para que el servidor siga atendiendo.
            Thread {
                val r = AttachClient(phone, port, repo).attach(p[1], p[2], onSas = { sas, p4 ->
                    synchronized(System.out) { println(Json.write(linkedMapOf("sas" to sas, "p4" to p4))); System.out.flush() }
                })
                val out = when (r) {
                    is AttachClient.Result.Paired -> linkedMapOf<String, Any?>("paired" to true, "p4Id" to r.p4Id, "p4Name" to r.p4Name)
                    is AttachClient.Result.Failed -> linkedMapOf<String, Any?>("paired" to false, "error" to r.message)
                }
                synchronized(System.out) { println(Json.write(out)); System.out.flush() }
            }.start()
        }
    }
    server.stop()
}
