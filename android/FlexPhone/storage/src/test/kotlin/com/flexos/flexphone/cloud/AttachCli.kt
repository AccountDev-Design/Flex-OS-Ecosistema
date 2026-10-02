package com.flexos.flexphone.cloud

import java.io.File

/**
 * El emparejamiento de la app (AttachClient) desde la linea de ordenes, para
 * pruebas de extremo a extremo contra el servidor web REAL del P4 (el mismo
 * FlexOS_MediaWeb + FlexOS_StorageCore, compilado en el PC: flexweb_host).
 *
 *   java -cp <classpath de pruebas> com.flexos.flexphone.cloud.AttachCliKt \
 *        <ip:puerto del P4> <oferta> <idTelefono> <puertoFlexCloud> <archivoEmparejamiento>
 *
 * Escribe una linea JSON por cosa que pasa: {"sas":"123456","p4":"..."} cuando
 * hay que comparar el codigo y {"paired":true,...} o {"paired":false,...} al
 * final. El emparejamiento se guarda en <archivoEmparejamiento> (en la app,
 * envuelto con el Keystore) y, si ya existia, se usa para demostrar que este
 * telefono ya estaba emparejado.
 */
fun main(args: Array<String>) {
    require(args.size >= 5) { "uso: <ip:puerto> <oferta> <idTelefono> <puertoFlexCloud> <archivoEmparejamiento>" }
    val file = File(args[4])
    val repo = object : PairingRepo {
        override fun load(): P4Pairing? {
            if (!file.exists()) return null
            val j = Json.parseObject(file.readText())
            val key = StorageCrypto.unhex(j["key"] as? String, 32) ?: return null
            return P4Pairing(j["p4Id"] as String, j["p4Name"] as String, key, j["host"] as String, (j["pairedAt"]).jsonLong() ?: 0L)
        }
        override fun save(p: P4Pairing) {
            file.writeText(Json.write(linkedMapOf(
                "p4Id" to p.p4Id, "p4Name" to p.p4Name, "key" to StorageCrypto.hex(p.key), "host" to p.host, "pairedAt" to p.pairedAt,
            )))
        }
        override fun clear() { file.delete() }
    }
    val client = AttachClient(PhoneInfo(args[2], "Galaxy A55 de pruebas", "SM-A556B"), args[3].toInt(), repo)
    val r = client.attach(args[0], args[1], onSas = { sas, p4 ->
        println(Json.write(linkedMapOf("sas" to sas, "p4" to p4)))
        System.out.flush()
    })
    when (r) {
        is AttachClient.Result.Paired -> println(Json.write(linkedMapOf("paired" to true, "p4Id" to r.p4Id, "p4Name" to r.p4Name)))
        is AttachClient.Result.Failed -> println(Json.write(linkedMapOf("paired" to false, "error" to r.message)))
    }
    System.out.flush()
}
