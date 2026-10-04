package com.flexos.flexphone.cloud

import java.io.IOException
import java.net.ConnectException
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.NoRouteToHostException
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketAddress
import java.net.SocketTimeoutException
import kotlin.concurrent.thread
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlin.test.fail

/**
 * El telefono hablando con Flex OS por HTTP: el cliente de sockets propio ([LocalHttpPoster]),
 * los errores POR PASO y la red local por subred ([Lan]).
 *
 * Es la prueba del fallo real: en Android 9+ `HttpURLConnection` no deja usar HTTP en claro, asi
 * que "Activar Flex Cloud" fallaba siempre con la Wi-Fi bien y el enlace abierto. Aqui el cliente
 * habla contra un servidor de verdad y se comprueba, ademas, que no usa la pila HTTP del sistema.
 */
class LocalHttpTest {

    /** Un servidor HTTP de una sola peticion, con la respuesta que se quiera. Devuelve lo recibido. */
    private class OneShot(val reply: (ByteArray) -> ByteArray?, val readBody: Boolean = true) : AutoCloseable {
        val server = ServerSocket(0, 1, InetAddress.getByName("127.0.0.1"))
        val port get() = server.localPort
        @Volatile var request = ""
        private val t = thread(isDaemon = true) {
            try {
                server.accept().use { c ->
                    c.soTimeout = 5_000
                    val inp = c.getInputStream()
                    val buf = java.io.ByteArrayOutputStream()
                    var st = 0
                    while (st < 4) {
                        val b = inp.read(); if (b < 0) break
                        buf.write(b)
                        st = when { b == 13 && (st == 0 || st == 2) -> st + 1; b == 10 && (st == 1 || st == 3) -> st + 1; b == 13 -> 1; else -> 0 }
                    }
                    val head = String(buf.toByteArray(), Charsets.ISO_8859_1)
                    val cl = Regex("(?i)content-length: *(\\d+)").find(head)?.groupValues?.get(1)?.toInt() ?: 0
                    val body = if (readBody) ByteArray(cl).also { var o = 0; while (o < cl) { val r = inp.read(it, o, cl - o); if (r < 0) break; o += r } } else ByteArray(0)
                    request = head + String(body, Charsets.UTF_8)
                    val out = reply(body)
                    if (out != null) { c.getOutputStream().write(out); c.getOutputStream().flush() }
                    else Thread.sleep(1_500)               // sin respuesta: se queda callado
                }
            } catch (_: Exception) {}
        }
        override fun close() { runCatching { server.close() } }
    }

    private fun http(status: String, body: String, extra: String = "") =
        ("HTTP/1.1 $status\r\nContent-Type: application/json\r\nContent-Length: ${body.toByteArray().size}\r\n$extra" +
            "Connection: close\r\n\r\n$body").toByteArray()

    // ------------------------------------------------------------------ el cliente
    @Test fun `POST de formulario - cabeceras, cuerpo y respuesta con Content-Length`() {
        OneShot({ http("202 Accepted", "{\"pairId\":\"ab\"}") }).use { s ->
            val (st, txt) = LocalHttpPoster().post("http://127.0.0.1:${s.port}/api/fs/phone/pair", linkedMapOf("offer" to "o1", "name" to "A55 ñ&=x"), 3000)
            assertEquals(202, st); assertEquals("{\"pairId\":\"ab\"}", txt)
            assertTrue(s.request.startsWith("POST /api/fs/phone/pair HTTP/1.1\r\n"), s.request)
            assertTrue(s.request.contains("X-Flex: 1\r\n"), "la web del P4 exige X-Flex en todo lo que cambia algo")
            assertTrue(s.request.contains("Host: 127.0.0.1:${s.port}\r\n"))
            assertTrue(s.request.endsWith("offer=o1&name=A55+%C3%B1%26%3Dx"), s.request)
        }
    }

    @Test fun `respuesta sin Content-Length - se lee hasta que cierra`() {
        OneShot({ "HTTP/1.0 200 OK\r\n\r\n{\"state\":\"pending\"}".toByteArray() }).use { s ->
            val (st, txt) = LocalHttpPoster().post("http://127.0.0.1:${s.port}/x", mapOf("a" to "b"), 3000)
            assertEquals(200, st); assertEquals("{\"state\":\"pending\"}", txt)
        }
    }

    @Test fun `un 403 con cuerpo de error se devuelve, no es una excepcion`() {
        OneShot({ http("403 Forbidden", "{\"error\":\"La oferta no vale o ya se uso\"}") }).use { s ->
            val (st, txt) = LocalHttpPoster().post("http://127.0.0.1:${s.port}/x", mapOf("a" to "b"), 3000)
            assertEquals(403, st); assertTrue(txt.contains("oferta"))
        }
    }

    // ------------------------------------------------------------------ errores POR PASO
    private fun stageOf(block: () -> Unit): HttpStage {
        try { block() } catch (e: HttpStageException) { return e.stage }
        fail("se esperaba un HttpStageException")
    }

    @Test fun `puerto cerrado en una IP que responde da REFUSED`() {
        val port = ServerSocket(0, 1, InetAddress.getByName("127.0.0.1")).use { it.localPort }   // se cierra: nadie escucha
        assertEquals(HttpStage.REFUSED, stageOf { LocalHttpPoster().post("http://127.0.0.1:$port/x", mapOf("a" to "b"), 1000) })
    }

    private class FailingSocket(val err: IOException) : Socket() {
        override fun connect(endpoint: SocketAddress?, timeout: Int) { throw err }
    }

    @Test fun `sin ruta, red inalcanzable o sin respuesta al conectar da UNREACHABLE`() {
        for (e in listOf(SocketTimeoutException("connect timed out"), NoRouteToHostException("No route to host"), ConnectException("Network is unreachable"))) {
            assertEquals(HttpStage.UNREACHABLE, stageOf { LocalHttpPoster({ FailingSocket(e) }).post("http://192.168.1.4:80/x", mapOf("a" to "b"), 1000) }, e.toString())
        }
    }

    @Test fun `el canal abre pero Flex OS no contesta (cierra o calla) da NO_REPLY`() {
        OneShot({ ByteArray(0) }).use { s ->      // cierra sin decir nada
            assertEquals(HttpStage.NO_REPLY, stageOf { LocalHttpPoster().post("http://127.0.0.1:${s.port}/x", mapOf("a" to "b"), 1000) })
        }
        OneShot({ null }).use { s ->               // se queda callado: vence el plazo de lectura
            assertEquals(HttpStage.NO_REPLY, stageOf { LocalHttpPoster().post("http://127.0.0.1:${s.port}/x", mapOf("a" to "b"), 300) })
        }
    }

    @Test fun `lo que contesta no es HTTP o es troceado da BAD_REPLY`() {
        OneShot({ "SSH-2.0-OpenSSH\r\n\r\n".toByteArray() }).use { s ->
            assertEquals(HttpStage.BAD_REPLY, stageOf { LocalHttpPoster().post("http://127.0.0.1:${s.port}/x", mapOf("a" to "b"), 1000) })
        }
        OneShot({ "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n".toByteArray() }).use { s ->
            assertEquals(HttpStage.BAD_REPLY, stageOf { LocalHttpPoster().post("http://127.0.0.1:${s.port}/x", mapOf("a" to "b"), 1000) })
        }
    }

    @Test fun `solo IPv4 de la red local por literal - nada de nombres ni de Internet`() {
        for (u in listOf("http://8.8.8.8:80/x", "http://p4.local:80/x", "https://192.168.1.4:80/x", "http://192.168.1.300:80/x",
                         "http://192.168.1.4:99999/x", "http://192.168.1.4/x", "http://1.2.3.4:80/x")) {
            assertEquals(HttpStage.NOT_LOCAL, stageOf { LocalHttpPoster({ fail("no debe ni abrir un socket: $u") }).post(u, mapOf("a" to "b"), 1000) }, u)
        }
    }

    @Test fun `el reintento es seguro solo si el pedido no llego`() {
        assertTrue(HttpStageException(HttpStage.UNREACHABLE, "t").requestNotDelivered)
        assertTrue(HttpStageException(HttpStage.REFUSED, "t").requestNotDelivered)
        assertFalse(HttpStageException(HttpStage.NO_REPLY, "t").requestNotDelivered)
        assertFalse(HttpStageException(HttpStage.BAD_REPLY, "t").requestNotDelivered)
    }

    // ------------------------------------------------------------------ AttachClient: que dice cada fallo
    private val phone = PhoneInfo("a55-0f1e2d3c4b5a6978", "Galaxy A55 5G", "SM-A556B")

    private class Scripted(val steps: MutableList<() -> Pair<Int, String>>) : FormPoster {
        var calls = 0
        override fun post(url: String, form: Map<String, String>, timeoutMs: Int): Pair<Int, String> { calls++; return steps.removeAt(0)() }
    }

    private fun attach(p: FormPoster, hint: (String) -> String? = { null }, sleeps: MutableList<Long> = ArrayList()) =
        AttachClient(phone, 47830, MemoryPairingRepo(), p, { 0L }, { sleeps.add(it) }, hint)
            .attach("192.168.1.4:80", "a".repeat(32), { _, _ -> })

    @Test fun `cada paso que falla dice lo que fallo, con la IP, y no es siempre el mismo mensaje`() {
        fun msg(stage: HttpStage): String {
            val r = attach(Scripted(MutableList(5) { { throw HttpStageException(stage, "192.168.1.4:80") } }))
            assertTrue(r is AttachClient.Result.Failed, r.toString())
            assertTrue(r.detail.isNotEmpty(), "el detalle tecnico acompana al mensaje")
            return r.message
        }
        val all = HttpStage.values().filter { it != HttpStage.NOT_LOCAL }.map { msg(it) }
        assertEquals(all.size, all.toSet().size, "mensajes distintos por paso: $all")
        for (m in all) assertTrue(m.contains("192.168.1.4:80"), m)
        assertTrue(msg(HttpStage.UNREACHABLE).contains("aísla"), "sin pista: el aislamiento de clientes del router")
        assertTrue(msg(HttpStage.REFUSED).contains("servidor web"))
    }

    @Test fun `fallo antes de llegar - se reintenta y se empareja sin gastar la oferta`() {
        val p4 = AttachTest.FakeP4().apply { approveOnPoll = 1 }
        val offer = p4.newOffer()
        var fails = 2
        val poster = FormPoster { url, form, t ->
            if (url.endsWith("/api/fs/phone/pair") && fails-- > 0) throw HttpStageException(HttpStage.UNREACHABLE, "x")
            p4.post(url.replace("192.168.1.4:80", "192.168.1.4:8080"), form, t)
        }
        val sleeps = ArrayList<Long>()
        val r = AttachClient(phone, 47830, MemoryPairingRepo(), poster, { 0L }, { sleeps.add(it) }).attach("192.168.1.4:80", offer, { _, _ -> })
        assertTrue(r is AttachClient.Result.Paired, r.toString())
        assertEquals(listOf(AttachClient.CONNECT_RETRY_MS, AttachClient.CONNECT_RETRY_MS), sleeps.take(2))
    }

    @Test fun `fallo despues de mandar el pedido - NO se repite (la oferta pudo gastarse)`() {
        val s = Scripted(MutableList(5) { { throw HttpStageException(HttpStage.NO_REPLY, "x") } })
        val r = attach(s)
        assertEquals(1, s.calls)
        assertTrue(r is AttachClient.Result.Failed && r.message.contains("no contestó"), r.toString())
    }

    @Test fun `tres intentos sin llegar y se explica, la pista de subred se anade`() {
        val s = Scripted(MutableList(5) { { throw HttpStageException(HttpStage.UNREACHABLE, "x") } })
        val r = attach(s, hint = { ip -> "PISTA para $ip" })
        assertEquals(AttachClient.CONNECT_ATTEMPTS, s.calls)
        assertTrue(r is AttachClient.Result.Failed && r.message.contains("PISTA para 192.168.1.4"), r.toString())
    }

    @Test fun `si lo ultimo que paso fue un corte de red, no se dice que no lo aprobaron`() {
        var t = 0L
        val pairJson = Json.write(mapOf("pairId" to "a".repeat(16), "pub" to StorageCrypto.hex(StorageCrypto.Ecdh.generate().publicPoint()),
            "p4id" to "flexos-x", "p4name" to "Flex OS", "approve" to 1, "expiresIn" to 90))
        var n = 0
        val poster = FormPoster { _, _, _ -> if (n++ == 0) 202 to pairJson else throw HttpStageException(HttpStage.UNREACHABLE, "x") }
        val r = AttachClient(phone, 47830, MemoryPairingRepo(), poster, { t }, { t += it }).attach("192.168.1.4:80", "a".repeat(32), { _, _ -> })
        assertTrue(r is AttachClient.Result.Failed && r.message.contains("Se perdió la comunicación"), r.toString())
    }

    // ------------------------------------------------------------------ la red local: subred, no IP igual
    private fun ip(s: String) = InetAddress.getByName(s)

    @Test fun `192_168_1_4 y 192_168_1_2 son vecinos de la misma LAN (IP distinta es lo normal)`() {
        assertTrue(Lan.sameSubnet(ip("192.168.1.4"), ip("192.168.1.2"), 24))
        assertTrue(Lan.sameSubnet(ip("192.168.1.4"), ip("192.168.1.2"), 8))
    }

    @Test fun `la mascara real manda - no se supone un 24`() {
        assertFalse(Lan.sameSubnet(ip("192.168.1.4"), ip("192.168.1.2"), 30), "/30: .0-.3 y .4-.7 son redes distintas")
        assertTrue(Lan.sameSubnet(ip("192.168.0.9"), ip("192.168.1.9"), 23))
        assertFalse(Lan.sameSubnet(ip("192.168.0.9"), ip("192.168.1.9"), 24))
        assertTrue(Lan.sameSubnet(ip("10.0.0.1"), ip("10.0.200.77"), 16))
        assertTrue(Lan.sameSubnet(ip("172.16.5.5"), ip("172.31.9.9"), 12))
        assertTrue(Lan.sameSubnet(ip("1.2.3.4"), ip("200.2.3.4"), 0))
        assertFalse(Lan.sameSubnet(ip("192.168.1.4"), ip("192.168.1.2"), 33))
    }

    @Test fun `pista de red - misma subred no acusa a nadie, otra subred lo dice, sin Wi-Fi tambien`() {
        assertNull(Lan.hintFor("192.168.1.4", ip("192.168.1.2"), 24))
        assertTrue(Lan.hintFor("192.168.0.4", ip("192.168.1.2"), 24)!!.contains("192.168.1.2/24"))
        assertTrue(Lan.hintFor("192.168.1.4", null, 24)!!.contains("no está conectado"))
        assertNull(Lan.hintFor("192.168.1.4", ip("192.168.1.2"), 23))
    }
}
