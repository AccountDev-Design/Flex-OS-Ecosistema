package com.flexos.flexphone.protocol

import java.security.MessageDigest
import java.security.SecureRandom
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec

/**
 * FLEX AUTH -- lado Android del emparejamiento y la sesion.
 *
 * ESPEJO EXACTO de `FlexOS_FlexAuth.cpp` del firmware. El P4 lleva
 * SHA-256 y HMAC escritos a mano porque ahi no hay biblioteca
 * garantizada; aqui se usan los de la plataforma, que son los
 * mismos algoritmos. Lo que tiene que coincidir byte a byte es lo
 * que se mete DENTRO del HMAC, y de eso se encargan las etiquetas y
 * el orden de los campos de este fichero.
 *
 * Si alguien cambia la derivacion en un solo lado, el telefono y el
 * reloj derivan claves distintas: el emparejamiento falla y ninguno
 * de los dos puede decir por que. Por eso `FlexAuthTest` fija los
 * MISMOS vectores dorados que `tests/host/test_flexauth.cpp`.
 *
 * QUE NO HACE, y hay que decirlo claro: no cifra la carga. Las
 * notificaciones viajan en claro por la red local. Esto impide que
 * un dispositivo NO emparejado abra sesion; no protege frente a
 * quien ya este escuchando la misma red.
 *
 * Kotlin/JVM puro: sin `android.*`. Se prueba con un JDK, sin
 * emulador y sin placa.
 */
object FlexAuth {

    const val KEY_SIZE = 32
    const val NONCE_SIZE = 16
    const val SALT_SIZE = 16
    const val PROOF_SIZE = 32
    const val ID_MAX = 32

    /** Papel de cada extremo en el reto-respuesta. */
    const val ROLE_PHONE = 0
    const val ROLE_HOST = 1

    // Texto de dominio. Cambiarlo invalida todos los vinculos, que es
    // exactamente lo que debe pasar si el esquema cambia.
    private val PAIR_TAG = "flexphone-pair-v2".toByteArray(Charsets.US_ASCII)
    private val PEER_TAG = "flexphone-peer-v2".toByteArray(Charsets.US_ASCII)
    private val HOST_TAG = "flexphone-host-v2".toByteArray(Charsets.US_ASCII)

    private val rng = SecureRandom()

    fun hmacSha256(key: ByteArray, msg: ByteArray): ByteArray {
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(key, "HmacSHA256"))
        return mac.doFinal(msg)
    }

    fun sha256(data: ByteArray): ByteArray =
        MessageDigest.getInstance("SHA-256").digest(data)

    /**
     * Comparacion en TIEMPO CONSTANTE. Un `contentEquals` sale en el
     * primer byte distinto, y ese tiempo se puede medir para adivinar
     * un MAC byte a byte.
     */
    fun equalsConstantTime(a: ByteArray, b: ByteArray): Boolean {
        if (a.size != b.size) return false
        var diff = 0
        for (i in a.indices) diff = diff or (a[i].toInt() xor b[i].toInt())
        return diff == 0
    }

    /** Longitud acotada de un id: nunca se recorre mas alla de ID_MAX. */
    private fun idBytes(s: String?): ByteArray {
        val raw = (s ?: "").toByteArray(Charsets.UTF_8)
        return if (raw.size <= ID_MAX) raw else raw.copyOf(ID_MAX)
    }

    /**
     * clave = HMAC( codigo , "flexphone-pair-v2" || sal || idFlexOS || idTelefono )
     *
     * Por la red solo viajan la sal y los identificadores, que son
     * publicos. El codigo de 6 digitos lo lee el usuario en Flex OS y
     * lo teclea aqui: nunca se transmite.
     *
     * Los ids entran con su LONGITUD delante. Sin eso, ("ab","c") y
     * ("a","bc") darian la misma clave, y dos emparejamientos
     * distintos podrian colisionar.
     */
    fun deriveKey(code: String, salt: ByteArray, flexosId: String, phoneId: String): ByteArray {
        require(salt.size == SALT_SIZE) { "sal de tamano incorrecto" }
        val a = idBytes(flexosId)
        val b = idBytes(phoneId)
        val msg = ByteArray(PAIR_TAG.size + SALT_SIZE + 1 + a.size + 1 + b.size)
        var at = 0
        PAIR_TAG.copyInto(msg, at); at += PAIR_TAG.size
        salt.copyInto(msg, at); at += SALT_SIZE
        msg[at++] = a.size.toByte()
        a.copyInto(msg, at); at += a.size
        msg[at++] = b.size.toByte()
        b.copyInto(msg, at)
        return hmacSha256(code.toByteArray(Charsets.UTF_8), msg)
    }

    /**
     * pruebaTelefono = HMAC(clave, "flexphone-peer-v2" || reto || sesion)
     * pruebaFlexOS   = HMAC(clave, "flexphone-host-v2" || reto || sesion)
     *
     * Las dos etiquetas son DISTINTAS a proposito: reenviar la prueba
     * ajena no sirve de nada. Sin eso, un equipo cualquiera de la red
     * podria hacerse pasar por Flex OS devolviendo la prueba que
     * acaba de escuchar, y quedarse con todas las notificaciones.
     */
    fun proof(key: ByteArray, role: Int, nonce: ByteArray, session: Int): ByteArray {
        require(nonce.size == NONCE_SIZE) { "reto de tamano incorrecto" }
        val tag = if (role == ROLE_HOST) HOST_TAG else PEER_TAG
        val msg = ByteArray(tag.size + NONCE_SIZE + 2)
        var at = 0
        tag.copyInto(msg, at); at += tag.size
        nonce.copyInto(msg, at); at += NONCE_SIZE
        msg[at++] = (session and 0xFF).toByte()
        msg[at] = ((session ushr 8) and 0xFF).toByte()
        return hmacSha256(key, msg)
    }

    fun verify(key: ByteArray, role: Int, nonce: ByteArray, session: Int, got: ByteArray): Boolean =
        equalsConstantTime(proof(key, role, nonce, session), got)

    /** Bytes de verdad aleatorios (SecureRandom), no `Random`. */
    fun randomBytes(n: Int): ByteArray = ByteArray(n).also { rng.nextBytes(it) }

    /**
     * Comprueba que lo tecleado es un codigo de emparejamiento
     * plausible ANTES de derivar nada. Seis digitos, ni mas ni menos:
     * derivar con basura solo produce un fallo mas adelante y mas
     * dificil de entender.
     */
    fun isValidCode(code: String): Boolean =
        code.length == 6 && code.all { it in '0'..'9' }
}
