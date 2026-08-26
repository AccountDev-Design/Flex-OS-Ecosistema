package com.flexos.flexphone.protocol

/**
 * FLEX LINK -- lado Android del protocolo de enlace con Flex OS.
 *
 * Este fichero es el ESPEJO EXACTO de `FlexOS_FlexLink.cpp` del
 * firmware. Cada constante, cada desplazamiento y el polinomio del
 * CRC estan puestos para producir los MISMOS bytes; si aqui se
 * mueve un campo, el P4 descarta la trama por CRC y no hay ningun
 * mensaje de error que lo explique. Por eso existe
 * `FlexLinkGoldenTest`: fija vectores dorados byte a byte.
 *
 * Kotlin/JVM PURO a proposito: sin `android.*`, sin `Context`, sin
 * nada del SDK. Asi el protocolo -- que es la parte mas facil de
 * romper en silencio -- se compila y se prueba en cualquier maquina
 * con un JDK, sin emulador y sin placa.
 */
object FlexLink {

    // ---- Version ------------------------------------------------
    const val VERSION = 1
    const val VERSION_MIN = 1

    // ---- Limites (identicos a FlexOS_FlexLink.h) ----------------
    const val HDR_SIZE = 18
    const val MAX_FRAME = 244
    const val MAX_PAYLOAD = MAX_FRAME - HDR_SIZE      // 226
    const val MAX_MESSAGE = 2048
    const val MAX_FRAGS = 16
    const val MIN_MTU = HDR_SIZE + 8

    private const val MAGIC0 = 0xF1.toByte()
    private const val MAGIC1 = 0x58.toByte()          // 'X'

    // ---- Tipos de mensaje ---------------------------------------
    // Los numeros van AL AIRE: nunca se reordenan.
    const val T_HELLO = 0x01
    const val T_WELCOME = 0x02
    const val T_PING = 0x03
    const val T_PONG = 0x04
    const val T_BYE = 0x05
    const val T_ACK = 0x06
    const val T_ERR = 0x07

    const val T_PAIR_REQ = 0x10
    const val T_PAIR_CODE = 0x11
    const val T_PAIR_CONFIRM = 0x12
    const val T_UNPAIR = 0x13

    const val T_NOTIF_ADD = 0x20
    const val T_NOTIF_UPDATE = 0x21
    const val T_NOTIF_REMOVE = 0x22
    const val T_NOTIF_CLEAR = 0x23

    const val T_REPLY_REQ = 0x30
    const val T_REPLY_RESULT = 0x31
    const val T_ACTION_REQ = 0x32
    const val T_ACTION_RESULT = 0x33

    const val T_PHONE_STATE = 0x40
    const val T_TIME_SYNC = 0x41
    const val T_FIND_START = 0x42
    const val T_FIND_STOP = 0x43

    const val T_MEDIA_STATE = 0x50
    const val T_MEDIA_CMD = 0x51

    const val T_RELAY_START = 0x60
    const val T_RELAY_STOP = 0x61
    const val T_RELAY_INFO = 0x62

    // ---- Codigos de error ---------------------------------------
    const val E_NONE = 0
    const val E_VERSION = 1
    const val E_CRC = 2
    const val E_TOOBIG = 3
    const val E_BADFRAG = 4
    const val E_TIMEOUT = 5
    const val E_NOTPAIRED = 6
    const val E_NOREPLY = 7      // la notificacion no admite RemoteInput
    const val E_GONE = 8         // la notificacion ya no existe / accion caducada
    const val E_DENIED = 9       // permiso revocado en Android
    const val E_BUSY = 10
    const val E_INTERNAL = 11

    fun errName(code: Int): String = when (code) {
        E_NONE -> "sin error"
        E_VERSION -> "version incompatible"
        E_CRC -> "datos corruptos"
        E_TOOBIG -> "mensaje demasiado grande"
        E_BADFRAG -> "fragmento invalido"
        E_TIMEOUT -> "tiempo agotado"
        E_NOTPAIRED -> "dispositivo no emparejado"
        E_NOREPLY -> "esta notificacion no admite respuesta"
        E_GONE -> "la notificacion ya no existe"
        E_DENIED -> "permiso denegado en el telefono"
        E_BUSY -> "ocupado"
        else -> "error interno"
    }

    // ---- Reintentos (misma politica que el firmware) ------------
    const val RETRY_MAX = 5
    const val RETRY_BASE_MS = 500L
    const val RETRY_CAP_MS = 30_000L

    /** Espera antes del intento [attempt] (0-based). 0 = rendirse. */
    fun retryDelayMs(attempt: Int): Long {
        if (attempt >= RETRY_MAX) return 0
        var d = RETRY_BASE_MS
        repeat(attempt) {
            if (d >= RETRY_CAP_MS / 2) { d = RETRY_CAP_MS; return@repeat }
            d *= 2
        }
        return if (d > RETRY_CAP_MS) RETRY_CAP_MS else d
    }

    // =============================================================
    //  CRC16-CCITT (poly 0x1021, init 0xFFFF)
    // =============================================================
    fun crc16(data: ByteArray, offset: Int = 0, length: Int = data.size - offset): Int {
        var crc = 0xFFFF
        for (i in offset until offset + length) {
            crc = crc xor ((data[i].toInt() and 0xFF) shl 8)
            repeat(8) {
                crc = if (crc and 0x8000 != 0) ((crc shl 1) xor 0x1021) and 0xFFFF
                      else (crc shl 1) and 0xFFFF
            }
        }
        return crc and 0xFFFF
    }

    // =============================================================
    //  Cabecera
    // =============================================================
    data class Header(
        val version: Int = VERSION,
        val type: Int,
        val session: Int,
        val packet: Int,
        val frag: Int = 0,
        val fragCount: Int = 1,
        val len: Int = 0,
        val counter: Long = 0,
    )

    /** Resultado de leer una trama. */
    sealed class ReadResult {
        data class Ok(val header: Header, val payload: ByteArray) : ReadResult() {
            // equals/hashCode a mano: ByteArray los compara por referencia.
            override fun equals(other: Any?): Boolean =
                other is Ok && header == other.header && payload.contentEquals(other.payload)
            override fun hashCode(): Int = 31 * header.hashCode() + payload.contentHashCode()
        }
        data class Err(val reason: String) : ReadResult()
    }

    /**
     * Escribe UNA trama. Devuelve el buffer exacto o lanza si los
     * argumentos son imposibles -- que aqui es un fallo de
     * programacion, no un dato del aire.
     */
    fun writeFrame(h: Header, payload: ByteArray? = null): ByteArray {
        val pl = payload ?: ByteArray(0)
        require(pl.size <= MAX_PAYLOAD) { "carga de ${pl.size} B, maximo $MAX_PAYLOAD" }
        require(h.fragCount in 1..MAX_FRAGS) { "fragCount ${h.fragCount} fuera de rango" }
        require(h.frag < h.fragCount) { "frag ${h.frag} >= fragCount ${h.fragCount}" }

        val out = ByteArray(HDR_SIZE + pl.size)
        out[0] = MAGIC0
        out[1] = MAGIC1
        out[2] = h.version.toByte()
        out[3] = h.type.toByte()
        putU16(out, 4, h.session)
        putU16(out, 6, h.packet)
        out[8] = h.frag.toByte()
        out[9] = h.fragCount.toByte()
        putU16(out, 10, pl.size)
        putU32(out, 12, h.counter)
        pl.copyInto(out, HDR_SIZE)
        // El CRC cubre los 16 primeros bytes de cabecera + la carga.
        putU16(out, 16, crc16(out, 0, 16).let { seed ->
            var crc = seed
            for (b in pl) {
                crc = crc xor ((b.toInt() and 0xFF) shl 8)
                repeat(8) {
                    crc = if (crc and 0x8000 != 0) ((crc shl 1) xor 0x1021) and 0xFFFF
                          else (crc shl 1) and 0xFFFF
                }
            }
            crc
        })
        return out
    }

    /**
     * Lee y VALIDA una trama. Todo lo que venga del aire pasa por
     * aqui: magia, longitud declarada, fragmentos, CRC y version, en
     * ese orden y antes de tocar nada.
     */
    fun readFrame(input: ByteArray, length: Int = input.size): ReadResult {
        if (length < HDR_SIZE) return ReadResult.Err("trama corta")
        if (length > MAX_FRAME) return ReadResult.Err("trama demasiado grande")
        if (input[0] != MAGIC0 || input[1] != MAGIC1) return ReadResult.Err("magia equivocada")

        val plen = getU16(input, 10)
        // La longitud declarada tiene que cuadrar EXACTAMENTE.
        if (plen + HDR_SIZE != length) return ReadResult.Err("longitud declarada imposible")
        if (plen > MAX_PAYLOAD) return ReadResult.Err("carga mayor que el maximo")

        val frag = input[8].toInt() and 0xFF
        val fragCount = input[9].toInt() and 0xFF
        if (fragCount == 0 || fragCount > MAX_FRAGS) return ReadResult.Err("fragCount invalido")
        if (frag >= fragCount) return ReadResult.Err("fragmento fuera de rango")

        // CRC antes que la version: una trama corrupta puede traer
        // cualquier cosa en el byte de version.
        val want = getU16(input, 16)
        var crc = crc16(input, 0, 16)
        for (i in 0 until plen) {
            crc = crc xor ((input[HDR_SIZE + i].toInt() and 0xFF) shl 8)
            repeat(8) {
                crc = if (crc and 0x8000 != 0) ((crc shl 1) xor 0x1021) and 0xFFFF
                      else (crc shl 1) and 0xFFFF
            }
        }
        if (crc != want) return ReadResult.Err("CRC no cuadra")

        val ver = input[2].toInt() and 0xFF
        if (ver < VERSION_MIN || ver > VERSION) return ReadResult.Err("version $ver incompatible")

        return ReadResult.Ok(
            Header(
                version = ver,
                type = input[3].toInt() and 0xFF,
                session = getU16(input, 4),
                packet = getU16(input, 6),
                frag = frag,
                fragCount = fragCount,
                len = plen,
                counter = getU32(input, 12),
            ),
            input.copyOfRange(HDR_SIZE, HDR_SIZE + plen),
        )
    }

    /** Cuantos fragmentos hacen falta. 0 = no representable. */
    fun fragCount(msgLen: Int, mtu: Int): Int {
        if (mtu < MIN_MTU) return 0
        val m = if (mtu > MAX_FRAME) MAX_FRAME else mtu
        val per = m - HDR_SIZE
        if (per <= 0) return 0
        if (msgLen > MAX_MESSAGE) return 0
        if (msgLen == 0) return 1
        val k = (msgLen + per - 1) / per
        return if (k > MAX_FRAGS) 0 else k
    }

    /**
     * Trocea un mensaje en tramas listas para enviar.
     *
     * Los fragmentos intermedios van SIEMPRE llenos a MAX_PAYLOAD:
     * el reensamblador del P4 calcula el desplazamiento asumiendolo
     * y rechaza el mensaje entero si no se cumple.
     */
    fun fragment(
        type: Int, session: Int, packet: Int, message: ByteArray,
        counterStart: Long, mtu: Int = MAX_FRAME,
    ): List<ByteArray> {
        require(message.size <= MAX_MESSAGE) { "mensaje de ${message.size} B, maximo $MAX_MESSAGE" }
        val per = MAX_PAYLOAD
        val k = if (message.isEmpty()) 1 else (message.size + per - 1) / per
        require(k <= MAX_FRAGS) { "harian falta $k fragmentos, maximo $MAX_FRAGS" }
        val out = ArrayList<ByteArray>(k)
        for (i in 0 until k) {
            val off = i * per
            val take = minOf(per, message.size - off).coerceAtLeast(0)
            out += writeFrame(
                Header(
                    type = type, session = session, packet = packet,
                    frag = i, fragCount = k, counter = counterStart + i,
                ),
                message.copyOfRange(off, off + take),
            )
        }
        return out
    }

    // ---- Enteros al aire: little-endian, byte a byte -------------
    private fun putU16(b: ByteArray, at: Int, v: Int) {
        b[at] = (v and 0xFF).toByte()
        b[at + 1] = ((v ushr 8) and 0xFF).toByte()
    }
    private fun putU32(b: ByteArray, at: Int, v: Long) {
        b[at] = (v and 0xFF).toByte()
        b[at + 1] = ((v ushr 8) and 0xFF).toByte()
        b[at + 2] = ((v ushr 16) and 0xFF).toByte()
        b[at + 3] = ((v ushr 24) and 0xFF).toByte()
    }
    private fun getU16(b: ByteArray, at: Int): Int =
        (b[at].toInt() and 0xFF) or ((b[at + 1].toInt() and 0xFF) shl 8)
    private fun getU32(b: ByteArray, at: Int): Long =
        (b[at].toLong() and 0xFF) or ((b[at + 1].toLong() and 0xFF) shl 8) or
        ((b[at + 2].toLong() and 0xFF) shl 16) or ((b[at + 3].toLong() and 0xFF) shl 24)
}

/**
 * Ventana anti-repeticion, igual que la del firmware: acepta un
 * contador mayor que el ultimo visto, o uno anterior dentro de la
 * ventana que NO se haya usado ya (BLE puede reordenar; repetir no
 * se permite).
 */
class AntiReplay {
    private var highest = 0L
    private var mask = 0L
    private var primed = false

    fun check(counter: Long): Boolean {
        if (!primed) { primed = true; highest = counter; mask = 0; return true }
        if (counter == highest) return false
        if (counter > highest) {
            val adv = counter - highest
            mask = if (adv >= 32) 0L else (mask shl adv.toInt()) or (1L shl (adv.toInt() - 1))
            highest = counter
            return true
        }
        val back = highest - counter
        if (back > 32) return false
        val bit = 1L shl (back.toInt() - 1)
        if (mask and bit != 0L) return false
        mask = mask or bit
        return true
    }

    fun reset() { highest = 0; mask = 0; primed = false }
}

/**
 * Reensamblador. Un solo mensaje en vuelo, como en el firmware: es
 * lo que evita que un emisor deje parciales colgados.
 */
class Reassembler {
    private val buf = ByteArray(FlexLink.MAX_MESSAGE)
    private var seenMask = 0
    private var packet = -1
    private var len = 0
    private var fragCount = 0
    private var active = false
    private var startedMs = 0L

    var badFrames = 0L; private set
    var duplicates = 0L; private set
    var outOfOrder = 0L; private set
    var abandoned = 0L; private set

    enum class Result { NEED_MORE, DONE, DROP }

    fun feed(h: FlexLink.Header, payload: ByteArray, nowMs: Long): Result {
        if (payload.size > FlexLink.MAX_PAYLOAD) { badFrames++; return Result.DROP }
        if (h.frag >= h.fragCount || h.fragCount > FlexLink.MAX_FRAGS) {
            outOfOrder++; return Result.DROP
        }
        if (active && packet != h.packet) { abandoned++; active = false }
        if (!active) {
            // Un mensaje solo puede EMPEZAR por su fragmento 0.
            if (h.frag != 0) { outOfOrder++; return Result.DROP }
            active = true; packet = h.packet; fragCount = h.fragCount
            seenMask = 0; len = 0; startedMs = nowMs
        }
        if (h.fragCount != fragCount) { outOfOrder++; return Result.DROP }

        val bit = 1 shl h.frag
        if (seenMask and bit != 0) { duplicates++; return Result.DROP }

        val off = h.frag * FlexLink.MAX_PAYLOAD
        if (off + payload.size > FlexLink.MAX_MESSAGE) { badFrames++; return Result.DROP }
        if (h.frag + 1 < h.fragCount && payload.size != FlexLink.MAX_PAYLOAD) {
            badFrames++; active = false; return Result.DROP
        }
        payload.copyInto(buf, off)
        seenMask = seenMask or bit
        if (off + payload.size > len) len = off + payload.size

        val full = if (fragCount >= 32) -1 else (1 shl fragCount) - 1
        if (seenMask and full == full) { active = false; return Result.DONE }
        return Result.NEED_MORE
    }

    /** Mensaje completo. Solo tiene sentido tras un DONE. */
    fun message(): ByteArray = buf.copyOfRange(0, len)

    fun expire(nowMs: Long, timeoutMs: Long): Boolean {
        if (!active) return false
        if (nowMs - startedMs < timeoutMs) return false
        active = false; abandoned++
        return true
    }

    fun reset() { active = false; seenMask = 0; len = 0; packet = -1 }
}
