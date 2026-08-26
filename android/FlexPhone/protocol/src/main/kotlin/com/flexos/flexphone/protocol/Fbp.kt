package com.flexos.flexphone.protocol

/**
 * FBP/1 -- Flex Browser Protocol, lado servicio.
 *
 * POR QUE EL RELAY HABLA ESTE PROTOCOLO Y NO OTRO
 * -----------------------------------------------
 * Flex OS ya tiene un navegador completo: interfaz, pestanas,
 * historial, favoritos, cache y tactil. Lo unico que le falta es
 * alguien que EJECUTE la pagina. Ese papel lo hace hoy el servicio
 * Ubuntu/PC hablando FBP/1 por WebSocket.
 *
 * El Browser Relay del telefono implementa EL MISMO protocolo. Asi:
 *   · no hay que reescribir ni una linea del navegador del P4,
 *   · el servidor Ubuntu sigue funcionando exactamente igual,
 *   · y el usuario puede cambiar de fuente sin notar la diferencia.
 *
 * La especificacion es `server/PROTOCOL.md`. Los desplazamientos de
 * este fichero estan verificados byte a byte contra el codec del
 * firmware (`FlexOS_Browser.cpp`) en `FbpTest`.
 *
 * Kotlin/JVM puro: se prueba sin Android, igual que Flex Link.
 */
object Fbp {

    const val VERSION = 1
    const val HDR_SIZE = 12

    private const val MAGIC0 = 0x46.toByte()   // 'F'
    private const val MAGIC1 = 0x42.toByte()   // 'B'

    // ---- Canales ------------------------------------------------
    const val CH_SESSION = 0
    const val CH_MEDIA = 200
    const val CH_FAVICON = 201

    // ---- Dispositivo -> servicio --------------------------------
    const val T_HELLO = 0x01
    const val T_PING = 0x02
    const val T_ACK = 0x03
    const val T_NAVIGATE = 0x10
    const val T_BACK = 0x11
    const val T_FORWARD = 0x12
    const val T_RELOAD = 0x13
    const val T_STOP = 0x14
    const val T_POINTER = 0x20
    const val T_SCROLL = 0x21
    const val T_KEY = 0x22
    const val T_VIEWPORT = 0x23
    const val T_TAB_NEW = 0x30
    const val T_TAB_CLOSE = 0x31
    const val T_TAB_SELECT = 0x32
    const val T_MEDIA = 0x40
    const val T_REQ_FRAME = 0x41

    // ---- Servicio -> dispositivo --------------------------------
    const val T_WELCOME = 0x81
    const val T_STATE = 0x82
    const val T_FRAME = 0x83
    const val T_MEDIA_INFO = 0x84
    const val T_ERROR = 0x85
    const val T_PONG = 0x86
    const val T_NAVIGATED = 0x87
    const val T_TABS = 0x88
    const val T_DOWNLOAD = 0x89

    // ---- Acciones -----------------------------------------------
    object Pointer { const val DOWN = 0; const val UP = 1; const val MOVE = 2; const val TAP = 3; const val CANCEL = 4 }
    object Key { const val TEXT = 0; const val PRESS = 1; const val RELEASE = 2 }
    object KeyCode {
        const val ENTER = 1; const val BACKSPACE = 2; const val TAB = 3; const val ESC = 4
        const val LEFT = 5; const val UP = 6; const val RIGHT = 7; const val DOWN = 8
    }

    /** Banderas de FRAME. */
    const val FRAME_KEYFRAME = 0x01
    const val FRAME_LAST = 0x02
    const val FORMAT_JPEG = 1

    /** Banderas de STATE. */
    const val ST_LOADING = 0x01
    const val ST_CAN_BACK = 0x02
    const val ST_CAN_FORWARD = 0x04
    const val ST_SECURE = 0x08

    data class Header(
        val version: Int, val type: Int, val flags: Int,
        val channel: Int, val seq: Int, val length: Long,
    )

    /**
     * Cabecera de 12 bytes, little-endian (el orden nativo del
     * ESP32: cero conversiones en el lado con menos CPU).
     */
    fun writeHeader(type: Int, flags: Int, channel: Int, seq: Int, payloadLen: Int): ByteArray {
        val b = ByteArray(HDR_SIZE)
        b[0] = MAGIC0; b[1] = MAGIC1
        b[2] = VERSION.toByte()
        b[3] = type.toByte()
        b[4] = flags.toByte()
        b[5] = channel.toByte()
        b[6] = (seq and 0xFF).toByte()
        b[7] = ((seq ushr 8) and 0xFF).toByte()
        b[8] = (payloadLen and 0xFF).toByte()
        b[9] = ((payloadLen ushr 8) and 0xFF).toByte()
        b[10] = ((payloadLen ushr 16) and 0xFF).toByte()
        b[11] = ((payloadLen ushr 24) and 0xFF).toByte()
        return b
    }

    fun readHeader(b: ByteArray, len: Int = b.size): Header? {
        if (len < HDR_SIZE) return null
        if (b[0] != MAGIC0 || b[1] != MAGIC1) return null
        val ver = b[2].toInt() and 0xFF
        if (ver != VERSION) return null          // no se negocia a la baja
        val length = (b[8].toLong() and 0xFF) or ((b[9].toLong() and 0xFF) shl 8) or
                     ((b[10].toLong() and 0xFF) shl 16) or ((b[11].toLong() and 0xFF) shl 24)
        return Header(
            version = ver,
            type = b[3].toInt() and 0xFF,
            flags = b[4].toInt() and 0xFF,
            channel = b[5].toInt() and 0xFF,
            seq = (b[6].toInt() and 0xFF) or ((b[7].toInt() and 0xFF) shl 8),
            length = length,
        )
    }

    /** Mensaje completo = cabecera + carga. */
    fun message(type: Int, flags: Int, channel: Int, seq: Int, payload: ByteArray = ByteArray(0)): ByteArray {
        val h = writeHeader(type, flags, channel, seq, payload.size)
        val out = ByteArray(h.size + payload.size)
        h.copyInto(out)
        payload.copyInto(out, h.size)
        return out
    }

    // ---- Cadenas FBP: u16 de longitud + bytes UTF-8, sin 0 final --
    fun putString(w: FbpWriter, s: String, maxBytes: Int = 65535) {
        val raw = s.toByteArray(Charsets.UTF_8)
        val take = Utf8.truncate(raw, minOf(maxBytes, 65535))
        w.u16(take)
        w.bytes(raw.copyOfRange(0, take))
    }

    // =============================================================
    //  Mensajes del SERVICIO (los que emite el relay del telefono)
    // =============================================================

    /**
     * WELCOME. Los valores CONCEDIDOS pueden ser menores que los
     * pedidos: el telefono no promete lo que no puede sostener.
     */
    fun welcome(
        seq: Int, viewportW: Int, viewportH: Int, caps: Long,
        maxTabs: Int, maxFrameBytes: Long, sessionId: String,
    ): ByteArray {
        val w = FbpWriter()
        w.u8(VERSION); w.u16(viewportW); w.u16(viewportH)
        w.u32(caps); w.u16(maxTabs); w.u32(maxFrameBytes)
        putString(w, sessionId, 64)
        return message(T_WELCOME, 0, CH_SESSION, seq, w.build())
    }

    fun state(seq: Int, channel: Int, flags: Int, progress: Int, title: String, url: String): ByteArray {
        val w = FbpWriter()
        w.u8(flags); w.u8(progress)
        putString(w, title, 512)
        putString(w, url, 2048)
        return message(T_STATE, 0, channel, seq, w.build())
    }

    /**
     * FRAME. La region va en COORDENADAS DE PANTALLA del
     * dispositivo; la imagen puede ser mas pequena si se pidio
     * escala reducida.
     */
    fun frame(
        seq: Int, channel: Int, x: Int, y: Int, w: Int, h: Int,
        keyframe: Boolean, last: Boolean, frameId: Long, image: ByteArray,
    ): ByteArray {
        val p = FbpWriter()
        p.u16(x); p.u16(y); p.u16(w); p.u16(h)
        p.u8(FORMAT_JPEG)
        var fl = 0
        if (keyframe) fl = fl or FRAME_KEYFRAME
        if (last) fl = fl or FRAME_LAST
        p.u8(fl)
        p.u32(frameId)
        p.u32(image.size.toLong())
        p.bytes(image)
        return message(T_FRAME, 0, channel, seq, p.build())
    }

    fun error(seq: Int, channel: Int, code: Int, msg: String): ByteArray {
        val w = FbpWriter()
        w.u16(code)
        putString(w, msg, 512)
        return message(T_ERROR, 0, channel, seq, w.build())
    }

    fun pong(seq: Int): ByteArray = message(T_PONG, 0, CH_SESSION, seq)

    fun navigated(seq: Int, channel: Int, url: String): ByteArray {
        val w = FbpWriter()
        putString(w, url, 2048)
        return message(T_NAVIGATED, 0, channel, seq, w.build())
    }

    fun tabs(seq: Int, active: Int, titles: List<Pair<Int, String>>): ByteArray {
        val w = FbpWriter()
        w.u8(titles.size); w.u8(active)
        for ((id, t) in titles) { w.u8(id); putString(w, t, 256) }
        return message(T_TABS, 0, CH_SESSION, seq, w.build())
    }

    // =============================================================
    //  Cargas del DISPOSITIVO que el relay tiene que leer
    // =============================================================
    data class Hello(
        val version: Int, val viewportW: Int, val viewportH: Int,
        val quality: Int, val profile: Int, val caps: Long,
        val formats: Int, val maxTabs: Int, val maxFrameBytes: Long,
        val token: String, val deviceId: String,
    )

    fun parseHello(p: ByteArray): Hello? {
        val r = FbpReader(p)
        val h = Hello(
            version = r.u8(), viewportW = r.u16(), viewportH = r.u16(),
            quality = r.u8(), profile = r.u8(), caps = r.u32(),
            formats = r.u8(), maxTabs = r.u8(), maxFrameBytes = r.u32(),
            token = r.str(), deviceId = r.str(),
        )
        return if (r.ok) h else null
    }

    data class PointerEvent(val action: Int, val x: Int, val y: Int, val button: Int)
    fun parsePointer(p: ByteArray): PointerEvent? {
        val r = FbpReader(p)
        val e = PointerEvent(r.u8(), r.u16(), r.u16(), r.u8())
        return if (r.ok) e else null
    }

    data class ScrollEvent(val dx: Int, val dy: Int)
    fun parseScroll(p: ByteArray): ScrollEvent? {
        val r = FbpReader(p)
        val e = ScrollEvent(r.i16(), r.i16())
        return if (r.ok) e else null
    }

    data class KeyEvent(val action: Int, val code: Int, val mods: Int, val text: String)
    fun parseKey(p: ByteArray): KeyEvent? {
        val r = FbpReader(p)
        val e = KeyEvent(r.u8(), r.u8(), r.u8(), r.str())
        return if (r.ok) e else null
    }

    data class Viewport(val w: Int, val h: Int, val quality: Int, val profile: Int, val scalePct: Int)
    fun parseViewport(p: ByteArray): Viewport? {
        val r = FbpReader(p)
        val v = Viewport(r.u16(), r.u16(), r.u8(), r.u8(), r.u8())
        return if (r.ok) v else null
    }

    fun parseNavigate(p: ByteArray): String? {
        val r = FbpReader(p)
        val s = r.str()
        return if (r.ok) s else null
    }
}

/** Escritor little-endian para cargas FBP. */
class FbpWriter(capacity: Int = 256) {
    private var buf = ByteArray(capacity)
    private var at = 0
    private fun ensure(n: Int) {
        if (at + n <= buf.size) return
        var s = buf.size * 2
        while (s < at + n) s *= 2
        buf = buf.copyOf(s)
    }
    fun u8(v: Int) = apply { ensure(1); buf[at++] = (v and 0xFF).toByte() }
    fun u16(v: Int) = apply {
        ensure(2); buf[at++] = (v and 0xFF).toByte(); buf[at++] = ((v ushr 8) and 0xFF).toByte()
    }
    fun i16(v: Int) = u16(v and 0xFFFF)
    fun u32(v: Long) = apply {
        ensure(4)
        buf[at++] = (v and 0xFF).toByte(); buf[at++] = ((v ushr 8) and 0xFF).toByte()
        buf[at++] = ((v ushr 16) and 0xFF).toByte(); buf[at++] = ((v ushr 24) and 0xFF).toByte()
    }
    fun bytes(b: ByteArray) = apply { ensure(b.size); b.copyInto(buf, at); at += b.size }
    fun build(): ByteArray = buf.copyOfRange(0, at)
}

/** Lector little-endian. Nunca lee fuera; marca `overflow`. */
class FbpReader(private val b: ByteArray, private val len: Int = b.size) {
    private var at = 0
    var overflow = false; private set
    private fun fit(n: Int): Boolean {
        if (overflow) return false
        if (at + n > len) { overflow = true; return false }
        return true
    }
    fun u8(): Int = if (!fit(1)) 0 else b[at++].toInt() and 0xFF
    fun u16(): Int {
        if (!fit(2)) return 0
        val v = (b[at].toInt() and 0xFF) or ((b[at + 1].toInt() and 0xFF) shl 8); at += 2; return v
    }
    fun i16(): Int { val v = u16(); return if (v >= 0x8000) v - 0x10000 else v }
    fun u32(): Long {
        if (!fit(4)) return 0
        val v = (b[at].toLong() and 0xFF) or ((b[at + 1].toLong() and 0xFF) shl 8) or
                ((b[at + 2].toLong() and 0xFF) shl 16) or ((b[at + 3].toLong() and 0xFF) shl 24)
        at += 4; return v
    }
    fun str(): String {
        val n = u16()
        if (!fit(n)) return ""
        val s = String(b, at, n, Charsets.UTF_8); at += n; return s
    }
    val ok: Boolean get() = !overflow
}
