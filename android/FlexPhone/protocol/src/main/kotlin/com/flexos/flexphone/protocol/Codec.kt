package com.flexos.flexphone.protocol

/**
 * Codecs de carga: el espejo de la seccion "Codecs" de
 * `FlexOS_FlexPhone.cpp`.
 *
 * Las cadenas van con prefijo de longitud de UN byte y se truncan en
 * FRONTERA UTF-8. Esa parte importa mas de lo que parece: un titulo
 * de notificacion con un emoji cortado por la mitad llega al P4 como
 * UTF-8 invalido, y el firmware lo corta ahi -- el usuario ve el
 * texto truncado antes de tiempo y no hay ningun error que lo
 * explique. Aqui se trunca bien desde el origen.
 */

/** Escritor con cursor. Nunca escribe fuera; marca `overflow`. */
class PayloadWriter(capacity: Int = 512) {
    private var buf = ByteArray(capacity)
    private var at = 0
    var overflow = false
        private set

    private fun ensure(extra: Int) {
        if (at + extra <= buf.size) return
        var n = buf.size * 2
        while (n < at + extra) n *= 2
        buf = buf.copyOf(n)
    }

    fun u8(v: Int) = apply { ensure(1); buf[at++] = (v and 0xFF).toByte() }

    fun u16(v: Int) = apply {
        ensure(2)
        buf[at++] = (v and 0xFF).toByte()
        buf[at++] = ((v ushr 8) and 0xFF).toByte()
    }

    fun u32(v: Long) = apply {
        ensure(4)
        buf[at++] = (v and 0xFF).toByte()
        buf[at++] = ((v ushr 8) and 0xFF).toByte()
        buf[at++] = ((v ushr 16) and 0xFF).toByte()
        buf[at++] = ((v ushr 24) and 0xFF).toByte()
    }

    fun bytes(src: ByteArray) = apply { ensure(src.size); src.copyInto(buf, at); at += src.size }

    /**
     * Cadena con prefijo de longitud. [maxLen] se recorta a 255
     * (el prefijo es de un byte) y el texto se trunca sin partir un
     * caracter multibyte.
     */
    fun str(s: String?, maxLen: Int) = apply {
        val cap = minOf(maxLen, 255)
        val raw = (s ?: "").toByteArray(Charsets.UTF_8)
        val take = Utf8.truncate(raw, cap)
        ensure(1 + take)
        buf[at++] = take.toByte()
        raw.copyInto(buf, at, 0, take)
        at += take
    }

    fun build(): ByteArray = buf.copyOfRange(0, at)
    val size: Int get() = at
}

/** Lector con cursor. Nunca lee fuera; marca `overflow`. */
class PayloadReader(private val buf: ByteArray, private val len: Int = buf.size) {
    private var at = 0
    var overflow = false
        private set

    private fun fit(n: Int): Boolean {
        if (overflow) return false
        if (at + n > len) { overflow = true; return false }
        return true
    }

    fun u8(): Int = if (!fit(1)) 0 else buf[at++].toInt() and 0xFF

    fun u16(): Int {
        if (!fit(2)) return 0
        val v = (buf[at].toInt() and 0xFF) or ((buf[at + 1].toInt() and 0xFF) shl 8)
        at += 2
        return v
    }

    fun u32(): Long {
        if (!fit(4)) return 0
        val v = (buf[at].toLong() and 0xFF) or ((buf[at + 1].toLong() and 0xFF) shl 8) or
                ((buf[at + 2].toLong() and 0xFF) shl 16) or ((buf[at + 3].toLong() and 0xFF) shl 24)
        at += 4
        return v
    }

    fun bytes(n: Int): ByteArray {
        if (!fit(n)) return ByteArray(n)
        val out = buf.copyOfRange(at, at + n)
        at += n
        return out
    }

    /** Cadena con prefijo de longitud. Una longitud que no cuadra marca overflow. */
    fun str(): String {
        if (!fit(1)) return ""
        val n = buf[at++].toInt() and 0xFF
        if (!fit(n)) return ""
        val s = String(buf, at, n, Charsets.UTF_8)
        at += n
        return s
    }

    val ok: Boolean get() = !overflow
}

/** Truncado UTF-8 que no parte caracteres. Misma regla que el firmware. */
object Utf8 {
    /** Longitud esperada de la secuencia que empieza por [c]. 0 = invalida. */
    private fun seqLen(c: Int): Int = when {
        c < 0x80 -> 1
        c and 0xE0 == 0xC0 -> 2
        c and 0xF0 == 0xE0 -> 3
        c and 0xF8 == 0xF0 -> 4
        else -> 0
    }

    /** Bytes de un prefijo VALIDO de [raw] que no pase de [maxBytes]. */
    fun truncate(raw: ByteArray, maxBytes: Int): Int {
        if (maxBytes <= 0) return 0
        var i = 0
        while (i < raw.size && i < maxBytes) {
            val need = seqLen(raw[i].toInt() and 0xFF)
            if (need <= 0) break                       // secuencia invalida: se corta
            if (i + need > maxBytes) break             // no cabe entera: no se parte
            var ok = true
            for (k in 1 until need) {
                if (i + k >= raw.size || (raw[i + k].toInt() and 0xC0) != 0x80) { ok = false; break }
            }
            if (!ok) break
            i += need
        }
        return i
    }

    /** Recorta [s] a como mucho [maxBytes] bytes UTF-8, sin partir. */
    fun clip(s: String, maxBytes: Int): String {
        val raw = s.toByteArray(Charsets.UTF_8)
        if (raw.size <= maxBytes) return s
        return String(raw, 0, truncate(raw, maxBytes), Charsets.UTF_8)
    }
}

// =============================================================
//  Modelos que viajan por el enlace
// =============================================================
// Los limites son los MISMOS que en FlexOS_FlexPhone.h: si aqui se
// mandara mas, el firmware lo truncaria por su cuenta y el usuario
// veria un texto cortado sin saber por que.
object Limits {
    const val PKG = 64
    const val APPNAME = 32
    const val TITLE = 64
    const val TEXT = 160          // resumen, NO el mensaje entero
    const val ACTIONS = 4
    const val ACTLABEL = 24
    const val REPLY = 256
    const val DEVNAME = 32
    const val MEDIA_TXT = 48
}

object Category {
    const val OTHER = 0; const val MSG = 1; const val CALL = 2; const val EMAIL = 3
    const val SOCIAL = 4; const val ALARM = 5; const val TRANSPORT = 6; const val SYS = 7
}

object Priority {
    const val MIN = 0; const val LOW = 1; const val DEFAULT = 2; const val HIGH = 3; const val URGENT = 4
}

object MediaStatus { const val STOP = 0; const val PLAYING = 1; const val PAUSED = 2 }
object MediaCmd { const val PLAY = 1; const val PAUSE = 2; const val NEXT = 3; const val PREV = 4 }
object NetKind { const val UNKNOWN = 0; const val NONE = 1; const val WIFI = 2; const val MOBILE = 3 }

/**
 * Una notificacion tal como se manda al P4.
 *
 * [canReply] es la pieza clave de todo el diseno: es `true` SOLO si
 * Android expuso una accion con `RemoteInput`. Flex OS usa ese bit
 * para decidir si ofrece el boton "Responder", asi que ponerlo a la
 * ligera es exactamente lo que produciria un boton que no hace nada.
 */
data class NotifPayload(
    val id: Long,
    val pkg: String,
    val app: String,
    val title: String,
    val text: String,
    val whenMs: Long,
    val category: Int,
    val priority: Int,
    val hidden: Boolean,
    val sensitive: Boolean,
    val canReply: Boolean,
    val replyAction: Int,
    val actions: List<String>,
) {
    fun encode(): ByteArray {
        val w = PayloadWriter()
        w.u32(id)
        w.str(pkg, Limits.PKG - 1)
        w.str(app, Limits.APPNAME - 1)
        w.str(title, Limits.TITLE - 1)
        w.str(text, Limits.TEXT - 1)
        w.u32(whenMs)
        w.u8(category)
        w.u8(priority)
        var flags = 0
        if (hidden) flags = flags or 0x01
        if (sensitive) flags = flags or 0x02
        if (canReply) flags = flags or 0x04
        w.u8(flags)
        w.u8(replyAction)
        val k = minOf(actions.size, Limits.ACTIONS)
        w.u8(k)
        for (i in 0 until k) w.str(actions[i], Limits.ACTLABEL - 1)
        return w.build()
    }

    companion object {
        /** Decodifica. `null` si los bytes no cuadran. */
        fun decode(data: ByteArray): NotifPayload? {
            val r = PayloadReader(data)
            val id = r.u32()
            val pkg = r.str(); val app = r.str(); val title = r.str(); val text = r.str()
            val whenMs = r.u32()
            var cat = r.u8(); var pri = r.u8()
            val flags = r.u8()
            val replyAction = r.u8()
            var k = r.u8()
            if (k > Limits.ACTIONS) k = Limits.ACTIONS
            val acts = ArrayList<String>(k)
            repeat(k) { acts += r.str() }
            if (!r.ok || pkg.isEmpty()) return null
            if (cat !in 0..Category.SYS) cat = Category.OTHER
            if (pri !in Priority.MIN..Priority.URGENT) pri = Priority.DEFAULT
            var canReply = flags and 0x04 != 0
            // No se puede responder a una accion que no existe.
            if (canReply && replyAction >= acts.size) canReply = false
            return NotifPayload(
                id, pkg, app, title, text, whenMs, cat, pri,
                hidden = flags and 0x01 != 0,
                sensitive = flags and 0x02 != 0,
                canReply = canReply,
                replyAction = replyAction,
                actions = acts,
            )
        }
    }
}

data class ReplyRequest(val notifId: Long, val action: Int, val text: String) {
    companion object {
        fun decode(data: ByteArray): ReplyRequest? {
            val r = PayloadReader(data)
            val id = r.u32(); val act = r.u8(); val txt = r.str()
            return if (r.ok) ReplyRequest(id, act, txt) else null
        }
    }
    fun encode(): ByteArray =
        PayloadWriter().u32(notifId).u8(action).str(text, minOf(Limits.REPLY - 1, 255)).build()
}

/** Resultado REAL de un intento de respuesta. [error] es FlexLink.E_*. */
data class ReplyResult(val notifId: Long, val error: Int) {
    fun encode(): ByteArray = PayloadWriter().u32(notifId).u8(error).build()
    val ok: Boolean get() = error == FlexLink.E_NONE
}

data class PhoneState(
    val name: String, val battery: Int, val charging: Boolean, val net: Int,
) {
    fun encode(): ByteArray {
        val w = PayloadWriter()
        w.str(name, Limits.DEVNAME - 1)
        // 255 = DESCONOCIDA. Nunca se manda 0 para decir "no lo se":
        // el P4 lo pintaria como una bateria vacia.
        w.u8(if (battery in 0..100) battery else 255)
        w.u8(if (charging) 1 else 0)
        w.u8(net)
        return w.build()
    }
}

data class MediaState(
    val title: String, val artist: String, val app: String, val status: Int,
) {
    fun encode(): ByteArray = PayloadWriter()
        .str(title, Limits.MEDIA_TXT - 1)
        .str(artist, Limits.MEDIA_TXT - 1)
        .str(app, Limits.APPNAME - 1)
        .u8(status)
        .build()
}

/**
 * Anuncio del Browser Relay. La IP y el puerto son los REALES en los
 * que el telefono esta escuchando; si el relay no llego a levantarse,
 * se manda [error] y el P4 lo muestra como error, nunca como
 * "conectado".
 */
data class RelayInfo(
    val ip: ByteArray, val port: Int, val protoVer: Int,
    val tls: Boolean, val caps: Int, val error: String = "",
) {
    fun encode(): ByteArray {
        require(ip.size == 4) { "la IP tiene que ser IPv4 (4 bytes)" }
        val w = PayloadWriter()
        w.bytes(ip); w.u16(port); w.u8(protoVer)
        w.u8(if (tls) 1 else 0); w.u16(caps)
        w.str(error, 63)
        return w.build()
    }
    override fun equals(other: Any?): Boolean =
        other is RelayInfo && ip.contentEquals(other.ip) && port == other.port &&
        protoVer == other.protoVer && tls == other.tls && caps == other.caps && error == other.error
    override fun hashCode(): Int = ip.contentHashCode() * 31 + port
}
