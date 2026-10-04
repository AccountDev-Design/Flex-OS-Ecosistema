package com.flexos.flexphone.cloud

import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.IOException
import java.io.OutputStream
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketException
import java.util.concurrent.Semaphore
import java.util.concurrent.atomic.AtomicBoolean

/** Quien es este telefono (lo que se ensena en /me y en /api/fs/hello). */
class PhoneInfo(val phoneId: String, val name: String, val model: String)

/**
 * SERVIDOR DE FLEX CLOUD DEL TELEFONO.
 *
 * Habla la API de Flex Cloud (`/api/cloud/...`, el mismo contrato que el
 * servicio de Flex Developer Studio) para que el gestor del P4
 * (`FlexOS_Cloud.cpp`) la use sin cambiar nada de lo que ya sabe hacer, y
 * `/api/fs/...` para abrir sesion con el Flex OS emparejado.
 *
 * QUIEN PUEDE ENTRAR
 *  · Solo la red local ([Http.isLocal]); lo demas se cierra sin contestar.
 *  · `/api/cloud/...` exige `Authorization: Bearer <token>` de una sesion
 *    abierta DESDE ESA MISMA IP con reto-respuesta (clave del emparejamiento).
 *  · Las unicas rutas sin token: `/api/cloud/health` (nada privado),
 *    `/api/fs/hello`, `/api/fs/challenge`, `/api/fs/session` (la propia
 *    autenticacion) y `/api/cloud/d/<enlace firmado>` (la firma ES la
 *    autorizacion: un archivo, 15 minutos, solo lectura).
 *
 * LIMITES: como mucho [maxConnections] conexiones a la vez (la siguiente recibe
 * un 503 al instante, nunca espera colgada), plazos de lectura en cada socket y
 * cuerpos JSON <= 64 KB.
 */
class CloudServer(
    private val cloud: CloudStore,
    private val sessions: SessionManager,
    private val pairing: PairingRepo,
    private val info: () -> PhoneInfo,
    private val maxConnections: Int = 6,
    private val log: (String) -> Unit = {},
) {
    private var server: ServerSocket? = null
    private val running = AtomicBoolean(false)
    private val slots = Semaphore(maxConnections)
    private val open = java.util.Collections.synchronizedSet(HashSet<Socket>())
    @Volatile private var lastMaintenance = 0L
    @Volatile var lastP4Contact = 0L
        private set

    val port: Int get() = server?.localPort ?: 0
    val isRunning: Boolean get() = running.get()

    companion object {
        const val API = "/api/cloud"
        const val JSON_MAX = 64 * 1024
        const val READ_TIMEOUT_MS = 30_000
        const val IDLE_TIMEOUT_MS = 15_000
        const val DEFAULT_PORT = 47830
    }

    /**
     * Arranca en [bind]:[port] (bind = la direccion Wi-Fi del telefono; null =
     * todas, solo para pruebas). Si el puerto esta ocupado y [fallbackAny], usa
     * uno libre. Devuelve el puerto o lanza.
     */
    fun start(bind: InetAddress?, port: Int = DEFAULT_PORT, fallbackAny: Boolean = true): Int {
        if (running.get()) return this.port
        val s = ServerSocket()
        s.reuseAddress = true
        try {
            s.bind(InetSocketAddress(bind, port), 16)
        } catch (e: IOException) {
            if (!fallbackAny) { s.close(); throw e }
            s.bind(InetSocketAddress(bind, 0), 16)
        }
        server = s
        running.set(true)
        Thread({ acceptLoop(s) }, "flex-cloud-srv").apply { isDaemon = true; start() }
        log("Flex Cloud del telefono escuchando en ${s.localPort}")
        return s.localPort
    }

    fun stop() {
        running.set(false)
        try { server?.close() } catch (e: IOException) { /* ya cerrado */ }
        server = null
        synchronized(open) { for (c in open) try { c.close() } catch (e: IOException) { } ; open.clear() }
        sessions.dropAll()
    }

    private fun acceptLoop(s: ServerSocket) {
        while (running.get()) {
            val sock = try { s.accept() } catch (e: IOException) { if (running.get()) log("el servidor dejo de aceptar conexiones"); break }
            if (!Http.isLocal(sock.inetAddress)) { try { sock.close() } catch (e: IOException) { }; continue }
            if (!slots.tryAcquire()) {
                // Lleno: se dice YA (el P4 reintenta con su espera) en vez de dejarlo colgado.
                Thread({ busy(sock) }, "flex-cloud-busy").apply { isDaemon = true; start() }
                continue
            }
            open.add(sock)
            Thread({
                try { serve(sock) } finally { open.remove(sock); slots.release(); lingerClose(sock) }
            }, "flex-cloud-conn").apply { isDaemon = true; start() }
        }
    }

    /**
     * Cierre "con espera": se cierra el sentido de salida y se descarta un poco de
     * lo que el otro lado aun estuviera mandando antes de soltar el socket. Cerrar
     * con datos sin leer hace que el sistema mande un RST, y un RST puede borrar
     * la respuesta de error (el 413, el 431) antes de que el otro lado la lea.
     */
    private fun lingerClose(sock: Socket) {
        try {
            if (!sock.isClosed) {
                sock.shutdownOutput()
                sock.soTimeout = 500
                val inp = sock.getInputStream()
                val buf = ByteArray(8192)
                var left = 256 * 1024
                while (left > 0) { val r = inp.read(buf); if (r < 0) break; left -= r }
            }
        } catch (e: IOException) { /* el otro lado ya se fue */ }
        try { sock.close() } catch (e: IOException) { }
    }

    private fun busy(sock: Socket) {
        try {
            sock.soTimeout = 2000
            val out = sock.getOutputStream()
            val (st, body) = errorBody(E.busy())
            writeJson(out, st, body, false, listOf("Retry-After" to "2"))
            out.flush()
        } catch (e: IOException) { } finally { lingerClose(sock) }
    }

    private fun serve(sock: Socket) {
        sock.tcpNoDelay = true
        val inp = BufferedInputStream(sock.getInputStream(), 64 * 1024)
        val out = BufferedOutputStream(sock.getOutputStream(), 64 * 1024)
        val ip = sock.inetAddress.hostAddress ?: ""
        var first = true
        while (running.get()) {
            sock.soTimeout = if (first) READ_TIMEOUT_MS else IDLE_TIMEOUT_MS
            val req = try {
                Http.readRequest(inp) ?: return
            } catch (e: HttpBadRequest) {
                try { writeJson(out, e.status, mapOf("ok" to false, "error" to mapOf("code" to "invalid_request", "message" to "Petición no válida.")), false); out.flush() } catch (x: IOException) { }
                return
            } catch (e: IOException) { return }
            first = false
            sock.soTimeout = READ_TIMEOUT_MS
            val body = BoundedInput(inp, if (req.contentLength > 0) req.contentLength else 0)
            val ctx = Ctx(req, body, out, ip, sock.localAddress, sock.localPort)
            try {
                handle(ctx)
            } catch (e: CloudError) {
                sendError(ctx, e)
            } catch (e: HttpBadRequest) {
                sendError(ctx, CloudError(e.status, "invalid_request", "Petición no válida."))
            } catch (e: JsonException) {
                sendError(ctx, E.invalid("JSON no válido."))
            } catch (e: SocketException) {
                return
            } catch (e: IOException) {
                return
            } catch (e: Exception) {
                log("fallo en la API: ${e.javaClass.simpleName}")
                sendError(ctx, RuntimeException("interno"))
            }
            try { out.flush() } catch (e: IOException) { return }
            // Lo que no se leyo del cuerpo: si es poco se descarta y la conexion
            // sigue; si es mucho, se cierra (no se leen megas para tirarlos).
            if (!ctx.closeAfter && body.remaining > 0 && !body.drain()) return
            if (ctx.closeAfter || !req.keepAlive) return
        }
    }

    private inner class Ctx(
        val req: HttpRequest, val body: BoundedInput, val out: OutputStream, val ip: String,
        val localAddr: InetAddress, val localPort: Int,
    ) {
        var closeAfter = false
        var headSent = false
    }

    // ------------------------------------------------------------- respuestas
    private fun baseHeaders(api: Boolean): MutableList<Pair<String, String>> {
        val h = mutableListOf(
            "X-Content-Type-Options" to "nosniff",
            "Referrer-Policy" to "no-referrer",
            "X-Frame-Options" to "DENY",
        )
        if (api) h.add("Cache-Control" to "no-store")
        return h
    }

    private fun writeJson(out: OutputStream, status: Int, obj: Map<String, Any?>, keep: Boolean, extra: List<Pair<String, String>> = emptyList()) {
        val bytes = Json.write(obj).toByteArray(Charsets.UTF_8)
        val h = baseHeaders(true)
        h.add("Content-Type" to "application/json; charset=utf-8")
        h.add("Content-Length" to bytes.size.toString())
        h.addAll(extra)
        if (!keep) h.add("Connection" to "close")
        Http.writeHead(out, status, h)
        out.write(bytes)
    }

    private fun ok(c: Ctx, obj: Map<String, Any?>, status: Int = 200) {
        val m = LinkedHashMap<String, Any?>()
        m["ok"] = true
        m.putAll(obj)
        c.headSent = true
        writeJson(c.out, status, m, c.req.keepAlive && !c.closeAfter)
    }

    private fun sendError(c: Ctx, e: Throwable) {
        if (c.headSent) { c.closeAfter = true; return }
        val (status, body) = errorBody(e)
        // Error a mitad de recibir un cuerpo grande: se cierra en vez de leerlo.
        if (c.body.remaining > 64 * 1024) c.closeAfter = true
        val extra = when {
            status == 429 -> listOf("Retry-After" to "60")
            // 416: dice cuanto mide el archivo, como manda la RFC 9110 (los reproductores lo usan para recolocarse).
            status == 416 && e is CloudError -> listOf("Content-Range" to "bytes */${e.details?.get("size") ?: 0}")
            else -> emptyList()
        }
        c.headSent = true
        try { writeJson(c.out, status, body, c.req.keepAlive && !c.closeAfter, extra) } catch (x: IOException) { c.closeAfter = true }
    }

    private fun json(c: Ctx): Map<String, Any?> {
        if (c.req.contentLength > JSON_MAX) throw E.payloadTooLarge()
        if (c.req.contentLength <= 0) return emptyMap()
        val type = (c.req.header("content-type") ?: "").substringBefore(';').trim().lowercase()
        val raw = c.body.readAllBounded(JSON_MAX)
        if (type != "application/json") throw E.invalid("El cuerpo debe ser application/json.")
        return Json.parseObject(String(raw, Charsets.UTF_8))
    }

    // ------------------------------------------------------------- despacho
    private val idRe = "([A-Za-z0-9_]{1,64})"

    private fun handle(c: Ctx) {
        maintenance()
        val p = c.req.path
        val m = c.req.method
        if (p == "/api/fs/hello" && m == "GET") return hello(c)
        if (p == "/api/fs/challenge" && m == "GET") return challenge(c)
        if (p == "/api/fs/session" && m == "POST") return session(c)
        if (!p.startsWith("$API/") && p != API) throw E.notFound("La ruta")
        val r = p.removePrefix(API)
        if (r == "/health" && (m == "GET" || m == "HEAD")) {
            return ok(c, linkedMapOf("service" to "flex-cloud", "impl" to "flexphone", "version" to 1, "time" to System.currentTimeMillis()))
        }
        // Enlace firmado: sin token (la firma es la autorizacion).
        Regex("^/d/([A-Za-z0-9_.-]{10,512})$").matchEntire(r)?.let { mt ->
            if (m != "GET" && m != "HEAD") throw E.methodNotAllowed()
            val f = cloud.verifyLink(mt.groupValues[1])
            return sendFile(c, f, c.req.query["inline"] == "1", crossOrigin = true)
        }
        authenticate(c)
        route(c, r, m)
    }

    private fun authenticate(c: Ctx) {
        if (sessions.blocked(c.ip)) throw E.rateLimited()
        val auth = c.req.header("authorization") ?: ""
        val token = if (auth.regionMatches(0, "Bearer ", 0, 7, ignoreCase = true)) auth.substring(7).trim() else null
        if (sessions.validate(token, c.ip) == null) {
            sessions.fail(c.ip)
            throw E.authRequired()
        }
        lastP4Contact = System.currentTimeMillis()
    }

    private fun route(c: Ctx, r: String, m: String) {
        var mt: MatchResult?
        when {
            r == "/me" && m == "GET" -> {
                val i = info()
                return ok(c, linkedMapOf(
                    "account" to linkedMapOf("id" to "phone:${i.phoneId}", "flexAddress" to i.name, "displayName" to i.name),
                    "device" to linkedMapOf("id" to i.phoneId, "model" to i.model, "kind" to "phone"),
                    "quota" to cloud.quota(),
                    "limits" to linkedMapOf(
                        "maxFileBytes" to cloud.cfg.maxFileBytes, "chunkDefault" to cloud.cfg.chunkDefault,
                        "chunkMin" to cloud.cfg.chunkMin, "chunkMax" to cloud.cfg.chunkMax,
                    ),
                ))
            }
            r == "/quota" && m == "GET" -> return ok(c, mapOf("quota" to cloud.quota()))
            r == "/files" && m == "GET" -> return ok(c, cloud.list(c.req.query))
            r == "/folders" && m == "POST" -> return ok(c, mapOf("folder" to cloud.createFolder(json(c))), 201)
            r == "/trash/empty" && m == "POST" -> { val x = LinkedHashMap(cloud.emptyTrash()); x["quota"] = cloud.quota(); return ok(c, x) }
            r == "/trash/restore" && m == "POST" -> {
                val id = json(c)["id"] as? String ?: throw E.invalid("Falta id.")
                return ok(c, cloud.restore(if (Names.ID_FOLDER.matches(id)) "folder" else "file", id))
            }
            r == "/uploads" && m == "GET" -> return ok(c, mapOf("uploads" to cloud.listUploads()))
            r == "/uploads" && m == "POST" -> {
                val u = cloud.createUpload(json(c), "device")
                return ok(c, mapOf("upload" to u), if (u["resumed"] == true) 200 else 201)
            }
        }
        mt = Regex("^/files/$idRe$").matchEntire(r)
        if (mt != null) {
            val id = mt.groupValues[1]
            when (m) {
                "GET" -> return ok(c, mapOf("file" to cloud.getFile(id)))
                "PATCH" -> return ok(c, mapOf("file" to cloud.updateFile(id, json(c))))
                "DELETE" -> {
                    if (c.req.query["permanent"] == "1") return ok(c, cloud.permanentDelete("file", id))
                    cloud.trash("file", id)
                    return ok(c, linkedMapOf("trashed" to true, "quota" to cloud.quota()))
                }
                else -> throw E.methodNotAllowed()
            }
        }
        mt = Regex("^/files/$idRe/(restore|permanent-delete|thumbnail|link|playable|prepare)$").matchEntire(r)
        if (mt != null) {
            val id = mt.groupValues[1]
            when (mt.groupValues[2] to m) {
                "restore" to "POST" -> return ok(c, cloud.restore("file", id))
                "permanent-delete" to "POST" -> { val x = LinkedHashMap(cloud.permanentDelete("file", id)); x["quota"] = cloud.quota(); return ok(c, x) }
                "thumbnail" to "PUT" -> {
                    val mime = (c.req.header("content-type") ?: "").substringBefore(';').trim().lowercase()
                    if (c.req.contentLength > cloud.cfg.thumbMaxBytes) throw E.payloadTooLarge()
                    return ok(c, cloud.setThumbnail(id, c.body.readAllBounded(cloud.cfg.thumbMaxBytes), mime))
                }
                "thumbnail" to "GET", "thumbnail" to "HEAD" -> return sendThumb(c, id)
                "playable" to "GET", "playable" to "HEAD" -> return sendFile(c, cloud.playableDownload(id), true, crossOrigin = false)
                "prepare" to "POST" -> return ok(c, mapOf("file" to cloud.retryPrepare(id)))
                "link" to "POST" -> {
                    val (token, exp) = cloud.signLink(id)
                    val host = c.localAddr.hostAddress?.let { if (it.contains(':')) "[$it]" else it } ?: "127.0.0.1"
                    val path = "$API/d/$token"
                    return ok(c, linkedMapOf("url" to "http://$host:${c.localPort}$path", "path" to path, "expiresAt" to exp))
                }
                else -> throw E.methodNotAllowed()
            }
        }
        mt = Regex("^/download/$idRe$").matchEntire(r)
        if (mt != null) {
            if (m != "GET" && m != "HEAD") throw E.methodNotAllowed()
            return sendFile(c, cloud.fileForDownload(mt.groupValues[1]), c.req.query["inline"] == "1", crossOrigin = false)
        }
        mt = Regex("^/folders/$idRe$").matchEntire(r)
        if (mt != null) {
            val id = mt.groupValues[1]
            when (m) {
                "GET" -> return ok(c, mapOf("folder" to cloud.getFolder(id)))
                "PATCH" -> return ok(c, mapOf("folder" to cloud.updateFolder(id, json(c))))
                "DELETE" -> {
                    if (c.req.query["permanent"] == "1") return ok(c, cloud.permanentDelete("folder", id))
                    val x = LinkedHashMap<String, Any?>(); x["trashed"] = true; x.putAll(cloud.trash("folder", id))
                    return ok(c, x)
                }
                else -> throw E.methodNotAllowed()
            }
        }
        mt = Regex("^/folders/$idRe/(restore|permanent-delete)$").matchEntire(r)
        if (mt != null && m == "POST") {
            val id = mt.groupValues[1]
            if (mt.groupValues[2] == "restore") return ok(c, cloud.restore("folder", id))
            val x = LinkedHashMap(cloud.permanentDelete("folder", id)); x["quota"] = cloud.quota()
            return ok(c, x)
        }
        mt = Regex("^/uploads/$idRe(/status)?$").matchEntire(r)
        if (mt != null) {
            val id = mt.groupValues[1]
            when (m) {
                "GET" -> return ok(c, mapOf("upload" to cloud.uploadStatus(id)))
                "DELETE" -> return ok(c, linkedMapOf("upload" to cloud.abortUpload(id), "quota" to cloud.quota()))
                else -> throw E.methodNotAllowed()
            }
        }
        mt = Regex("^/uploads/$idRe/parts/(\\d{1,6})$").matchEntire(r)
        if (mt != null && (m == "PUT" || m == "POST")) return part(c, mt.groupValues[1], mt.groupValues[2].toInt())
        mt = Regex("^/uploads/$idRe/complete$").matchEntire(r)
        if (mt != null && m == "POST") {
            val u = cloud.completeUpload(mt.groupValues[1], json(c))
            return ok(c, linkedMapOf("upload" to u, "file" to u["file"], "quota" to cloud.quota()))
        }
        throw E.notFound("La ruta")
    }

    private fun part(c: Ctx, id: String, n: Int) {
        val declared = partDigest(c.req)
        if (c.req.contentLength < 0) throw E.lengthRequired()
        val plan = cloud.planPart(id, n, declared, c.req.contentLength)
        if (plan.already != null) return ok(c, plan.already)     // no se lee el cuerpo: ya la tiene
        ok(c, cloud.receivePart(id, n, declared!!, c.body, plan))
    }

    /** SHA-256 declarado de una parte: X-Part-SHA256 (hex) o Content-Digest (RFC 9530). */
    private fun partDigest(req: HttpRequest): String? {
        req.header("x-part-sha256")?.trim()?.takeIf { it.isNotEmpty() }?.let { return it.lowercase() }
        val cd = req.header("content-digest") ?: return null
        val mt = Regex("sha-256=:([A-Za-z0-9+/=]+):", RegexOption.IGNORE_CASE).find(cd) ?: return null
        return try { StorageCrypto.hex(java.util.Base64.getDecoder().decode(mt.groupValues[1])) } catch (e: IllegalArgumentException) { null }
    }

    // ------------------------------------------------------------ bytes
    private fun sendThumb(c: Ctx, id: String) {
        val t = cloud.thumbnail(id)
        val etag = "\"t-${t.key}\""
        if (c.req.header("if-none-match") == etag) {
            val h = baseHeaders(false)
            h.add("ETag" to etag); h.add("Cache-Control" to "private, max-age=86400"); h.add("Content-Length" to "0")
            c.headSent = true
            Http.writeHead(c.out, 304, h)
            return
        }
        sendObject(c, t.key, t.size, t.mime, null, true, etag, null, extraCache = "private, max-age=86400", crossOrigin = false)
    }

    private fun sendFile(c: Ctx, f: CloudStore.Download, inline: Boolean, crossOrigin: Boolean) {
        val safeInline = inline && Names.inlineSafe(f.mime)
        sendObject(c, f.storageKey, f.size, f.mime, f.name, safeInline, "\"${f.sha256}\"", f.updatedAt, null, crossOrigin)
    }

    private fun sendObject(
        c: Ctx, key: String, size: Long, mime: String, name: String?, inline: Boolean, etag: String,
        lastModified: Long?, extraCache: String?, crossOrigin: Boolean,
    ) {
        val ifRange = c.req.header("if-range")
        var range = if (ifRange == null || ifRange == etag) Http.parseRange(c.req.header("range"), size) else null
        if (size == 0L) range = null
        val h = baseHeaders(false)
        h.add("Accept-Ranges" to "bytes")
        h.add("ETag" to etag)
        if (lastModified != null) h.add("Last-Modified" to Http.httpDate(lastModified))
        // Un archivo del usuario nunca se interpreta como pagina de este origen.
        h.add("Content-Security-Policy" to "default-src 'none'; sandbox")
        // Los enlaces firmados los usa la pagina del P4 (otro origen) en <img> y <video>.
        h.add("Cross-Origin-Resource-Policy" to if (crossOrigin) "cross-origin" else "same-origin")
        h.add("Content-Type" to if (inline) mime else if (Names.inlineSafe(mime)) mime else "application/octet-stream")
        if (name != null) {
            h.add("Content-Disposition" to Names.contentDisposition(name, inline))
            h.add("Cache-Control" to "private, no-cache")
        } else if (extraCache != null) h.add("Cache-Control" to extraCache)
        val status: Int
        val start: Long
        val end: Long
        if (range != null) {
            status = 206; start = range[0]; end = range[1]
            h.add("Content-Range" to "bytes $start-$end/$size")
        } else {
            status = 200; start = 0; end = size - 1
        }
        val len = if (size == 0L) 0L else end - start + 1
        h.add("Content-Length" to len.toString())
        if (!c.req.keepAlive) h.add("Connection" to "close")
        c.headSent = true
        Http.writeHead(c.out, status, h)
        if (c.req.method == "HEAD" || len == 0L) return
        cloud.openRead(key, start, end).use { s ->
            val buf = ByteArray(64 * 1024)
            var left = len
            while (left > 0) {
                val r = s.read(buf, 0, minOf(buf.size.toLong(), left).toInt())
                if (r < 0) { c.closeAfter = true; return }      // el objeto se acorto: la conexion no se reutiliza
                c.out.write(buf, 0, r)
                left -= r
            }
        }
    }

    // ------------------------------------------------------------ sesion
    private fun hello(c: Ctx) {
        val i = info()
        val p = pairing.load()
        ok(c, linkedMapOf("service" to "flex-storage", "phoneId" to i.phoneId, "name" to i.name, "model" to i.model,
            "paired" to (p != null), "port" to c.localPort))
    }

    private fun challenge(c: Ctx) {
        if (sessions.blocked(c.ip)) throw E.rateLimited()
        ok(c, linkedMapOf("nonce" to sessions.newChallenge(), "expiresIn" to 60, "phoneId" to info().phoneId))
    }

    private fun session(c: Ctx) {
        if (sessions.blocked(c.ip)) throw E.rateLimited()
        val b = json(c)
        val p4Id = b["p4Id"] as? String
        val nonce = b["nonce"] as? String
        val mac = StorageCrypto.unhex(b["mac"] as? String, 32)
        val pr = pairing.load()
        if (p4Id == null || nonce == null || mac == null || nonce.length != 32) { sessions.fail(c.ip); throw E.invalid("Petición de sesión incompleta.") }
        // El reto se gasta pase lo que pase: un reto no sirve dos veces.
        val fresh = sessions.consumeChallenge(nonce)
        if (pr == null || pr.p4Id != p4Id) { sessions.fail(c.ip); throw E.unknownDevice() }
        if (!fresh || !StorageCrypto.equalsConstantTime(StorageCrypto.sessionMac(pr.key, nonce, p4Id), mac)) {
            sessions.fail(c.ip)
            throw E.authRequired()
        }
        val s = sessions.create(c.ip)
        lastP4Contact = System.currentTimeMillis()
        log("sesion abierta con ${pr.p4Name}")
        ok(c, linkedMapOf(
            "token" to s.token, "expiresIn" to SessionManager.IDLE_MS / 1000,
            "mac" to StorageCrypto.hex(StorageCrypto.sessionOk(pr.key, nonce, s.token)),
            "phoneId" to info().phoneId, "name" to info().name,
        ))
    }

    // ------------------------------------------------------------ mantenimiento
    private fun maintenance() {
        val t = System.currentTimeMillis()
        if (t - lastMaintenance < 60_000) return
        lastMaintenance = t
        try { cloud.expireUploads(); cloud.purgeTrash() } catch (e: Exception) { log("mantenimiento: ${e.javaClass.simpleName}") }
    }
}
