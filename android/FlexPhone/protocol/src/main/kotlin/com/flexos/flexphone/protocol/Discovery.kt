package com.flexos.flexphone.protocol

/**
 * DESCUBRIMIENTO: los bytes que van al aire.
 *
 * El espejo exacto de `FlexOS_FlexPhone_Discovery.h`. Vive en
 * `:protocol` y no en la app por el mismo motivo que el resto del
 * protocolo: asi se puede probar sin Android, contra los MISMOS
 * vectores que la bateria de C++ (`tests/host/test_flexphone_discovery.cpp`).
 *
 * POR QUE HAY DOS SENTIDOS, y no por simetria: por fiabilidad.
 *
 *  · Que el reloj emita y este telefono RECIBA una difusion es la
 *    direccion FRAGIL. El controlador Wi-Fi de Android descarta las
 *    tramas de difusion que no van dirigidas a su MAC para ahorrar
 *    bateria; el cerrojo de multidifusion ayuda, pero con la pantalla
 *    apagada no lo cura del todo.
 *
 *  · Al reves es SOLIDO: emitir desde el telefono no lo filtra nadie,
 *    y la respuesta del reloj vuelve en unidifusion, que tampoco.
 *    Ademas, al recibir la pregunta el reloj ya sabe la direccion (el
 *    origen del paquete) y el puerto (la carga), asi que puede
 *    conectar sin haber descubierto nada por su cuenta.
 *
 * LOS CUATRO PAQUETES
 *
 *   reloj    -> difusion : "FLEXPHONE?" u8 ver
 *   telefono -> unidifus.: "FLEXPHONE!" u8 ver  u16 puertoTcp  str nombre
 *   telefono -> difusion : "FLEXOS?"    u8 ver  u16 puertoTcp  str nombre
 *   reloj    -> unidifus.: "FLEXOS!"    u8 ver  u8 flags       str id  str nombre
 */
object Discovery {
    /** Los MISMOS numeros que FlexOS_FlexPhone_Discovery.h. */
    const val TCP_PORT = 47820
    const val UDP_PORT = 47821

    /** El reloj pregunta / el telefono contesta. */
    val PROBE: ByteArray = "FLEXPHONE?".toByteArray(Charsets.US_ASCII)
    val REPLY: ByteArray = "FLEXPHONE!".toByteArray(Charsets.US_ASCII)

    /** El telefono pregunta / el reloj contesta. */
    val ASK: ByteArray = "FLEXOS?".toByteArray(Charsets.US_ASCII)
    val ANS: ByteArray = "FLEXOS!".toByteArray(Charsets.US_ASCII)

    /** El reloj esta AHORA ensenando un codigo de emparejamiento. */
    const val FLAG_PAIRING = 0x01
    /** Tope de un `str` en estos paquetes. */
    const val NAME_MAX = 31

    // -----------------------------------------------------------
    //  Reconocer
    // -----------------------------------------------------------
    // Cada comprobacion exige ademas los campos FIJOS que vienen
    // detras de la marca. Reconocer por la marca sola dejaria pasar
    // un paquete cortado justo despues, y el siguiente paso leeria
    // campos que no estan.
    fun isProbe(b: ByteArray, n: Int = b.size): Boolean = starts(b, n, PROBE, 1)
    fun isReply(b: ByteArray, n: Int = b.size): Boolean = starts(b, n, REPLY, 3)
    fun isAsk(b: ByteArray, n: Int = b.size): Boolean = starts(b, n, ASK, 3)
    fun isAnswer(b: ByteArray, n: Int = b.size): Boolean = starts(b, n, ANS, 2)

    private fun starts(b: ByteArray, n: Int, mark: ByteArray, extra: Int): Boolean {
        if (n < mark.size + extra || b.size < mark.size) return false
        for (i in mark.indices) if (b[i] != mark[i]) return false
        return true
    }

    // -----------------------------------------------------------
    //  Construir
    // -----------------------------------------------------------
    /** "FLEXPHONE?" u8 ver -- la pregunta del reloj. No la manda el telefono. */
    fun buildProbe(ver: Int = FlexLink.VERSION): ByteArray =
        PayloadWriter(16).bytes(PROBE).u8(ver).build()

    /** "FLEXPHONE!" u8 ver u16 puerto str nombre -- la respuesta de este telefono. */
    fun buildReply(tcpPort: Int, name: String?, ver: Int = FlexLink.VERSION): ByteArray =
        PayloadWriter(64).bytes(REPLY).u8(ver).u16(tcpPort).str(name, NAME_MAX).build()

    /** "FLEXOS?" u8 ver u16 puerto str nombre -- la pregunta de este telefono. */
    fun buildAsk(tcpPort: Int, name: String?, ver: Int = FlexLink.VERSION): ByteArray =
        PayloadWriter(64).bytes(ASK).u8(ver).u16(tcpPort).str(name, NAME_MAX).build()

    /** "FLEXOS!" u8 ver u8 flags str id str nombre -- la respuesta del reloj. */
    fun buildAnswer(flags: Int, id: String?, name: String?, ver: Int = FlexLink.VERSION): ByteArray =
        PayloadWriter(96).bytes(ANS).u8(ver).u8(flags).str(id, NAME_MAX).str(name, NAME_MAX).build()

    // -----------------------------------------------------------
    //  Leer
    // -----------------------------------------------------------
    /** La pregunta de un reloj. Null si el paquete no es una. */
    data class Probe(val ver: Int)

    fun parseProbe(b: ByteArray, n: Int = b.size): Probe? {
        if (!isProbe(b, n)) return null
        return Probe(b[PROBE.size].toInt() and 0xFF)
    }

    /** La respuesta de un telefono (la lee el reloj; aqui, solo las pruebas). */
    data class Reply(val ver: Int, val port: Int, val name: String)

    fun parseReply(b: ByteArray, n: Int = b.size): Reply? {
        if (!isReply(b, n)) return null
        val r = PayloadReader(b, n)
        r.bytes(REPLY.size)
        val ver = r.u8()
        val port = r.u16()
        // El nombre es OPCIONAL: un paquete sin el sigue siendo valido,
        // y uno que miente sobre su longitud no rompe nada -- el lector
        // marca overflow y aqui se queda vacio.
        val name = r.str()
        return Reply(ver, port, if (r.ok) name else "")
    }

    /** La respuesta de un reloj: lo que llena la lista de dispositivos. */
    data class Answer(val ver: Int, val flags: Int, val id: String, val name: String) {
        val pairing: Boolean get() = (flags and FLAG_PAIRING) != 0
    }

    fun parseAnswer(b: ByteArray, n: Int = b.size): Answer? {
        if (!isAnswer(b, n)) return null
        val r = PayloadReader(b, n)
        r.bytes(ANS.size)
        val ver = r.u8()
        val flags = r.u8()
        val id = r.str()
        val name = r.str()
        // Sin identificador no hay nada que ensenar: dos relojes sin
        // nombre se confundirian entre si en la lista.
        if (id.isEmpty()) return null
        return Answer(ver, flags, id, if (name.isEmpty()) id else name)
    }

    /** La pregunta de un telefono (la lee el reloj; aqui, solo las pruebas). */
    data class Ask(val ver: Int, val port: Int, val name: String)

    fun parseAsk(b: ByteArray, n: Int = b.size): Ask? {
        if (!isAsk(b, n)) return null
        val r = PayloadReader(b, n)
        r.bytes(ASK.size)
        val ver = r.u8()
        val port = r.u16()
        val name = r.str()
        return Ask(ver, port, if (r.ok) name else "")
    }
}
