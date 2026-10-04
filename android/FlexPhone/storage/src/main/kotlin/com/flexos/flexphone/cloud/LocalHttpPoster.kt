package com.flexos.flexphone.cloud

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.net.ConnectException
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.NoRouteToHostException
import java.net.Socket
import java.net.SocketTimeoutException
import java.net.URLEncoder

/** POST de un formulario. Lanza [HttpStageException] (una IOException) diciendo EN QUE PASO fallo. */
fun interface FormPoster {
    /** Estado HTTP y cuerpo. */
    fun post(url: String, form: Map<String, String>, timeoutMs: Int): Pair<Int, String>
}

/** El paso de la comunicacion telefono -> Flex OS en el que fallo algo. */
enum class HttpStage(val label: String) {
    /** El telefono no llego a abrir el canal TCP: sin ruta, red inalcanzable o sin respuesta. */
    UNREACHABLE("TCP: sin ruta o sin respuesta"),
    /** Llego a la IP, pero el puerto no acepto (nada escucha ahi). */
    REFUSED("TCP: puerto rechazado"),
    /** El canal se abrio, pero Flex OS no contesto a la peticion HTTP. */
    NO_REPLY("HTTP: abierto pero sin respuesta"),
    /** Contesto algo que no es HTTP. */
    BAD_REPLY("HTTP: respuesta no valida"),
    /** La direccion no es de la red local (nunca se intenta). */
    NOT_LOCAL("direccion no local"),
}

class HttpStageException(val stage: HttpStage, val target: String, cause: Throwable? = null) :
    IOException("${stage.label} ($target)" + (cause?.let { " - ${it.javaClass.simpleName}" } ?: ""), cause) {
    /** El pedido NO llego a Flex OS: repetirlo es seguro (una oferta de un solo uso no se gasta). */
    val requestNotDelivered: Boolean get() = stage == HttpStage.UNREACHABLE || stage == HttpStage.REFUSED
}

/**
 * HTTP/1.1 minimo del telefono hacia el servidor web de Flex OS, sobre un [Socket] propio.
 *
 * POR QUE NO `HttpURLConnection`
 * ------------------------------
 * Android 9+ (y esta app apunta a la 35) PROHIBE el HTTP en claro en la pila HTTP
 * del sistema salvo que el manifiesto lo permita, y lo hace lanzando una
 * IOException ("Cleartext HTTP traffic to ... not permitted") ANTES de abrir el
 * socket. El servidor del P4 habla HTTP sin TLS (es una red local y el ESP32 no
 * lleva certificados), asi que `HttpURLConnection` fallaba SIEMPRE en el telefono
 * -- y como cualquier IOException se contaba como "no se pudo hablar con Flex OS",
 * parecia un problema de Wi-Fi con el P4 mostrando el enlace abierto.
 * En la JVM del PC esa regla no existe, por eso ninguna prueba lo vio.
 *
 * Permitir el HTTP en claro para toda la app (`usesCleartextTraffic`) abriria la
 * puerta a cualquier otra conexion futura; Android no deja acotarlo por rango de
 * IP. Aqui el limite es explicito: SOLO se habla con una IPv4 literal de la red
 * local ([Http.isLocal]) y nunca con nombres.
 *
 * [connect] permite al telefono abrir el socket POR LA RED WI-FI aunque Android
 * haya elegido los datos moviles como red predeterminada (una Wi-Fi sin Internet).
 */
class LocalHttpPoster(
    private val newSocket: () -> Socket = { Socket() },
    private val connectTimeoutMs: Int = 4_000,
) : FormPoster {

    override fun post(url: String, form: Map<String, String>, timeoutMs: Int): Pair<Int, String> {
        val m = Regex("^http://(\\d{1,3}(?:\\.\\d{1,3}){3}):(\\d{1,5})(/[\\x21-\\x7e]*)$").matchEntire(url)
            ?: throw HttpStageException(HttpStage.NOT_LOCAL, "url")
        val ip = m.groupValues[1]
        val port = m.groupValues[2].toInt()
        val path = m.groupValues[3]
        val octets = ip.split('.').map { it.toInt() }
        val target = "$ip:$port"
        if (octets.any { it > 255 } || port !in 1..65535) throw HttpStageException(HttpStage.NOT_LOCAL, target)
        val addr = InetAddress.getByAddress(ByteArray(4) { octets[it].toByte() })
        if (!Http.isLocal(addr)) throw HttpStageException(HttpStage.NOT_LOCAL, target)

        val body = form.entries.joinToString("&") { (k, v) -> URLEncoder.encode(k, "UTF-8") + "=" + URLEncoder.encode(v, "UTF-8") }
            .toByteArray(Charsets.UTF_8)
        val head = ("POST $path HTTP/1.1\r\nHost: $target\r\nX-Flex: 1\r\n" +       // X-Flex: la web del P4 lo exige (CSRF)
            "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: ${body.size}\r\n" +
            "Accept-Encoding: identity\r\nConnection: close\r\n\r\n").toByteArray(Charsets.US_ASCII)

        val s = newSocket()
        try {
            try {
                s.connect(InetSocketAddress(addr, port), connectTimeoutMs)
            } catch (e: SocketTimeoutException) { throw HttpStageException(HttpStage.UNREACHABLE, target, e)
            } catch (e: NoRouteToHostException) { throw HttpStageException(HttpStage.UNREACHABLE, target, e)
            } catch (e: ConnectException) {
                // "Connection refused" = la IP respondio y el puerto no; el resto (red inalcanzable, tiempo) = sin ruta.
                val refused = e.message?.contains("refused", ignoreCase = true) == true
                throw HttpStageException(if (refused) HttpStage.REFUSED else HttpStage.UNREACHABLE, target, e)
            } catch (e: IOException) { throw HttpStageException(HttpStage.UNREACHABLE, target, e) }
            s.soTimeout = timeoutMs
            s.tcpNoDelay = true
            val status: Int
            val text: String
            try {
                s.getOutputStream().apply { write(head); write(body); flush() }
                val r = readResponse(s.getInputStream())
                status = r.first; text = r.second
            } catch (e: HttpStageException) { throw e
            } catch (e: IOException) { throw HttpStageException(HttpStage.NO_REPLY, target, e) }
            return status to text
        } finally {
            runCatching { s.close() }
        }
    }

    private fun readResponse(input: InputStream): Pair<Int, String> {
        val head = ByteArrayOutputStream(512)
        var state = 0
        while (true) {
            val b = input.read()
            if (b < 0) throw HttpStageException(HttpStage.NO_REPLY, "respuesta vacia")
            head.write(b)
            if (head.size() > 16 * 1024) throw HttpStageException(HttpStage.BAD_REPLY, "cabeceras enormes")
            state = when {
                b == '\r'.code && (state == 0 || state == 2) -> state + 1
                b == '\n'.code && (state == 1 || state == 3) -> state + 1
                b == '\r'.code -> 1
                else -> 0
            }
            if (state == 4) break
        }
        val lines = String(head.toByteArray(), Charsets.ISO_8859_1).split("\r\n")
        val sl = Regex("^HTTP/1\\.[01] (\\d{3})(?: .*)?$").matchEntire(lines[0]) ?: throw HttpStageException(HttpStage.BAD_REPLY, "linea de estado")
        val status = sl.groupValues[1].toInt()
        var len = -1L
        for (i in 1 until lines.size) {
            val c = lines[i].indexOf(':')
            if (c > 0 && lines[i].substring(0, c).trim().equals("content-length", ignoreCase = true))
                len = lines[i].substring(c + 1).trim().toLongOrNull() ?: throw HttpStageException(HttpStage.BAD_REPLY, "Content-Length")
            if (c > 0 && lines[i].substring(0, c).trim().equals("transfer-encoding", ignoreCase = true))
                throw HttpStageException(HttpStage.BAD_REPLY, "cuerpo troceado")
        }
        val cap = 64 * 1024
        val out = ByteArrayOutputStream()
        val buf = ByteArray(4096)
        while (out.size() < cap && (len < 0 || out.size() < len)) {
            val want = if (len < 0) buf.size else minOf(buf.size.toLong(), len - out.size()).toInt()
            val r = input.read(buf, 0, want)
            if (r < 0) break
            out.write(buf, 0, minOf(r, cap - out.size()))
        }
        return status to String(out.toByteArray(), Charsets.UTF_8)
    }
}
