package com.flexos.flexphone.cloud

import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.nio.file.Files
import java.security.MessageDigest
import kotlin.test.AfterTest
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

/**
 * El servidor de Flex Cloud del telefono por SOCKETS DE VERDAD: autenticacion
 * por reto-respuesta, token ligado a la IP, la API de Flex Cloud con rangos y
 * partes, enlaces firmados, limites y peticiones hostiles.
 */
class ServerTest {
    private val tmp: File = Files.createTempDirectory("flexcloud-srv").toFile()
    private val key = ByteArray(32) { (it * 7 + 3).toByte() }
    private val p4Id = "flexos-test0001"
    private lateinit var store: CloudStore
    private lateinit var server: CloudServer
    private var port = 0

    @BeforeTest fun up() {
        store = CloudStore(ObjectStore(File(tmp, "data")), File(tmp, "meta"))
        store.recover()
        val repo = MemoryPairingRepo(P4Pairing(p4Id, "Flex OS de prueba", key, "127.0.0.1:8080", 0))
        server = CloudServer(store, SessionManager(), repo, { PhoneInfo("a55-test", "Galaxy A55 5G", "SM-A556B") }, maxConnections = 4)
        port = server.start(InetAddress.getLoopbackAddress(), 0)
    }

    @AfterTest fun down() { server.stop(); tmp.deleteRecursively() }

    // ------------------------------------------------------------ cliente minimo
    class Resp(val status: Int, val headers: Map<String, String>, val body: ByteArray) {
        val text get() = String(body, Charsets.UTF_8)
        val json get() = Json.parseObject(text)
    }

    private fun connect(from: String? = null): Socket {
        val s = Socket()
        if (from != null) s.bind(InetSocketAddress(from, 0))
        s.connect(InetSocketAddress("127.0.0.1", port), 3000)
        s.soTimeout = 10_000
        return s
    }

    private fun readResp(inp: InputStream, head: Boolean = false): Resp {
        val h = ByteArrayOutputStream()
        var st = 0
        while (st < 4) {
            val b = inp.read()
            if (b < 0) throw java.io.EOFException("sin respuesta")
            h.write(b)
            st = when {
                b == 13 && (st == 0 || st == 2) -> st + 1
                b == 10 && (st == 1 || st == 3) -> st + 1
                b == 13 -> 1
                else -> 0
            }
        }
        val lines = String(h.toByteArray(), Charsets.ISO_8859_1).split("\r\n")
        val status = lines[0].split(' ')[1].toInt()
        val headers = HashMap<String, String>()
        for (l in lines.drop(1)) { val c = l.indexOf(':'); if (c > 0) headers[l.substring(0, c).lowercase()] = l.substring(c + 1).trim() }
        val len = headers["content-length"]?.toInt() ?: 0
        val body = ByteArray(if (head) 0 else len)
        var got = 0
        while (got < body.size) { val r = inp.read(body, got, body.size - got); if (r < 0) break; got += r }
        return Resp(status, headers, body)
    }

    private fun req(method: String, path: String, token: String? = null, body: ByteArray? = null, type: String? = null,
                    extra: Map<String, String> = emptyMap(), from: String? = null): Resp {
        connect(from).use { s ->
            val out = s.getOutputStream()
            val sb = StringBuilder("$method $path HTTP/1.1\r\nHost: 127.0.0.1:$port\r\nConnection: close\r\n")
            if (token != null) sb.append("Authorization: Bearer $token\r\n")
            if (type != null) sb.append("Content-Type: $type\r\n")
            for ((k, v) in extra) sb.append("$k: $v\r\n")
            if (body != null) sb.append("Content-Length: ${body.size}\r\n")
            sb.append("\r\n")
            out.write(sb.toString().toByteArray(Charsets.UTF_8))
            if (body != null) out.write(body)
            out.flush()
            return readResp(BufferedInputStream(s.getInputStream()), method == "HEAD")
        }
    }

    private fun json(method: String, path: String, token: String?, obj: Map<String, Any?>) =
        req(method, path, token, Json.write(obj).toByteArray(), "application/json")

    private fun login(from: String? = null): String {
        val ch = req("GET", "/api/fs/challenge", from = from).json
        val nonce = ch["nonce"] as String
        val mac = StorageCrypto.hex(StorageCrypto.sessionMac(key, nonce, p4Id))
        val r = connect(from).use { s ->
            val body = Json.write(mapOf("p4Id" to p4Id, "nonce" to nonce, "mac" to mac)).toByteArray()
            s.getOutputStream().write(("POST /api/fs/session HTTP/1.1\r\nHost: x\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: ${body.size}\r\n\r\n").toByteArray() + body)
            readResp(BufferedInputStream(s.getInputStream()))
        }
        assertEquals(200, r.status, r.text)
        val token = r.json["token"] as String
        // El telefono demuestra que TAMBIEN tiene la clave (autenticacion mutua).
        assertEquals(StorageCrypto.hex(StorageCrypto.sessionOk(key, nonce, token)), r.json["mac"])
        return token
    }

    private fun sha(b: ByteArray) = ObjectStore.hex(MessageDigest.getInstance("SHA-256").digest(b))

    private fun uploadFile(token: String, name: String, data: ByteArray): Map<String, Any?> {
        val u = json("POST", "/api/cloud/uploads", token, mapOf("name" to name, "size" to data.size, "sha256" to sha(data), "chunkSize" to 65536)).json
        @Suppress("UNCHECKED_CAST") val up = u["upload"] as Map<String, Any?>
        val id = up["uploadId"] as String
        val parts = (up["totalParts"] as Long).toInt()
        for (n in 1..parts) {
            val p = data.copyOfRange((n - 1) * 65536, minOf(data.size, n * 65536))
            val r = req("PUT", "/api/cloud/uploads/$id/parts/$n", token, p, "application/octet-stream", mapOf("X-Part-SHA256" to sha(p)))
            assertEquals(200, r.status, r.text)
        }
        val done = json("POST", "/api/cloud/uploads/$id/complete", token, mapOf("sha256" to sha(data)))
        assertEquals(200, done.status, done.text)
        @Suppress("UNCHECKED_CAST")
        return done.json["file"] as Map<String, Any?>
    }

    // ------------------------------------------------------------ pruebas
    @Test fun `sin sesion no hay Flex Cloud`() {
        assertEquals(200, req("GET", "/api/cloud/health").status)
        val r = req("GET", "/api/cloud/files")
        assertEquals(401, r.status); assertEquals("auth_required", (r.json["error"] as Map<*, *>)["code"])
        assertEquals(401, req("GET", "/api/cloud/files", token = "0".repeat(48)).status)
        val hello = req("GET", "/api/fs/hello").json
        assertEquals("a55-test", hello["phoneId"]); assertEquals(true, hello["paired"])
    }

    @Test fun `sesion por reto-respuesta, de un solo uso y ligada a la IP`() {
        val token = login()
        assertEquals(200, req("GET", "/api/cloud/files", token).status)
        // Otra IP de la misma red no puede usar el token aunque lo haya escuchado.
        assertEquals(401, req("GET", "/api/cloud/files", token, from = "127.0.0.2").status)
        // Un reto ya usado no abre otra sesion; un MAC con otra clave tampoco.
        val nonce = req("GET", "/api/fs/challenge").json["nonce"] as String
        val wrong = StorageCrypto.hex(StorageCrypto.sessionMac(ByteArray(32), nonce, p4Id))
        assertEquals(401, json("POST", "/api/fs/session", null, mapOf("p4Id" to p4Id, "nonce" to nonce, "mac" to wrong)).status)
        val good = StorageCrypto.hex(StorageCrypto.sessionMac(key, nonce, p4Id))
        assertEquals(401, json("POST", "/api/fs/session", null, mapOf("p4Id" to p4Id, "nonce" to nonce, "mac" to good)).status, "el reto ya se gasto")
        // Un Flex OS que no es el emparejado.
        val n2 = req("GET", "/api/fs/challenge").json["nonce"] as String
        val r = json("POST", "/api/fs/session", null, mapOf("p4Id" to "otro", "nonce" to n2, "mac" to StorageCrypto.hex(StorageCrypto.sessionMac(key, n2, "otro"))))
        assertEquals(403, r.status); assertEquals("device_revoked", (r.json["error"] as Map<*, *>)["code"])
    }

    @Test fun `diez fallos seguidos bloquean a esa IP un minuto`() {
        repeat(10) { req("GET", "/api/cloud/files", token = "1".repeat(48), from = "127.0.0.3") }
        assertEquals(429, req("GET", "/api/cloud/files", token = "1".repeat(48), from = "127.0.0.3").status)
        assertEquals(429, req("GET", "/api/fs/challenge", from = "127.0.0.3").status)
        // Las demas IP siguen funcionando.
        assertEquals(200, req("GET", "/api/fs/challenge").status)
    }

    @Test fun `la API de Flex Cloud entera - carpetas, partes, me, cuota, papelera`() {
        val t = login()
        val me = req("GET", "/api/cloud/me", t).json
        assertEquals("Galaxy A55 5G", (me["account"] as Map<*, *>)["flexAddress"])
        assertEquals(5L shl 30, (me["quota"] as Map<*, *>)["totalBytes"])
        val f = json("POST", "/api/cloud/folders", t, mapOf("name" to "Música"))
        assertEquals(201, f.status)
        val fid = (f.json["folder"] as Map<*, *>)["id"] as String
        val data = ByteArray(150_000) { (it % 251).toByte() }
        val file = uploadFile(t, "canción.wav", data)
        assertEquals(sha(data), file["sha256"])
        val mv = json("PATCH", "/api/cloud/files/${file["id"]}", t, mapOf("parentId" to fid, "name" to "Canción.wav"))
        assertEquals(200, mv.status, mv.text)
        val list = req("GET", "/api/cloud/files?parentId=$fid", t).json
        @Suppress("UNCHECKED_CAST")
        assertEquals(listOf("Canción.wav"), (list["items"] as List<Map<String, Any?>>).map { it["name"] })
        @Suppress("UNCHECKED_CAST")
        assertEquals(listOf("Música"), ((list["folder"] as Map<String, Any?>)["path"] as List<Map<String, Any?>>).map { it["name"] })
        assertEquals(200, req("DELETE", "/api/cloud/files/${file["id"]}", t).status)
        assertEquals(150_000L, (req("GET", "/api/cloud/quota", t).json["quota"] as Map<*, *>)["trashBytes"])
        assertEquals(200, req("POST", "/api/cloud/files/${file["id"]}/restore", t).status)
        assertEquals(200, req("DELETE", "/api/cloud/folders/$fid", t).status)
        val emptied = req("POST", "/api/cloud/trash/empty", t)
        assertEquals(150_000L, emptied.json["freedBytes"])
        assertEquals(0L, (emptied.json["quota"] as Map<*, *>)["usedBytes"])
    }

    @Test fun `descarga con rangos, If-Range, HEAD y 416`() {
        val t = login()
        val data = ByteArray(200_000) { (it * 13).toByte() }
        val file = uploadFile(t, "video.avi", data)
        val id = file["id"]
        val full = req("GET", "/api/cloud/download/$id", t)
        assertEquals(200, full.status); assertTrue(full.body.contentEquals(data))
        assertEquals("bytes", full.headers["accept-ranges"]); assertEquals("\"${sha(data)}\"", full.headers["etag"])
        assertEquals("default-src 'none'; sandbox", full.headers["content-security-policy"])
        val part = req("GET", "/api/cloud/download/$id", t, extra = mapOf("Range" to "bytes=1000-1999"))
        assertEquals(206, part.status); assertEquals("bytes 1000-1999/200000", part.headers["content-range"])
        assertTrue(part.body.contentEquals(data.copyOfRange(1000, 2000)))
        val tail = req("GET", "/api/cloud/download/$id", t, extra = mapOf("Range" to "bytes=-500"))
        assertEquals(206, tail.status); assertTrue(tail.body.contentEquals(data.copyOfRange(199_500, 200_000)))
        val open = req("GET", "/api/cloud/download/$id", t, extra = mapOf("Range" to "bytes=199000-"))
        assertEquals(1000, open.body.size)
        // Otra version (If-Range que no cuadra): se manda entero, nunca se mezclan versiones.
        val other = req("GET", "/api/cloud/download/$id", t, extra = mapOf("Range" to "bytes=10-20", "If-Range" to "\"otra\""))
        assertEquals(200, other.status); assertEquals(200_000, other.body.size)
        assertEquals(416, req("GET", "/api/cloud/download/$id", t, extra = mapOf("Range" to "bytes=300000-")).status)
        val head = req("HEAD", "/api/cloud/download/$id", t)
        assertEquals(200, head.status); assertEquals("200000", head.headers["content-length"]); assertEquals(0, head.body.size)
        assertEquals(404, req("GET", "/api/cloud/download/fil_${"a".repeat(24)}", t).status)
    }

    @Test fun `enlace firmado directo para el navegador, sin token`() {
        val t = login()
        val data = ByteArray(5000) { 7 }
        val file = uploadFile(t, "foto.jpg", data)
        val link = req("POST", "/api/cloud/files/${file["id"]}/link", t).json
        val url = link["url"] as String
        assertTrue(url.startsWith("http://127.0.0.1:$port/api/cloud/d/"), url)
        val path = link["path"] as String
        val r = req("GET", "$path?inline=1", from = "127.0.0.9")            // otro equipo de la red (el navegador)
        assertEquals(200, r.status); assertTrue(r.body.contentEquals(data))
        assertEquals("cross-origin", r.headers["cross-origin-resource-policy"])
        assertTrue(r.headers["content-disposition"]!!.startsWith("inline;"))
        assertEquals(403, req("GET", path.dropLast(3) + "abc").status)
    }

    @Test fun `parte con otra longitud - error y la conexion se cierra sin leer de mas`() {
        val t = login()
        val u = json("POST", "/api/cloud/uploads", t, mapOf("name" to "a.bin", "size" to 100_000, "chunkSize" to 65536)).json
        val id = (u["upload"] as Map<*, *>)["uploadId"]
        val r = req("PUT", "/api/cloud/uploads/$id/parts/1", t, ByteArray(1000), "application/octet-stream", mapOf("X-Part-SHA256" to "a".repeat(64)))
        assertEquals(400, r.status); assertEquals("part_size_mismatch", (r.json["error"] as Map<*, *>)["code"])
        val noSha = req("PUT", "/api/cloud/uploads/$id/parts/1", t, ByteArray(65536), "application/octet-stream")
        assertEquals(400, noSha.status)
    }

    @Test fun `peticiones hostiles - linea rota, cabeceras enormes, troceado, Content-Length doble`() {
        fun raw(bytes: ByteArray): Int = connect().use { s ->
            s.getOutputStream().write(bytes)
            try { readResp(BufferedInputStream(s.getInputStream())).status } catch (e: java.io.IOException) { -1 }
        }
        assertEquals(400, raw("GARBAGE\r\n\r\n".toByteArray()))
        assertEquals(431, raw(("GET / HTTP/1.1\r\nX: " + "a".repeat(20_000) + "\r\n\r\n").toByteArray()))
        assertEquals(411, raw("POST /api/cloud/uploads HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n".toByteArray()))
        assertEquals(400, raw("POST /api/fs/session HTTP/1.1\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\nhello!".toByteArray()))
        assertEquals(400, raw("GET /api/cloud/%00 HTTP/1.1\r\n\r\n".toByteArray()))
        assertEquals(505, raw("GET / HTTP/2.0\r\n\r\n".toByteArray()))
        // JSON enorme: 413 sin leerlo.
        val t = login()
        val big = req("POST", "/api/cloud/folders", t, ByteArray(70_000) { 'a'.code.toByte() }, "application/json")
        assertEquals(413, big.status)
        assertEquals(400, req("POST", "/api/cloud/folders", t, "{no es json".toByteArray(), "application/json").status)
        assertEquals(404, req("GET", "/nada", t).status)
        assertEquals(405, req("PUT", "/api/cloud/files/fil_${"a".repeat(24)}", t).status)
    }

    @Test fun `varias peticiones por conexion (keep-alive) y limite de conexiones`() {
        val t = login()
        connect().use { s ->
            val out = s.getOutputStream()
            val inp = BufferedInputStream(s.getInputStream())
            repeat(3) {
                out.write("GET /api/cloud/quota HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer $t\r\n\r\n".toByteArray())
                assertEquals(200, readResp(inp).status)
            }
        }
        // Cuatro conexiones ociosas ocupan los cuatro huecos: la quinta recibe un 503 al instante.
        Thread.sleep(300)                                 // que se suelten los huecos de las anteriores
        val idle = (1..4).map { connect() }
        Thread.sleep(200)
        val fifth = connect()
        val r = readResp(BufferedInputStream(fifth.getInputStream()))
        assertEquals(503, r.status); assertEquals("2", r.headers["retry-after"])
        fifth.close()
        idle.forEach { it.close() }
        Thread.sleep(300)
        assertEquals(200, req("GET", "/api/cloud/health").status)
    }

    @Test fun `solo red local`() {
        assertTrue(Http.isLocal(InetAddress.getByName("192.168.1.20")))
        assertTrue(Http.isLocal(InetAddress.getByName("10.0.0.2")))
        assertTrue(Http.isLocal(InetAddress.getByName("172.20.10.2")))
        assertTrue(Http.isLocal(InetAddress.getByName("100.64.1.1")))
        assertTrue(Http.isLocal(InetAddress.getByName("fe80::1")))
        assertFalse(Http.isLocal(InetAddress.getByName("8.8.8.8")))
        assertFalse(Http.isLocal(InetAddress.getByName("2001:4860::1")))
        assertNotNull(AttachClient.parseHost("192.168.1.50:8080"))
        assertEquals(null, AttachClient.parseHost("8.8.8.8:8080"))
        assertEquals(null, AttachClient.parseHost("p4.local:8080"))
        assertEquals(null, AttachClient.parseHost("192.168.1.300:8080"))
        assertEquals(null, AttachClient.parseHost("192.168.1.5:99999"))
    }
}
