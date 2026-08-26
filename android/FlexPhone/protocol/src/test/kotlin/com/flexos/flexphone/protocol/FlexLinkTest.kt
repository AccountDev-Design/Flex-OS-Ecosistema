package com.flexos.flexphone.protocol

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * Pruebas del protocolo del lado Android.
 *
 * Las de VECTORES DORADOS son las importantes: fijan los bytes
 * exactos que tiene que producir este codigo. El mismo vector esta
 * comprobado desde C++ en `tests/host/test_flexlink_vectors.cpp`,
 * asi que si alguien mueve un campo en uno de los dos lados, una de
 * las dos baterias falla. Sin esto, un desajuste entre telefono y
 * placa solo se ve como "el P4 no recibe nada".
 */
class FlexLinkTest {

    private fun hex(b: ByteArray) = b.joinToString("") { "%02X".format(it) }

    // ---------------------------------------------------------
    //  1) VECTORES DORADOS
    // ---------------------------------------------------------
    @Test
    fun `vector dorado - trama PING vacia`() {
        val f = FlexLink.writeFrame(
            FlexLink.Header(type = FlexLink.T_PING, session = 0x1234, packet = 7, counter = 1)
        )
        assertEquals(FlexLink.HDR_SIZE, f.size, "un PING sin carga mide exactamente la cabecera")
        // magia, version, tipo, sesion, paquete, frag/total, len, contador, CRC
        assertEquals("F1580103341207000001000001000000EEA7", hex(f))
    }

    @Test
    fun `vector dorado - trama con carga ASCII`() {
        val f = FlexLink.writeFrame(
            FlexLink.Header(type = FlexLink.T_NOTIF_ADD, session = 0x1234, packet = 7, counter = 1),
            "hola flex".toByteArray(Charsets.UTF_8),
        )
        assertEquals("F15801203412070000010900010000006DFF686F6C6120666C6578", hex(f))
    }

    @Test
    fun `vector dorado - CRC16 CCITT`() {
        // Vectores clasicos del CRC16-CCITT con init 0xFFFF.
        assertEquals(0x29B1, FlexLink.crc16("123456789".toByteArray()))
        assertEquals(0xFFFF, FlexLink.crc16(ByteArray(0)))
    }

    @Test
    fun `vector dorado - notificacion codificada`() {
        val n = NotifPayload(
            id = 42, pkg = "com.whatsapp", app = "WhatsApp", title = "Ana",
            text = "un mensaje", whenMs = 123456, category = Category.MSG,
            priority = Priority.HIGH, hidden = false, sensitive = false,
            canReply = true, replyAction = 1,
            actions = listOf("Marcar leido", "Responder"),
        )
        assertEquals(
            "2A0000000C636F6D2E776861747361707008576861747341707003416E610A756E206D656E73616A6540E2" +
            "010001030401020C4D6172636172206C6569646F09526573706F6E646572",
            hex(n.encode()),
        )
    }

    // ---------------------------------------------------------
    //  2) Ida y vuelta
    // ---------------------------------------------------------
    @Test
    fun `ida y vuelta de una trama`() {
        val body = "carga".toByteArray()
        val f = FlexLink.writeFrame(
            FlexLink.Header(type = FlexLink.T_NOTIF_ADD, session = 9, packet = 3, counter = 77), body
        )
        val r = FlexLink.readFrame(f)
        assertTrue(r is FlexLink.ReadResult.Ok, "no volvio: $r")
        r as FlexLink.ReadResult.Ok
        assertEquals(FlexLink.T_NOTIF_ADD, r.header.type)
        assertEquals(9, r.header.session)
        assertEquals(77L, r.header.counter)
        assertContentEquals(body, r.payload)
    }

    // ---------------------------------------------------------
    //  3) Tramas que hay que RECHAZAR
    // ---------------------------------------------------------
    @Test
    fun `rechaza tramas invalidas`() {
        val good = FlexLink.writeFrame(
            FlexLink.Header(type = FlexLink.T_NOTIF_ADD, session = 1, packet = 1, counter = 1),
            "abcdef".toByteArray(),
        )
        // corrupta en la carga
        val a = good.copyOf(); a[FlexLink.HDR_SIZE + 1] = (a[FlexLink.HDR_SIZE + 1].toInt() xor 1).toByte()
        assertTrue(FlexLink.readFrame(a) is FlexLink.ReadResult.Err, "acepto una carga corrupta")
        // corrupta en la cabecera
        val b = good.copyOf(); b[6] = (b[6].toInt() xor 8).toByte()
        assertTrue(FlexLink.readFrame(b) is FlexLink.ReadResult.Err, "el CRC no cubre la cabecera")
        // magia
        val c = good.copyOf(); c[0] = 0
        assertTrue(FlexLink.readFrame(c) is FlexLink.ReadResult.Err, "acepto magia equivocada")
        // corta
        assertTrue(FlexLink.readFrame(good, 4) is FlexLink.ReadResult.Err, "acepto una trama corta")
        // longitud mentirosa
        val d = good.copyOf(); d[10] = 0xFF.toByte()
        assertTrue(FlexLink.readFrame(d) is FlexLink.ReadResult.Err, "acepto una longitud imposible")
        // version futura
        val e = good.copyOf(); e[2] = 99
        assertTrue(FlexLink.readFrame(e) is FlexLink.ReadResult.Err, "interpreto una version futura")
        // TODO prefijo: ninguno debe reventar
        for (cut in 0 until good.size) FlexLink.readFrame(good, cut)
    }

    // ---------------------------------------------------------
    //  4) Fragmentacion
    // ---------------------------------------------------------
    @Test
    fun `fragmenta y reensambla`() {
        val msg = ByteArray(700) { ((it * 7 + 3) and 0xFF).toByte() }
        val frames = FlexLink.fragment(FlexLink.T_NOTIF_ADD, 1, 5, msg, counterStart = 100)
        assertEquals(4, frames.size, "700 B deberian ir en 4 fragmentos")

        val re = Reassembler()
        var last = Reassembler.Result.DROP
        for (f in frames) {
            val r = FlexLink.readFrame(f)
            assertTrue(r is FlexLink.ReadResult.Ok)
            r as FlexLink.ReadResult.Ok
            last = re.feed(r.header, r.payload, 1000)
        }
        assertEquals(Reassembler.Result.DONE, last, "no completo")
        assertContentEquals(msg, re.message(), "el contenido reensamblado no coincide")
    }

    @Test
    fun `descarta repetidos y fuera de orden`() {
        val msg = ByteArray(500) { it.toByte() }
        val frames = FlexLink.fragment(FlexLink.T_NOTIF_ADD, 1, 6, msg, counterStart = 1)
        val re = Reassembler()

        // Empezar por el ULTIMO fragmento: se descarta.
        val lastF = FlexLink.readFrame(frames.last()) as FlexLink.ReadResult.Ok
        assertEquals(Reassembler.Result.DROP, re.feed(lastF.header, lastF.payload, 0),
            "acepto un mensaje que empieza por el final")
        assertTrue(re.outOfOrder > 0)

        // Ahora en orden, con el primero REPETIDO.
        re.reset()
        val f0 = FlexLink.readFrame(frames[0]) as FlexLink.ReadResult.Ok
        re.feed(f0.header, f0.payload, 0)
        assertEquals(Reassembler.Result.DROP, re.feed(f0.header, f0.payload, 0),
            "acepto el mismo fragmento dos veces")
        assertEquals(1L, re.duplicates)
        for (i in 1 until frames.size) {
            val f = FlexLink.readFrame(frames[i]) as FlexLink.ReadResult.Ok
            re.feed(f.header, f.payload, 0)
        }
        assertContentEquals(msg, re.message(), "el repetido corrompio el mensaje")
    }

    @Test
    fun `un parcial a medias caduca`() {
        val msg = ByteArray(500) { it.toByte() }
        val frames = FlexLink.fragment(FlexLink.T_NOTIF_ADD, 1, 6, msg, counterStart = 1)
        val re = Reassembler()
        val f0 = FlexLink.readFrame(frames[0]) as FlexLink.ReadResult.Ok
        assertEquals(Reassembler.Result.NEED_MORE, re.feed(f0.header, f0.payload, 1000))
        assertFalse(re.expire(2000, 5000), "caduco antes de tiempo")
        assertTrue(re.expire(9000, 5000), "no caduco el parcial")
    }

    @Test
    fun `limites de fragmentacion`() {
        assertEquals(0, FlexLink.fragCount(FlexLink.MAX_MESSAGE + 1, FlexLink.MAX_FRAME))
        assertEquals(0, FlexLink.fragCount(100, FlexLink.MIN_MTU - 1))
        assertEquals(1, FlexLink.fragCount(0, FlexLink.MAX_FRAME))
    }

    // ---------------------------------------------------------
    //  5) Anti-repeticion
    // ---------------------------------------------------------
    @Test
    fun `anti repeticion`() {
        val a = AntiReplay()
        assertTrue(a.check(100), "el primero deberia entrar")
        assertFalse(a.check(100), "acepto el mismo contador dos veces")
        assertTrue(a.check(101))
        assertTrue(a.check(103), "no acepto un hueco hacia delante")
        assertTrue(a.check(102), "no acepto un reordenado dentro de la ventana")
        assertFalse(a.check(102), "acepto un reordenado repetido")
        assertFalse(a.check(1), "acepto un contador demasiado viejo")
        assertTrue(a.check(100_000), "no acepto un salto grande")
        assertFalse(a.check(103), "tras el salto, lo viejo sigue rechazado")
    }

    // ---------------------------------------------------------
    //  6) UTF-8
    // ---------------------------------------------------------
    @Test
    fun `no parte caracteres multibyte`() {
        // "café": la 'é' son 2 bytes. Cortar a 4 partiria el caracter.
        assertEquals("caf", Utf8.clip("café", 4))
        assertEquals("café", Utf8.clip("café", 5))
        // Emoji de 4 bytes en un hueco de 3.
        assertEquals("", Utf8.clip("📱", 3))
        assertEquals("📱", Utf8.clip("📱", 4))

        // Y por el camino del codec, que es el que de verdad se usa.
        val w = PayloadWriter().str("café", 4)
        val r = PayloadReader(w.build())
        assertEquals("caf", r.str(), "el codec partio la 'e' acentuada")
    }

    // ---------------------------------------------------------
    //  7) Campos
    // ---------------------------------------------------------
    @Test
    fun `campos ida y vuelta y longitud mentirosa`() {
        val w = PayloadWriter().u8(0xAB).u16(0x1234).u32(0xDEADBEEFL).str("WhatsApp", 32)
        val r = PayloadReader(w.build())
        assertEquals(0xAB, r.u8())
        assertEquals(0x1234, r.u16())
        assertEquals(0xDEADBEEFL, r.u32())
        assertEquals("WhatsApp", r.str())
        assertTrue(r.ok)

        // Dice 255 bytes, trae 2.
        val liar = PayloadReader(byteArrayOf(0xFF.toByte(), 'a'.code.toByte(), 'b'.code.toByte()))
        assertEquals("", liar.str())
        assertFalse(liar.ok, "no marco la longitud mentirosa")
    }

    // ---------------------------------------------------------
    //  8) Notificaciones: la regla de RemoteInput
    // ---------------------------------------------------------
    @Test
    fun `canReply no sobrevive sin una accion real`() {
        // Se declara canReply pero la accion 3 no existe: al decodificar
        // tiene que quedar en false. Es la salvaguarda que impide que
        // Flex OS pinte un boton "Responder" que no hace nada.
        val w = PayloadWriter()
        w.u32(8).str("com.x", 32).str("X", 16).str("T", 16).str("B", 16)
        w.u32(0).u8(Category.MSG).u8(Priority.DEFAULT)
        w.u8(0x04).u8(3).u8(0)                       // canReply, accion 3, 0 acciones
        val n = NotifPayload.decode(w.build())
        assertTrue(n != null, "no decodifico")
        assertFalse(n!!.canReply, "dejo canReply con una accion inexistente")
    }

    @Test
    fun `notificacion ida y vuelta y saneado`() {
        val n = NotifPayload(
            id = 5, pkg = "com.whatsapp", app = "WhatsApp", title = "Ana", text = "hola",
            whenMs = 1, category = Category.MSG, priority = Priority.HIGH,
            hidden = false, sensitive = false, canReply = true, replyAction = 0,
            actions = listOf("Responder"),
        )
        val back = NotifPayload.decode(n.encode())
        assertEquals(n, back, "la notificacion no vuelve igual")

        // Sin paquete no es identificable.
        val w = PayloadWriter()
        w.u32(1).str("", 32).str("a", 16).str("b", 16).str("c", 16)
        w.u32(0).u8(0).u8(0).u8(0).u8(0).u8(0)
        assertNull(NotifPayload.decode(w.build()), "acepto una notificacion sin paquete")

        // Categoria y prioridad fuera de rango se sanean.
        val w2 = PayloadWriter()
        w2.u32(1).str("com.x", 32).str("a", 16).str("b", 16).str("c", 16)
        w2.u32(0).u8(99).u8(99).u8(0).u8(0).u8(0)
        val s = NotifPayload.decode(w2.build())
        assertEquals(Category.OTHER, s!!.category, "no saneo la categoria")
        assertEquals(Priority.DEFAULT, s.priority, "no saneo la prioridad")

        // Cualquier prefijo truncado: ninguno debe reventar.
        val enc = n.encode()
        for (cut in 0 until enc.size) NotifPayload.decode(enc.copyOfRange(0, cut))
    }

    @Test
    fun `una bateria desconocida no se manda como cero`() {
        val r = PayloadReader(PhoneState("Pixel", battery = -1, charging = false, net = NetKind.WIFI).encode())
        r.str()
        assertEquals(255, r.u8(), "una bateria desconocida deberia ir como 255, no como 0")
    }

    @Test
    fun `resultado de respuesta`() {
        val ok = ReplyResult(7, FlexLink.E_NONE)
        assertTrue(ok.ok)
        assertFalse(ReplyResult(7, FlexLink.E_GONE).ok)
        val r = PayloadReader(ReplyResult(7, FlexLink.E_NOREPLY).encode())
        assertEquals(7L, r.u32())
        assertEquals(FlexLink.E_NOREPLY, r.u8())
    }

    // ---------------------------------------------------------
    //  9) Reintentos
    // ---------------------------------------------------------
    @Test
    fun `los reintentos crecen y se rinden`() {
        var prev = 0L
        for (i in 0 until FlexLink.RETRY_MAX) {
            val d = FlexLink.retryDelayMs(i)
            assertTrue(d > 0, "intento $i sin espera")
            assertTrue(d >= prev, "la espera no crece en $i")
            assertTrue(d <= FlexLink.RETRY_CAP_MS, "la espera se paso del tope en $i")
            prev = d
        }
        assertEquals(0L, FlexLink.retryDelayMs(FlexLink.RETRY_MAX), "no se rinde")
        assertEquals(0L, FlexLink.retryDelayMs(200))
    }
}
