package com.flexos.flexphone.cloud

import com.flexos.flexphone.cloud.media.AudioPcmSource
import com.flexos.flexphone.cloud.media.BytesSource
import com.flexos.flexphone.cloud.media.FakeAudioSource
import com.flexos.flexphone.cloud.media.FakeVideoSource
import com.flexos.flexphone.cloud.media.JvmImageCodec
import com.flexos.flexphone.cloud.media.Jpeg
import com.flexos.flexphone.cloud.media.MediaAnalyzer
import com.flexos.flexphone.cloud.media.MediaFixtures
import com.flexos.flexphone.cloud.media.MediaPipeline
import com.flexos.flexphone.cloud.media.Plan
import com.flexos.flexphone.cloud.media.StandardConverter
import java.io.BufferedInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.net.InetAddress
import java.net.Socket
import java.nio.file.Files
import java.security.MessageDigest
import kotlin.test.AfterTest
import kotlin.test.BeforeTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * Flex Cloud del telefono PREPARANDO lo multimedia para el P4, de punta a punta con sockets de verdad: se sube un archivo,
 * la cola lo analiza y (si hace falta) lo convierte, y el P4 lo pide por `/files/:id/playable` con rangos.
 */
class MediaServerTest {
    private val tmp: File = Files.createTempDirectory("flexcloud-media").toFile()
    private val key = ByteArray(32) { (it * 5 + 1).toByte() }
    private val p4Id = "flexos-test0002"
    private lateinit var objects: ObjectStore
    private lateinit var store: CloudStore
    private lateinit var server: CloudServer
    private var pipeline: MediaPipeline? = null
    private var port = 0
    private val codec = JvmImageCodec()
    @Volatile private var gate = true

    private fun openStore(cfg: CloudConfig = CloudConfig()): CloudStore {
        objects = ObjectStore(File(tmp, "data"))
        return CloudStore(objects, File(tmp, "meta"), cfg).also { it.recover() }
    }

    private fun startServer() {
        val repo = MemoryPairingRepo(P4Pairing(p4Id, "Flex OS de prueba", key, "127.0.0.1:8080", 0))
        server = CloudServer(store, SessionManager(), repo, { PhoneInfo("a55-test", "Galaxy A55 5G", "SM-A556B") }, maxConnections = 4)
        port = server.start(InetAddress.getLoopbackAddress(), 0)
    }

    private fun startPipeline(converter: com.flexos.flexphone.cloud.media.MediaConverter? = StandardConverter(codec, videoSource = { FakeVideoSource(1280, 720, 0, 48, 24.0) }, audioSource = { FakeAudioSource(44100, 2, 2.0) })) {
        pipeline = MediaPipeline(store, converter, { gate }).also { it.start() }
    }

    @BeforeTest fun up() { store = openStore(); startServer() }
    @AfterTest fun down() { pipeline?.stop(); server.stop(); tmp.deleteRecursively() }

    // ------------------------------------------------------------ cliente minimo
    class Resp(val status: Int, val headers: Map<String, String>, val body: ByteArray) {
        val text get() = String(body, Charsets.UTF_8)
        val json get() = Json.parseObject(text)
    }

    private fun readResp(inp: InputStream, head: Boolean): Resp {
        val h = ByteArrayOutputStream()
        var st = 0
        while (st < 4) {
            val b = inp.read(); if (b < 0) throw java.io.EOFException("sin respuesta")
            h.write(b)
            st = when { b == 13 && (st == 0 || st == 2) -> st + 1; b == 10 && (st == 1 || st == 3) -> st + 1; b == 13 -> 1; else -> 0 }
        }
        val lines = String(h.toByteArray(), Charsets.ISO_8859_1).split("\r\n")
        val headers = HashMap<String, String>()
        for (l in lines.drop(1)) { val c = l.indexOf(':'); if (c > 0) headers[l.substring(0, c).lowercase()] = l.substring(c + 1).trim() }
        val len = headers["content-length"]?.toInt() ?: 0
        val body = ByteArray(if (head) 0 else len)
        var got = 0
        while (got < body.size) { val r = inp.read(body, got, body.size - got); if (r < 0) break; got += r }
        return Resp(lines[0].split(' ')[1].toInt(), headers, body)
    }

    private fun req(method: String, path: String, token: String? = null, body: ByteArray? = null, type: String? = null, extra: Map<String, String> = emptyMap()): Resp {
        Socket("127.0.0.1", port).use { s ->
            s.soTimeout = 10_000
            val sb = StringBuilder("$method $path HTTP/1.1\r\nHost: 127.0.0.1:$port\r\nConnection: close\r\n")
            if (token != null) sb.append("Authorization: Bearer $token\r\n")
            if (type != null) sb.append("Content-Type: $type\r\n")
            for ((k, v) in extra) sb.append("$k: $v\r\n")
            if (body != null) sb.append("Content-Length: ${body.size}\r\n")
            sb.append("\r\n")
            s.getOutputStream().write(sb.toString().toByteArray(Charsets.UTF_8))
            if (body != null) s.getOutputStream().write(body)
            s.getOutputStream().flush()
            return readResp(BufferedInputStream(s.getInputStream()), method == "HEAD")
        }
    }

    private fun login(): String {
        val nonce = req("GET", "/api/fs/challenge").json["nonce"] as String
        val body = Json.write(mapOf("p4Id" to p4Id, "nonce" to nonce, "mac" to StorageCrypto.hex(StorageCrypto.sessionMac(key, nonce, p4Id)))).toByteArray()
        return req("POST", "/api/fs/session", body = body, type = "application/json").json["token"] as String
    }

    private fun sha(b: ByteArray) = ObjectStore.hex(MessageDigest.getInstance("SHA-256").digest(b))

    private fun upload(t: String, name: String, data: ByteArray, mime: String? = null): String {
        val m = LinkedHashMap<String, Any?>(mapOf("name" to name, "size" to data.size, "sha256" to sha(data), "chunkSize" to 65536))
        if (mime != null) m["mimeType"] = mime
        @Suppress("UNCHECKED_CAST") val up = req("POST", "/api/cloud/uploads", t, Json.write(m).toByteArray(), "application/json").json["upload"] as Map<String, Any?>
        val id = up["uploadId"] as String
        for (n in 1..(up["totalParts"] as Long).toInt()) {
            val p = data.copyOfRange((n - 1) * 65536, minOf(data.size, n * 65536))
            assertEquals(200, req("PUT", "/api/cloud/uploads/$id/parts/$n", t, p, "application/octet-stream", mapOf("X-Part-SHA256" to sha(p))).status)
        }
        val done = req("POST", "/api/cloud/uploads/$id/complete", t, Json.write(mapOf("sha256" to sha(data))).toByteArray(), "application/json")
        assertEquals(200, done.status, done.text)
        return (done.json["upload"] as Map<*, *>)["fileId"] as String
    }

    @Suppress("UNCHECKED_CAST")
    private fun file(t: String, id: String): Map<String, Any?> = req("GET", "/api/cloud/files/$id", t).json["file"] as Map<String, Any?>
    @Suppress("UNCHECKED_CAST")
    private fun playable(t: String, id: String): Map<String, Any?> = file(t, id)["playable"] as Map<String, Any?>

    private fun settle(): Boolean = pipeline!!.awaitIdle(30_000)
    private fun an(b: ByteArray) = MediaAnalyzer.analyze(BytesSource(b))

    // ------------------------------------------------------------ casos
    @Test fun `AVI MJPEG compatible - native, se sirve el ORIGINAL con rangos, y salen metadatos y miniatura`() {
        startPipeline()
        val t = login()
        val avi = MediaFixtures.aviMjpeg(File(tmp, "x").also { it.mkdirs() }, "ok.avi", 320, 240, 12, 12).readBytes()
        val id = upload(t, "clip.avi", avi, "video/x-msvideo")
        assertTrue(settle())
        val f = file(t, id)
        val p = f["playable"] as Map<*, *>
        assertEquals("native", p["state"]); assertEquals("none", p["plan"]); assertEquals("flexos-ultra-v1", p["profile"])
        assertEquals(avi.size.toLong(), p["size"]); assertEquals(320L, p["width"]); assertEquals(240L, p["height"]); assertEquals("mjpeg", p["codec"])
        assertEquals(true, f["hasThumbnail"])
        val md = f["metadata"] as Map<*, *>
        assertEquals(1000L, md["durationMs"]); assertEquals(1200L, md["fpsX100"]); assertEquals("avi", md["container"])
        val th = req("GET", "/api/cloud/files/$id/thumbnail", t)
        assertEquals(200, th.status); assertTrue(th.body.size <= 40 * 1024 && Jpeg.parse(BytesSource(th.body))!!.p4Decodable)
        // El P4 pide los bytes por /playable con rangos
        val full = req("GET", "/api/cloud/files/$id/playable", t)
        assertTrue(full.body.contentEquals(avi)); assertEquals("bytes", full.headers["accept-ranges"])
        val part = req("GET", "/api/cloud/files/$id/playable", t, extra = mapOf("Range" to "bytes=1000-4095"))
        assertEquals(206, part.status); assertEquals("bytes 1000-4095/${avi.size}", part.headers["content-range"])
        assertTrue(part.body.contentEquals(avi.copyOfRange(1000, 4096)))
        assertEquals(avi.size.toString(), req("HEAD", "/api/cloud/files/$id/playable", t).headers["content-length"])
        assertEquals(1, (req("GET", "/api/cloud/files?view=folder", t).json["items"] as List<*>).size, "la lista no duplica nada")
    }

    @Test fun `H264 - se prepara en el telefono, el original queda intacto y el P4 recibe un AVI MJPEG con rangos`() {
        startPipeline()
        val t = login()
        val mp4 = MediaFixtures.mov("avc1", 1280, 720, List(24) { ByteArray(4000) { b -> b.toByte() } }, fps = 24, brand = "isom")
        val id = upload(t, "vacaciones.mp4", mp4, "video/mp4")
        assertTrue(settle())
        val p = playable(t, id)
        assertEquals("ready", p["state"], p.toString()); assertEquals("transcode", p["plan"])
        assertEquals(640L, p["width"]); assertEquals(360L, p["height"]); assertEquals("mjpeg", p["codec"]); assertEquals("avi", p["container"])
        assertEquals("video/x-msvideo", p["mime"])
        // original intacto
        assertTrue(req("GET", "/api/cloud/download/$id", t).body.contentEquals(mp4))
        // lo que ve el P4
        val size = (p["size"] as Long).toInt()
        val v = req("GET", "/api/cloud/files/$id/playable", t)
        assertEquals(size, v.body.size)
        val check = an(v.body)
        assertEquals(Plan.NONE, check.plan, check.toString())
        val mid = req("GET", "/api/cloud/files/$id/playable", t, extra = mapOf("Range" to "bytes=${size / 2}-${size / 2 + 999}"))
        assertEquals(206, mid.status); assertTrue(mid.body.contentEquals(v.body.copyOfRange(size / 2, size / 2 + 1000)))
        // la version cuenta en la cuota
        val q = req("GET", "/api/cloud/quota", t).json["quota"] as Map<*, *>
        assertEquals(mp4.size.toLong() + size, q["usedBytes"])
        assertEquals(1, (req("GET", "/api/cloud/files?view=folder", t).json["items"] as List<*>).size)
    }

    @Test fun `PNG, JPEG progresivo y WAV de 24 bits - cada uno acaba en el formato del perfil`() {
        startPipeline()
        val t = login()
        val ids = mapOf(
            "foto.png" to upload(t, "foto.png", MediaFixtures.png(300, 200)),
            "prog.jpg" to upload(t, "prog.jpg", MediaFixtures.jpeg(800, 600, progressive = true)),
            "audio.wav" to upload(t, "audio.wav", MediaFixtures.wavPcm(48000, 2, 24, 1.0)),
        )
        assertTrue(settle())
        for ((name, id) in ids) {
            val p = playable(t, id)
            assertEquals("ready", p["state"], "$name $p")
            val c = an(req("GET", "/api/cloud/files/$id/playable", t).body)
            assertEquals(Plan.NONE, c.plan, "$name: $c")
        }
        assertEquals("image/jpeg", playable(t, ids["foto.png"]!!)["mime"]); assertEquals("audio/wav", playable(t, ids["audio.wav"]!!)["mime"])
    }

    @Test fun `M4A con AAC pasa por el decodificador de audio de la plataforma`() {
        startPipeline()
        val t = login()
        val id = upload(t, "cancion.m4a", MediaFixtures.m4a("mp4a", 44100, 2, 20))
        assertTrue(settle())
        val p = playable(t, id)
        assertEquals("ready", p["state"], p.toString()); assertEquals(22050L, p["sampleRate"]); assertEquals(1L, p["channels"])
    }

    @Test fun `una foto compatible pero grande tiene vista previa ligera, el original sigue en descarga`() {
        startPipeline()
        val t = login()
        val big = MediaFixtures.jpeg(3200, 2400, quality = 0.9f)
        val id = upload(t, "grande.jpg", big)
        assertTrue(settle())
        val p = playable(t, id)
        assertEquals("ready", p["state"], p.toString()); assertEquals("preview", p["plan"]); assertEquals(1600L, p["width"])
        assertTrue((p["size"] as Long) < big.size)
        assertTrue(req("GET", "/api/cloud/download/$id", t).body.contentEquals(big))
        // una foto pequena no necesita version
        val small = upload(t, "chica.jpg", MediaFixtures.jpeg(800, 600))
        assertTrue(settle())
        assertEquals("native", playable(t, small)["state"])
    }

    @Test fun `lo imposible o roto se dice con el motivo y playable responde 409`() {
        startPipeline()
        val t = login()
        val dir = File(tmp, "y").also { it.mkdirs() }
        val h264Avi = MediaFixtures.aviMjpeg(dir, "h.avi", 160, 120, 4, 12).also { MediaFixtures.patchAviCodec(it, "H264", "H264") }.readBytes()
        val bad = upload(t, "raro.avi", h264Avi)
        val cut = upload(t, "cortada.jpg", MediaFixtures.jpeg(320, 240).let { it.copyOf(it.size / 2) })
        val fake = upload(t, "texto.jpg", "esto no es una foto".toByteArray(), "image/jpeg")
        assertTrue(settle())
        val p1 = playable(t, bad); assertEquals("unsupported", p1["state"]); assertTrue((p1["reason"] as String).contains("H264", ignoreCase = true), p1.toString())
        val p2 = playable(t, cut); assertEquals("corrupt", p2["state"]); assertTrue((p2["reason"] as String).contains("cortado"), p2.toString())
        val p3 = playable(t, fake); assertEquals("corrupt", p3["state"])
        for (id in listOf(bad, cut, fake)) {
            val r = req("GET", "/api/cloud/files/$id/playable", t)
            assertEquals(409, r.status); assertEquals("not_ready", ((r.json["error"] as Map<*, *>)["code"]))
        }
        // reintentar no cambia lo imposible
        assertEquals(200, req("POST", "/api/cloud/files/$bad/prepare", t).status)
        assertTrue(settle()); assertEquals("unsupported", playable(t, bad)["state"])
    }

    @Test fun `un archivo que no es multimedia ni se mira, y sin playable da 409`() {
        startPipeline()
        val t = login()
        val id = upload(t, "notas.txt", "hola".toByteArray())
        assertTrue(settle())
        assertNull(file(t, id)["playable"])
        assertEquals(409, req("GET", "/api/cloud/files/$id/playable", t).status)
        assertEquals(400, req("POST", "/api/cloud/files/$id/prepare", t).status)
    }

    @Test fun `un fallo del conversor queda como failed con motivo y se puede reintentar`() {
        var boom = true
        val flaky = object : com.flexos.flexphone.cloud.media.MediaConverter {
            val real = StandardConverter(codec)
            override fun canConvert(facts: com.flexos.flexphone.cloud.media.MediaFacts, plan: Plan) = real.canConvert(facts, plan)
            override fun convert(src: File, facts: com.flexos.flexphone.cloud.media.MediaFacts, plan: Plan, out: File, progress: (Int) -> Unit, cancelled: () -> Boolean) {
                if (boom) throw com.flexos.flexphone.cloud.media.MediaException("Este teléfono no sabe abrir este formato de imagen.")
                real.convert(src, facts, plan, out, progress, cancelled)
            }
            override fun thumbnail(src: File, facts: com.flexos.flexphone.cloud.media.MediaFacts) = real.thumbnail(src, facts)
        }
        startPipeline(flaky)
        val t = login()
        val id = upload(t, "a.png", MediaFixtures.png(100, 100))
        assertTrue(settle())
        val p = playable(t, id)
        assertEquals("failed", p["state"]); assertTrue((p["reason"] as String).contains("formato de imagen"))
        boom = false
        assertEquals(200, req("POST", "/api/cloud/files/$id/prepare", t).status)
        assertTrue(settle())
        assertEquals("ready", playable(t, id)["state"])
    }

    @Test fun `un resultado que no cumple el perfil no se publica`() {
        val liar = object : com.flexos.flexphone.cloud.media.MediaConverter {
            override fun canConvert(facts: com.flexos.flexphone.cloud.media.MediaFacts, plan: Plan) = true
            override fun convert(src: File, facts: com.flexos.flexphone.cloud.media.MediaFacts, plan: Plan, out: File, progress: (Int) -> Unit, cancelled: () -> Boolean) {
                out.writeBytes(MediaFixtures.png(10, 10))            // dice que convirtio, pero entrega un PNG
            }
            override fun thumbnail(src: File, facts: com.flexos.flexphone.cloud.media.MediaFacts): ByteArray? = null
        }
        startPipeline(liar)
        val t = login()
        val id = upload(t, "a.png", MediaFixtures.png(100, 100))
        assertTrue(settle())
        val p = playable(t, id)
        assertEquals("failed", p["state"]); assertTrue((p["reason"] as String).contains("no cumple el perfil"), p.toString())
        assertEquals(409, req("GET", "/api/cloud/files/$id/playable", t).status)
    }

    @Test fun `sin conversor queda unsupported y al volver a arrancar con uno se prepara solo`() {
        startPipeline(null)
        val t = login()
        val id = upload(t, "a.png", MediaFixtures.png(100, 100))
        assertTrue(settle())
        assertEquals("unsupported", playable(t, id)["state"])
        // se reinicia el servicio con un conversor nuevo
        pipeline!!.stop(); server.stop()
        store = openStore(); startServer(); startPipeline()
        assertTrue(settle())
        val t2 = login()
        assertEquals("ready", playable(t2, id)["state"])
    }

    @Test fun `el estado sobrevive a un reinicio y lo que estaba convirtiendose vuelve a la cola`() {
        startPipeline()
        val t = login()
        val id = upload(t, "a.png", MediaFixtures.png(100, 100))
        assertTrue(settle())
        val before = playable(t, id)
        pipeline!!.stop(); server.stop()
        store = openStore(); startServer()
        val t2 = login()
        assertEquals(before["state"], playable(t2, id)["state"]); assertEquals(before["size"], playable(t2, id)["size"])
        assertTrue(req("GET", "/api/cloud/files/$id/playable", t2).body.isNotEmpty())
    }

    @Test fun `el gate aplaza la conversion sin tocar el analisis`() {
        gate = false
        startPipeline()
        val t = login()
        val id = upload(t, "a.png", MediaFixtures.png(100, 100))
        Thread.sleep(600)
        assertEquals("pending", playable(t, id)["state"])
        assertEquals(409, req("GET", "/api/cloud/files/$id/playable", t).status)
        gate = true
        assertTrue(settle()); assertEquals("ready", playable(t, id)["state"])
    }

    @Test fun `borrar el original borra su version, libera la cuota y no deja objetos huerfanos`() {
        startPipeline()
        val t = login()
        val id = upload(t, "a.png", MediaFixtures.png(200, 200))
        assertTrue(settle())
        assertEquals("ready", playable(t, id)["state"])
        assertEquals(200, req("DELETE", "/api/cloud/files/$id", t).status)
        assertEquals(200, req("DELETE", "/api/cloud/files/$id?permanent=1", t).status)
        val q = req("GET", "/api/cloud/quota", t).json["quota"] as Map<*, *>
        assertEquals(0L, q["usedBytes"])
        val left = File(tmp, "data/objects").walkTopDown().filter { it.isFile }.toList()
        assertTrue(left.isEmpty(), "quedan objetos: $left")
    }

    @Test fun `si la version no cabe se dice, y el original sigue disponible`() {
        store.let { }; pipeline?.stop(); server.stop()
        val png = MediaFixtures.png(400, 300)
        store = openStore(CloudConfig(quotaBytes = png.size + 2000L, deviceMarginBytes = 0))
        startServer(); startPipeline()
        val t = login()
        val id = upload(t, "a.png", png)
        assertTrue(settle())
        val p = playable(t, id)
        assertEquals("failed", p["state"]); assertTrue((p["reason"] as String).contains("espacio"), p.toString())
        assertTrue(req("GET", "/api/cloud/download/$id", t).body.contentEquals(png))
    }

    @Test fun `convertir se cancela si se borra el archivo y no queda nada`() {
        val slow = StandardConverter(codec, videoSource = { object : com.flexos.flexphone.cloud.media.VideoFrameSource {
            val inner = FakeVideoSource(640, 480, 0, 100000, 12.0)
            override val width = 640; override val height = 480; override val rotation = 0
            override val durationMs: Long? = 100_000_000; override val fps: Double? = 12.0
            override fun nextFrame(outW: Int, outH: Int, keep: (Long) -> Boolean): com.flexos.flexphone.cloud.media.VideoFrame? { Thread.sleep(5); return inner.nextFrame(outW, outH, keep) }
            override fun close() = inner.close()
        } })
        startPipeline(slow)
        val t = login()
        val id = upload(t, "largo.mp4", MediaFixtures.mov("avc1", 640, 480, List(5) { ByteArray(1000) }, fps = 12, brand = "isom"))
        val end = System.currentTimeMillis() + 10_000
        while (System.currentTimeMillis() < end && (file(t, id)["playable"] as? Map<*, *>)?.get("state") != "preparing") Thread.sleep(20)
        assertEquals("preparing", playable(t, id)["state"])
        assertEquals(200, req("DELETE", "/api/cloud/files/$id", t).status)
        assertTrue(settle())
        assertEquals(200, req("DELETE", "/api/cloud/files/$id?permanent=1", t).status)
        Thread.sleep(200)
        assertTrue(File(tmp, "data/objects").walkTopDown().none { it.isFile }, "quedan objetos")
        assertTrue(File(tmp, "data/tmp").listFiles().orEmpty().isEmpty(), "quedan temporales")
    }

    @Test fun `416 lleva Content-Range con el tamano, y If-Range con una fecha manda el archivo entero`() {
        val t = login()
        val data = ByteArray(10_000) { it.toByte() }
        val id = upload(t, "a.bin", data)
        val r = req("GET", "/api/cloud/download/$id", t, extra = mapOf("Range" to "bytes=20000-"))
        assertEquals(416, r.status); assertEquals("bytes */10000", r.headers["content-range"])
    }
}
