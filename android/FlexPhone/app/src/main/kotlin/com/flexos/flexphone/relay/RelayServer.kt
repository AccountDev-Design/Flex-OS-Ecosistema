package com.flexos.flexphone.relay

import android.content.Context
import android.util.Log
import com.flexos.flexphone.protocol.Fbp
import com.flexos.flexphone.protocol.FbpWriter
import java.io.InputStream
import java.io.OutputStream
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.security.MessageDigest
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Servidor del Browser Relay.
 *
 * Habla EXACTAMENTE lo mismo que el servicio Ubuntu/PC:
 *   · `GET /v1/health`   -> JSON, para que Flex OS compruebe que
 *                           esto esta vivo ANTES de decir "conectado"
 *   · `GET /v1/version`  -> JSON con la version del protocolo
 *   · `/v1/session`      -> WebSocket, subprotocolo `fbp.v1`,
 *                           tramas binarias
 *
 * Asi el navegador del P4 no necesita ni una linea nueva, y el
 * servidor Ubuntu sigue funcionando igual que siempre.
 *
 * QUIEN PUEDE CONECTAR
 * --------------------
 * Solo se escucha en la interfaz local del Wi-Fi, y el HELLO tiene
 * que traer el token de sesion que se negocio POR BLE. Un dispositivo
 * que no haya emparejado no tiene ese token, asi que aunque llegue al
 * puerto se le cierra la conexion.
 *
 * Sobre TLS: aqui se sirve en claro dentro de la red local, igual que
 * hace el modo de desarrollo del servidor Ubuntu. El token va en el
 * HELLO y el enlace de control (BLE) SI esta cifrado por bonding. La
 * interfaz de Flex OS lo dice: "Sin TLS (red local)". No se anuncia
 * como cifrado algo que no lo esta.
 */
class RelayServer(
    private val ctx: Context,
    private val sessionToken: String,
    private val onEvent: (Event) -> Unit,
) {
    sealed class Event {
        data class Listening(val address: ByteArray, val port: Int) : Event()
        data class Error(val message: String) : Event()
        object ClientConnected : Event()
        object ClientGone : Event()
    }

    companion object {
        private const val TAG = "FlexPhone/RelaySrv"
        private const val WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
        /** Tope de un mensaje entrante. Un HELLO son decenas de bytes. */
        private const val MAX_INBOUND = 64 * 1024
    }

    private var server: ServerSocket? = null
    private val running = AtomicBoolean(false)
    private var thread: Thread? = null
    @Volatile private var session: RelaySession? = null

    val port: Int get() = server?.localPort ?: 0

    fun start(preferredPort: Int): Boolean {
        if (running.get()) return true
        return try {
            // 0 = que el sistema elija un puerto libre. Es el valor de
            // fabrica: fijar uno a mano solo sirve para chocar con otra
            // app.
            server = ServerSocket(preferredPort)
            running.set(true)
            thread = Thread({ acceptLoop() }, "flex-relay").apply { isDaemon = true; start() }
            val addr = localWifiAddress()
            if (addr == null) {
                onEvent(Event.Error("el telefono no esta en una red Wi-Fi"))
                stop()
                return false
            }
            onEvent(Event.Listening(addr, server!!.localPort))
            true
        } catch (e: Exception) {
            onEvent(Event.Error("no se pudo abrir el puerto"))
            false
        }
    }

    fun stop() {
        running.set(false)
        runCatching { session?.close() }
        session = null
        runCatching { server?.close() }
        server = null
        thread = null
    }

    fun isRunning(): Boolean = running.get()

    /** Envia un mensaje FBP a la sesion activa, si la hay. */
    fun send(frame: ByteArray): Boolean = session?.sendBinary(frame) ?: false

    private fun acceptLoop() {
        while (running.get()) {
            val sock = try {
                server?.accept() ?: break
            } catch (e: Exception) {
                if (running.get()) onEvent(Event.Error("el servidor dejo de aceptar conexiones"))
                break
            }
            // UNA sesion a la vez: el relay sirve a un solo Flex OS.
            // Una segunda conexion se cierra en vez de repartir los
            // fotogramas entre dos.
            if (session != null) { runCatching { sock.close() }; continue }
            Thread({ serve(sock) }, "flex-relay-conn").apply { isDaemon = true; start() }
        }
    }

    private fun serve(sock: Socket) {
        sock.soTimeout = 60_000
        val input = sock.getInputStream()
        val output = sock.getOutputStream()
        try {
            val req = readHttpRequest(input) ?: return
            when {
                req.path == "/v1/health" -> respondJson(output, health())
                req.path == "/v1/version" -> respondJson(output, """{"protocol":1,"impl":"flexphone-relay"}""")
                req.path == "/v1/session" -> upgrade(sock, req, input, output)
                else -> respond(output, "404 Not Found", "text/plain", "no existe")
            }
        } catch (e: Exception) {
            // Nada de trazas con contenido: solo el hecho.
            Log.w(TAG, "conexion terminada")
        } finally {
            if (session?.socket !== sock) runCatching { sock.close() }
        }
    }

    private fun health(): String {
        val s = session
        return """{"ok":true,"protocol":1,"impl":"flexphone-relay",""" +
               """"tabs":${RelayEngine.tabCount()},"busy":${s != null}}"""
    }

    // ---------------------------------------------------------
    //  HTTP minimo
    // ---------------------------------------------------------
    private data class Request(val method: String, val path: String, val headers: Map<String, String>)

    private fun readHttpRequest(input: InputStream): Request? {
        val line = StringBuilder()
        val headers = HashMap<String, String>()
        var first: String? = null
        var count = 0
        while (true) {
            val c = input.read()
            if (c < 0) return null
            if (c == '\n'.code) {
                val s = line.toString().trim()
                line.setLength(0)
                if (s.isEmpty()) break
                if (first == null) first = s
                else {
                    val i = s.indexOf(':')
                    if (i > 0) headers[s.substring(0, i).trim().lowercase()] = s.substring(i + 1).trim()
                }
                if (++count > 64) return null           // cabeceras absurdas: se corta
            } else if (c != '\r'.code) {
                line.append(c.toChar())
                if (line.length > 2048) return null
            }
        }
        val parts = first?.split(' ') ?: return null
        if (parts.size < 2) return null
        return Request(parts[0], parts[1].substringBefore('?'), headers)
    }

    private fun respondJson(out: OutputStream, body: String) =
        respond(out, "200 OK", "application/json", body)

    private fun respond(out: OutputStream, status: String, type: String, body: String) {
        val b = body.toByteArray(Charsets.UTF_8)
        val head = "HTTP/1.1 $status\r\nContent-Type: $type; charset=utf-8\r\n" +
                   "Content-Length: ${b.size}\r\nConnection: close\r\n\r\n"
        out.write(head.toByteArray(Charsets.US_ASCII))
        out.write(b)
        out.flush()
    }

    // ---------------------------------------------------------
    //  WebSocket
    // ---------------------------------------------------------
    private fun upgrade(sock: Socket, req: Request, input: InputStream, output: OutputStream) {
        val key = req.headers["sec-websocket-key"]
        if (req.headers["upgrade"]?.lowercase() != "websocket" || key == null) {
            respond(output, "400 Bad Request", "text/plain", "se esperaba un WebSocket")
            return
        }
        val accept = MessageDigest.getInstance("SHA-1")
            .digest((key + WS_GUID).toByteArray(Charsets.US_ASCII))
            .let { android.util.Base64.encodeToString(it, android.util.Base64.NO_WRAP) }

        val head = StringBuilder("HTTP/1.1 101 Switching Protocols\r\n")
            .append("Upgrade: websocket\r\nConnection: Upgrade\r\n")
            .append("Sec-WebSocket-Accept: $accept\r\n")
        // El subprotocolo se confirma SOLO si el cliente lo pidio.
        if (req.headers["sec-websocket-protocol"]?.contains("fbp.v1") == true)
            head.append("Sec-WebSocket-Protocol: fbp.v1\r\n")
        head.append("\r\n")
        output.write(head.toString().toByteArray(Charsets.US_ASCII))
        output.flush()

        val s = RelaySession(sock, input, output, sessionToken, onEvent)
        session = s
        onEvent(Event.ClientConnected)
        try {
            s.loop()
        } finally {
            session = null
            onEvent(Event.ClientGone)
            runCatching { sock.close() }
        }
    }

    /** Direccion IPv4 del telefono en la red local. */
    private fun localWifiAddress(): ByteArray? {
        return runCatching {
            java.net.NetworkInterface.getNetworkInterfaces().toList()
                .filter { it.isUp && !it.isLoopback }
                .flatMap { it.inetAddresses.toList() }
                .firstOrNull { it is java.net.Inet4Address && it.isSiteLocalAddress }
                ?.address
        }.getOrNull()
    }
}
