package com.flexos.flexphone.relay

import android.util.Log
import com.flexos.flexphone.protocol.Fbp
import com.flexos.flexphone.protocol.FbpReader
import java.io.InputStream
import java.io.OutputStream
import java.net.Socket
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
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
    /** HELLO valido: esta conexion pasa a ser LA sesion (ver RelayServer). */
    private val onAuthenticated: (RelaySession) -> Unit = {},
) {
    companion object {
        private const val TAG = "FlexPhone/RelaySess"
        /** Tope de una trama entrante. El P4 nunca manda nada grande. */
        private const val MAX_INBOUND = 64 * 1024
        /**
         * Silencio maximo de un P4 autenticado. Flex OS manda un ACK cada
         * 250 ms y un PING cada 15 s mientras la sesion esta lista, asi que
         * 30 s sin un solo byte es un P4 que ya no esta (se reinicio, perdio
         * la Wi-Fi): la sesion se da por cerrada y sus pestanas quedan a la
         * espera de la reconexion. Antes eran 60 s.
         */
        private const val IDLE_READ_MS = 30_000
        private const val OP_BINARY = 0x2
        private const val OP_CLOSE = 0x8
        private const val OP_PING = 0x9
        private const val OP_PONG = 0xA
        /**
         * Lo que puede esperar en la cola de salida antes de empezar a descartar
         * FOTOGRAMAS (no mensajes de control). Un fotograma son decenas de KB; con
         * esto caben unos pocos y un P4 lento no hace crecer la memoria del telefono.
         */
        private const val TX_BUDGET_BYTES = 1_500_000
        private val TX_POISON = ByteArray(0)
    }

    private val seq = AtomicInteger(1)
    @Volatile private var authenticated = false
    @Volatile private var closed = false

    // #############################################################
    // ##  NADIE ESCRIBE EN EL SOCKET SALVO SU HILO DE SALIDA
    // ##  ------------------------------------------------------
    // ##  Los cambios de estado de una pestana (`onTabState`), la lista de
    // ##  pestanas (`pushTabs`) y los errores se mandan desde el hilo PRINCIPAL
    // ##  (los callbacks del WebView y `main.post`). Android lanza
    // ##  NetworkOnMainThreadException si el hilo principal toca un socket, y
    // ##  `sendBinary` la tragaba y marcaba la sesion como cerrada: el relay
    // ##  moria en el primer cambio de pagina. Ahora quien llama solo ENCOLA, y
    // ##  escribe `txLoop`. Ver tambien WifiLinkServer.sendRaw.
    // #############################################################
    private val txQueue = LinkedBlockingQueue<ByteArray>()
    private val txBytes = AtomicInteger(0)
    private val writer = Thread({ txLoop() }, "flex-relay-tx").apply { isDaemon = true; start() }

    private fun txLoop() {
        try {
            while (true) {
                val f = txQueue.poll(500, TimeUnit.MILLISECONDS)
                if (f == null) { if (closed) break else continue }
                if (f === TX_POISON) break
                output.write(f)
                output.flush()
                txBytes.addAndGet(-f.size)
            }
        } catch (e: Exception) {
            closed = true
        }
        runCatching { socket.close() }
    }

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
        closed = true
        txQueue.offer(TX_POISON)                // el hilo de salida termina y cierra el socket
        RelayEngine.detach(this)                // solo cuenta si sigue siendo LA sesion
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
        runCatching { socket.soTimeout = IDLE_READ_MS }
        onAuthenticated(this)

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
                  keyframe: Boolean, last: Boolean, frameId: Long, image: ByteArray): Boolean {
        // Un fotograma es lo UNICO que se puede descartar: si el P4 no lee tan
        // deprisa como se captura, el siguiente lo sustituye. Se pide un
        // keyframe para que lo que llegue despues no dependa del descartado.
        if (!closed && txBytes.get() > TX_BUDGET_BYTES) {
            RelayEngine.requestKeyframe(channel)
            return true
        }
        return sendBinary(Fbp.frame(nextSeq(), channel, x, y, w, h, keyframe, last, frameId, image))
    }

    fun sendState(channel: Int, flags: Int, progress: Int, title: String, url: String): Boolean =
        sendBinary(Fbp.state(nextSeq(), channel, flags, progress, title, url))

    fun sendError(channel: Int, code: Int, msg: String): Boolean =
        sendBinary(Fbp.error(nextSeq(), channel, code, msg))

    /** Encola un mensaje FBP. Nunca toca el socket. false = la sesion ya esta cerrada. */
    fun sendBinary(payload: ByteArray): Boolean {
        if (closed) return false
        enqueue(frame(OP_BINARY, payload))
        return true
    }

    private fun enqueue(f: ByteArray) {
        txBytes.addAndGet(f.size)
        txQueue.offer(f)
    }

    /** Cabecera WebSocket + carga, en un solo array listo para escribir. */
    private fun frame(opcode: Int, payload: ByteArray): ByteArray {
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
        val out = ByteArray(head.size + n)
        head.copyInto(out, 0)
        if (n > 0) payload.copyInto(out, head.size)
        return out
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
                OP_PING -> { enqueue(frame(OP_PONG, data)); readFrame() }
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
            // El CLOSE sale por el hilo de salida y DESPUES de lo ya encolado; el
            // veneno hace que el hilo termine y cierre el socket.
            enqueue(frame(OP_CLOSE, payload))
        }
        txQueue.offer(TX_POISON)
    }
}
