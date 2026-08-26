package com.flexos.flexphone.relay

import android.util.Log
import com.flexos.flexphone.protocol.Fbp
import com.flexos.flexphone.protocol.FbpReader
import java.io.InputStream
import java.io.OutputStream
import java.net.Socket
import java.util.concurrent.atomic.AtomicInteger

/**
 * Una sesion WebSocket con Flex OS, hablando FBP/1.
 *
 * ENMARCADO WEBSOCKET: solo lo que hace falta. Tramas binarias,
 * ping/pong y close. Sin compresion por mensaje -- la carga ya son
 * JPEG y comprimirlos otra vez solo gasta CPU del telefono.
 *
 * TODO lo que llega se valida antes de usarse: la longitud, la
 * mascara y el tipo. Un cliente que mande una trama de 4 GB no puede
 * hacer que la app reserve 4 GB.
 */
class RelaySession(
    val socket: Socket,
    private val input: InputStream,
    private val output: OutputStream,
    private val expectedToken: String,
    private val onEvent: (RelayServer.Event) -> Unit,
) {
    companion object {
        private const val TAG = "FlexPhone/RelaySess"
        /** Tope de una trama entrante. El P4 nunca manda nada grande. */
        private const val MAX_INBOUND = 64 * 1024
        private const val OP_BINARY = 0x2
        private const val OP_CLOSE = 0x8
        private const val OP_PING = 0x9
        private const val OP_PONG = 0xA
    }

    private val seq = AtomicInteger(1)
    @Volatile private var authenticated = false
    @Volatile private var closed = false
    private val writeLock = Any()

    private fun nextSeq(): Int {
        // El seq se reinicia en 1 al desbordar, como dice PROTOCOL.md.
        val v = seq.getAndIncrement()
        if (v >= 0xFFFF) seq.set(1)
        return v and 0xFFFF
    }

    fun loop() {
        while (!closed) {
            val msg = readFrame() ?: break
            handle(msg)
        }
        RelayEngine.detach()
    }

    // ---------------------------------------------------------
    //  FBP
    // ---------------------------------------------------------
    private fun handle(data: ByteArray) {
        val h = Fbp.readHeader(data) ?: run {
            // Cabecera que no cuadra: se corta la sesion. Seguir
            // leyendo un flujo desalineado solo produce basura.
            close(1002, "cabecera FBP invalida")
            return
        }
        if (h.length > MAX_INBOUND || Fbp.HDR_SIZE + h.length > data.size) {
            close(1009, "mensaje demasiado grande"); return
        }
        val payload = data.copyOfRange(Fbp.HDR_SIZE, (Fbp.HDR_SIZE + h.length).toInt())

        // NADA se atiende antes del HELLO: sin token valido, esta
        // conexion no puede navegar.
        if (!authenticated && h.type != Fbp.T_HELLO) {
            close(1008, "falta el saludo"); return
        }

        when (h.type) {
            Fbp.T_HELLO -> onHello(payload)
            Fbp.T_PING -> sendBinary(Fbp.pong(nextSeq()))
            Fbp.T_ACK -> Unit
            Fbp.T_NAVIGATE -> Fbp.parseNavigate(payload)?.let { RelayEngine.navigate(h.channel, it) }
            Fbp.T_BACK -> RelayEngine.back(h.channel)
            Fbp.T_FORWARD -> RelayEngine.forward(h.channel)
            Fbp.T_RELOAD -> RelayEngine.reload(h.channel)
            Fbp.T_STOP -> RelayEngine.stopLoading(h.channel)
            Fbp.T_POINTER -> Fbp.parsePointer(payload)?.let {
                RelayEngine.pointer(h.channel, it.action, it.x, it.y)
            }
            Fbp.T_SCROLL -> Fbp.parseScroll(payload)?.let { RelayEngine.scroll(h.channel, it.dx, it.dy) }
            Fbp.T_KEY -> Fbp.parseKey(payload)?.let { RelayEngine.key(h.channel, it.action, it.code, it.text) }
            Fbp.T_VIEWPORT -> Fbp.parseViewport(payload)?.let { RelayEngine.viewport(it) }
            Fbp.T_TAB_NEW -> RelayEngine.newTab()
            Fbp.T_TAB_CLOSE -> RelayEngine.closeTab(h.channel)
            Fbp.T_TAB_SELECT -> RelayEngine.selectTab(h.channel)
            Fbp.T_REQ_FRAME -> RelayEngine.requestKeyframe(h.channel)
            Fbp.T_MEDIA -> Unit          // el reproductor propio no lo sirve el relay
            else -> Unit                 // tipo desconocido: se ignora, no se cae
        }
    }

    private fun onHello(payload: ByteArray) {
        val hello = Fbp.parseHello(payload)
        if (hello == null) { close(1002, "HELLO invalido"); return }
        if (hello.version != Fbp.VERSION) {
            // No se negocia a la baja: es la regla de PROTOCOL.md.
            sendBinary(Fbp.error(nextSeq(), 0, 426, "version de protocolo incompatible"))
            close(1002, "version incompatible"); return
        }
        // TOKEN: el que se acordo por BLE. Sin el, esta conexion no
        // viene de un Flex OS emparejado.
        if (expectedToken.isNotEmpty() && hello.token != expectedToken) {
            sendBinary(Fbp.error(nextSeq(), 0, 401, "dispositivo no emparejado"))
            close(1008, "token invalido")
            Log.w(TAG, "conexion rechazada: token invalido")
            return
        }
        authenticated = true

        val vp = RelayEngine.configure(hello)
        sendBinary(
            Fbp.welcome(
                seq = nextSeq(),
                viewportW = vp.first, viewportH = vp.second,
                caps = RelayEngine.capabilities(),
                // Se concede lo que el telefono PUEDE sostener, que
                // puede ser menos de lo que el P4 pidio.
                maxTabs = RelayEngine.maxTabs(hello.maxTabs),
                maxFrameBytes = RelayEngine.maxFrameBytes(hello.maxFrameBytes),
                sessionId = RelayEngine.sessionId(),
            )
        )
        RelayEngine.attach(this)
    }

    // ---------------------------------------------------------
    //  Salida
    // ---------------------------------------------------------
    fun sendFrame(channel: Int, x: Int, y: Int, w: Int, h: Int,
                  keyframe: Boolean, last: Boolean, frameId: Long, image: ByteArray): Boolean =
        sendBinary(Fbp.frame(nextSeq(), channel, x, y, w, h, keyframe, last, frameId, image))

    fun sendState(channel: Int, flags: Int, progress: Int, title: String, url: String): Boolean =
        sendBinary(Fbp.state(nextSeq(), channel, flags, progress, title, url))

    fun sendError(channel: Int, code: Int, msg: String): Boolean =
        sendBinary(Fbp.error(nextSeq(), channel, code, msg))

    fun sendBinary(payload: ByteArray): Boolean {
        if (closed) return false
        return try {
            synchronized(writeLock) { writeFrame(OP_BINARY, payload) }
            true
        } catch (e: Exception) {
            closed = true
            false
        }
    }

    private fun writeFrame(opcode: Int, payload: ByteArray) {
        val n = payload.size
        // El SERVIDOR no enmascara (RFC 6455): solo el cliente lo hace.
        val head = when {
            n < 126 -> byteArrayOf((0x80 or opcode).toByte(), n.toByte())
            n <= 0xFFFF -> byteArrayOf(
                (0x80 or opcode).toByte(), 126,
                ((n ushr 8) and 0xFF).toByte(), (n and 0xFF).toByte(),
            )
            else -> byteArrayOf(
                (0x80 or opcode).toByte(), 127,
                0, 0, 0, 0,
                ((n ushr 24) and 0xFF).toByte(), ((n ushr 16) and 0xFF).toByte(),
                ((n ushr 8) and 0xFF).toByte(), (n and 0xFF).toByte(),
            )
        }
        output.write(head)
        if (n > 0) output.write(payload)
        output.flush()
    }

    // ---------------------------------------------------------
    //  Entrada
    // ---------------------------------------------------------
    private fun readFrame(): ByteArray? {
        try {
            val b0 = input.read(); if (b0 < 0) return null
            val b1 = input.read(); if (b1 < 0) return null
            val opcode = b0 and 0x0F
            val masked = (b1 and 0x80) != 0
            var len = (b1 and 0x7F).toLong()
            if (len == 126L) {
                len = ((input.read().toLong() and 0xFF) shl 8) or (input.read().toLong() and 0xFF)
            } else if (len == 127L) {
                len = 0
                repeat(8) { len = (len shl 8) or (input.read().toLong() and 0xFF) }
            }
            // TOPE antes de reservar nada: si no, un byte de longitud
            // manipulado reserva memoria arbitraria.
            if (len < 0 || len > MAX_INBOUND) { close(1009, "trama demasiado grande"); return null }

            val mask = ByteArray(4)
            if (masked) { if (!readFully(mask, 4)) return null }

            val data = ByteArray(len.toInt())
            if (!readFully(data, data.size)) return null
            if (masked) for (i in data.indices) data[i] = (data[i].toInt() xor mask[i % 4].toInt()).toByte()

            return when (opcode) {
                OP_BINARY -> data
                OP_PING -> { synchronized(writeLock) { writeFrame(OP_PONG, data) }; readFrame() }
                OP_PONG -> readFrame()
                OP_CLOSE -> { closed = true; null }
                else -> readFrame()          // texto u opcode raro: se ignora
            }
        } catch (e: Exception) {
            closed = true
            return null
        }
    }

    private fun readFully(dst: ByteArray, n: Int): Boolean {
        var at = 0
        while (at < n) {
            val r = input.read(dst, at, n - at)
            if (r < 0) return false
            at += r
        }
        return true
    }

    fun close(code: Int = 1000, reason: String = "") {
        if (closed) return
        closed = true
        runCatching {
            val body = reason.toByteArray(Charsets.UTF_8)
            val payload = ByteArray(2 + body.size)
            payload[0] = ((code ushr 8) and 0xFF).toByte()
            payload[1] = (code and 0xFF).toByte()
            body.copyInto(payload, 2)
            synchronized(writeLock) { writeFrame(OP_CLOSE, payload) }
        }
        runCatching { socket.close() }
    }
}
