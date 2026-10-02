package com.flexos.flexphone.cloud

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.net.Inet4Address
import java.net.Inet6Address
import java.net.InetAddress
import java.time.Instant
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter

/**
 * HTTP/1.1 minimo para Flex Cloud del telefono.
 *
 * Lo que llega aqui viene de la red local, asi que todo esta ACOTADO: cabecera
 * entera <= 16 KB, como mucho 64 cabeceras, linea de peticion corta, nada de
 * cuerpos troceados (el P4 y la web siempre mandan Content-Length), una sola
 * Content-Length, rutas con escapes validos y sin bytes nulos.
 */
class HttpRequest(
    val method: String,
    val path: String,                       // des-escapada, sin la consulta
    val query: Map<String, String>,
    val headers: Map<String, String>,       // nombres en minusculas
    val contentLength: Long,                // -1 = no vino
    val keepAlive: Boolean,
) {
    fun header(name: String): String? = headers[name.lowercase()]
}

class HttpBadRequest(val status: Int, msg: String) : IOException(msg)

object Http {
    const val MAX_HEAD = 16 * 1024
    const val MAX_HEADERS = 64
    const val MAX_QUERY_PARAMS = 64

    /**
     * Lee la siguiente peticion. null = el otro lado cerro (o no mando nada antes
     * del plazo del socket) entre peticiones. Una peticion mal formada lanza
     * [HttpBadRequest] con el estado que corresponde.
     */
    fun readRequest(input: InputStream): HttpRequest? {
        val head = ByteArrayOutputStream(512)
        var state = 0                                   // cuantos de "\r\n\r\n" llevamos
        while (true) {
            val b = try { input.read() } catch (e: java.net.SocketTimeoutException) {
                if (head.size() == 0) return null else throw HttpBadRequest(408, "la peticion no llego entera")
            }
            if (b < 0) { if (head.size() == 0) return null; throw HttpBadRequest(400, "peticion cortada") }
            head.write(b)
            if (head.size() > MAX_HEAD) throw HttpBadRequest(431, "cabeceras demasiado grandes")
            state = when {
                b == '\r'.code && (state == 0 || state == 2) -> state + 1
                b == '\n'.code && (state == 1 || state == 3) -> state + 1
                b == '\r'.code -> 1
                else -> 0
            }
            if (state == 4) break
            // Tolera saltos de linea a secas al principio (algun cliente entre peticiones).
            if (head.size() == 2 && head.toByteArray().let { it[0] == '\r'.code.toByte() && it[1] == '\n'.code.toByte() }) { head.reset(); state = 0 }
        }
        val text = String(head.toByteArray(), Charsets.ISO_8859_1)
        val lines = text.split("\r\n")
        val reqLine = lines[0]
        val parts = reqLine.split(' ')
        if (parts.size != 3 || reqLine.length > 2048) throw HttpBadRequest(400, "linea de peticion no valida")
        val (method, target, version) = parts
        if (!Regex("^[A-Z]{3,7}$").matches(method)) throw HttpBadRequest(400, "metodo no valido")
        if (version != "HTTP/1.1" && version != "HTTP/1.0") throw HttpBadRequest(505, "version no soportada")
        if (!target.startsWith("/")) throw HttpBadRequest(400, "destino no valido")
        val headers = HashMap<String, String>()
        var n = 0
        for (i in 1 until lines.size) {
            val l = lines[i]
            if (l.isEmpty()) continue
            if (l[0] == ' ' || l[0] == '\t') throw HttpBadRequest(400, "cabecera plegada")
            val c = l.indexOf(':')
            if (c <= 0) throw HttpBadRequest(400, "cabecera no valida")
            val name = l.substring(0, c).trim().lowercase()
            if (!Regex("^[a-z0-9!#$%&'*+.^_`|~-]+$").matches(name)) throw HttpBadRequest(400, "nombre de cabecera no valido")
            val value = l.substring(c + 1).trim()
            if (++n > MAX_HEADERS) throw HttpBadRequest(431, "demasiadas cabeceras")
            if (name == "content-length" && headers.containsKey(name) && headers[name] != value) throw HttpBadRequest(400, "Content-Length repetida")
            headers[name] = if (headers.containsKey(name) && name != "content-length") headers[name] + ", " + value else value
        }
        if (headers.containsKey("transfer-encoding")) throw HttpBadRequest(411, "cuerpos troceados no admitidos")
        val cl = headers["content-length"]?.let {
            if (!Regex("^\\d{1,15}$").matches(it)) throw HttpBadRequest(400, "Content-Length no valida")
            it.toLong()
        } ?: -1L
        val q = target.indexOf('?')
        val rawPath = if (q >= 0) target.substring(0, q) else target
        val rawQuery = if (q >= 0) target.substring(q + 1) else ""
        val path = decodePath(rawPath)
        val conn = headers["connection"]?.lowercase() ?: ""
        val keep = if (version == "HTTP/1.1") !conn.contains("close") else conn.contains("keep-alive")
        return HttpRequest(method, path, parseQuery(rawQuery), headers, cl, keep)
    }

    fun decodePath(raw: String): String {
        val out = ByteArrayOutputStream(raw.length)
        var i = 0
        while (i < raw.length) {
            val c = raw[i]
            if (c == '%') {
                if (i + 2 >= raw.length) throw HttpBadRequest(400, "escape incompleto")
                val v = (Character.digit(raw[i + 1], 16) shl 4) or Character.digit(raw[i + 2], 16)
                if (Character.digit(raw[i + 1], 16) < 0 || Character.digit(raw[i + 2], 16) < 0) throw HttpBadRequest(400, "escape no valido")
                if (v == 0) throw HttpBadRequest(400, "byte nulo en la ruta")
                out.write(v)
                i += 3
            } else {
                if (c.code < 0x21 || c.code > 0x7e) throw HttpBadRequest(400, "caracter no valido en la ruta")
                out.write(c.code)
                i++
            }
        }
        return String(out.toByteArray(), Charsets.UTF_8)
    }

    fun parseQuery(raw: String): Map<String, String> {
        val m = LinkedHashMap<String, String>()
        if (raw.isEmpty()) return m
        val pairs = raw.split('&')
        if (pairs.size > MAX_QUERY_PARAMS) throw HttpBadRequest(400, "demasiados parametros")
        for (p in pairs) {
            if (p.isEmpty()) continue
            val e = p.indexOf('=')
            val k = formDecode(if (e >= 0) p.substring(0, e) else p)
            val v = formDecode(if (e >= 0) p.substring(e + 1) else "")
            m[k] = v
        }
        return m
    }

    fun formDecode(s: String): String = try {
        java.net.URLDecoder.decode(s, "UTF-8")
    } catch (e: IllegalArgumentException) {
        throw HttpBadRequest(400, "consulta mal escapada")
    }

    /** Un solo rango "bytes=a-b", "bytes=a-" o "bytes=-n". null = servir entero. */
    fun parseRange(header: String?, size: Long): LongArray? {
        if (header == null) return null
        val m = Regex("^bytes=(\\d*)-(\\d*)$").matchEntire(header.trim()) ?: return null
        val a = m.groupValues[1]
        val b = m.groupValues[2]
        if (a.isEmpty() && b.isEmpty()) return null
        if (a.length > 18 || b.length > 18) throw E.range(size)
        val start: Long
        val end: Long
        if (a.isEmpty()) {
            val n = b.toLong()
            if (n == 0L) throw E.range(size)
            start = maxOf(0L, size - n); end = size - 1
        } else {
            start = a.toLong()
            end = if (b.isEmpty()) size - 1 else minOf(b.toLong(), size - 1)
        }
        if (start > end || start >= size) throw E.range(size)
        return longArrayOf(start, end)
    }

    fun httpDate(ms: Long): String = DateTimeFormatter.RFC_1123_DATE_TIME.format(Instant.ofEpochMilli(ms).atZone(ZoneOffset.UTC))

    fun statusText(s: Int): String = when (s) {
        200 -> "OK"; 201 -> "Created"; 204 -> "No Content"; 206 -> "Partial Content"; 304 -> "Not Modified"
        400 -> "Bad Request"; 401 -> "Unauthorized"; 403 -> "Forbidden"; 404 -> "Not Found"; 405 -> "Method Not Allowed"
        408 -> "Request Timeout"; 409 -> "Conflict"; 410 -> "Gone"; 411 -> "Length Required"; 413 -> "Payload Too Large"
        416 -> "Range Not Satisfiable"; 422 -> "Unprocessable Entity"; 429 -> "Too Many Requests"; 431 -> "Request Header Fields Too Large"
        500 -> "Internal Server Error"; 503 -> "Service Unavailable"; 505 -> "HTTP Version Not Supported"; 507 -> "Insufficient Storage"
        else -> "Status"
    }

    /** Escribe estado y cabeceras (ISO-8859-1; los valores ya vienen escapados). */
    fun writeHead(out: OutputStream, status: Int, headers: List<Pair<String, String>>) {
        val sb = StringBuilder(256)
        sb.append("HTTP/1.1 ").append(status).append(' ').append(statusText(status)).append("\r\n")
        for ((k, v) in headers) {
            require(!k.contains('\r') && !k.contains('\n') && !v.contains('\r') && !v.contains('\n')) { "cabecera con salto de linea" }
            sb.append(k).append(": ").append(v).append("\r\n")
        }
        sb.append("\r\n")
        out.write(sb.toString().toByteArray(Charsets.ISO_8859_1))
    }

    /** Solo red local: privadas, enlace local, loopback y CGNAT (punto de acceso del movil). */
    fun isLocal(a: InetAddress?): Boolean {
        if (a == null) return false
        if (a.isLoopbackAddress || a.isLinkLocalAddress || a.isSiteLocalAddress) return true
        if (a is Inet4Address) {
            val b = a.address
            val o0 = b[0].toInt() and 0xFF
            val o1 = b[1].toInt() and 0xFF
            return o0 == 100 && o1 in 64..127           // 100.64.0.0/10
        }
        if (a is Inet6Address) {
            val b0 = a.address[0].toInt() and 0xFF
            return (b0 and 0xFE) == 0xFC                 // fc00::/7 (ULA)
        }
        return false
    }
}

/** Cuerpo de una peticion: exactamente Content-Length bytes del socket, ni uno mas. */
class BoundedInput(private val inp: InputStream, private var left: Long) : InputStream() {
    val remaining: Long get() = left
    override fun read(): Int {
        if (left <= 0) return -1
        val r = inp.read()
        if (r >= 0) left-- else left = 0
        return r
    }
    override fun read(b: ByteArray, off: Int, len: Int): Int {
        if (left <= 0) return -1
        val r = inp.read(b, off, minOf(len.toLong(), left).toInt())
        if (r > 0) left -= r else if (r < 0) left = 0
        return r
    }
    /** Lee el resto si es poco (para poder reutilizar la conexion). false = demasiado: hay que cerrar. */
    fun drain(max: Long = 64 * 1024): Boolean {
        if (left > max) return false
        val buf = ByteArray(8192)
        while (left > 0) if (read(buf, 0, buf.size) < 0) return false
        return true
    }
    fun readAllBounded(max: Int): ByteArray {
        if (left > max) throw E.payloadTooLarge()
        val out = ByteArrayOutputStream(left.toInt().coerceAtLeast(0))
        val buf = ByteArray(8192)
        while (left > 0) {
            val r = read(buf, 0, buf.size)
            if (r < 0) throw IOException("cuerpo cortado")
            out.write(buf, 0, r)
        }
        return out.toByteArray()
    }
}
