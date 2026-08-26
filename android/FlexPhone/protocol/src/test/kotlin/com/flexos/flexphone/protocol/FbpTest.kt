package com.flexos.flexphone.protocol

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * FBP/1: los bytes que el Browser Relay del telefono le manda al P4.
 *
 * Los VECTORES DORADOS de esta clase se generaron ejecutando el codec
 * REAL del firmware (`fbpWriteHeader`, `fbpBuildPointer`, ... de
 * `FlexOS_Browser.cpp`). Si el relay del telefono emitiera una
 * cabecera con los campos en otro orden, el navegador del P4
 * descartaria el mensaje sin decir por que: se veria como "la pagina
 * no carga". Estos vectores lo convierten en un fallo de pruebas.
 */
class FbpTest {

    private fun hex(b: ByteArray) = b.joinToString("") { "%02X".format(it) }

    // ---------------------------------------------------------
    //  Vectores dorados: salida del codec del firmware
    // ---------------------------------------------------------
    @Test
    fun `vector dorado - cabecera`() {
        // fbpWriteHeader(type=0x10 NAVIGATE, flags=0, canal=1, seq=7, len=5)
        val h = Fbp.writeHeader(Fbp.T_NAVIGATE, 0, 1, 7, 5)
        assertEquals(Fbp.HDR_SIZE, h.size)
        assertEquals("464201100001070005000000", hex(h))
    }

    @Test
    fun `vector dorado - PING`() {
        // fbpBuildSimple(seq=9, canal=1, PING)
        assertEquals("464201020001090000000000", hex(Fbp.message(Fbp.T_PING, 0, 1, 9)))
    }

    @Test
    fun `vector dorado - POINTER`() {
        // fbpBuildPointer(seq=3, canal=1, accion=tap, x=120, y=240, boton=0)
        val p = FbpWriter().u8(Fbp.Pointer.TAP).u16(120).u16(240).u8(0).build()
        assertEquals("464201200001030006000000037800F00000", hex(Fbp.message(Fbp.T_POINTER, 0, 1, 3, p)))
    }

    @Test
    fun `vector dorado - SCROLL con delta negativo`() {
        // fbpBuildScroll(seq=4, canal=1, dx=0, dy=-50). El -50 va en
        // complemento a dos: CEFF. Es el caso que mas facil se rompe.
        val p = FbpWriter().i16(0).i16(-50).build()
        assertEquals("4642012100010400040000000000CEFF", hex(Fbp.message(Fbp.T_SCROLL, 0, 1, 4, p)))
    }

    @Test
    fun `vector dorado - NAVIGATE con cadena`() {
        // fbpBuildNavigate(seq=2, canal=1, "http://a.b/")
        val w = FbpWriter()
        Fbp.putString(w, "http://a.b/")
        assertEquals(
            "46420110000102000D0000000B00687474703A2F2F612E622F",
            hex(Fbp.message(Fbp.T_NAVIGATE, 0, 1, 2, w.build())),
        )
    }

    // ---------------------------------------------------------
    //  Cabeceras
    // ---------------------------------------------------------
    @Test
    fun `cabecera ida y vuelta`() {
        val m = Fbp.message(Fbp.T_FRAME, Fbp.FRAME_KEYFRAME, 3, 1234, ByteArray(10))
        val h = Fbp.readHeader(m)
        assertTrue(h != null)
        assertEquals(Fbp.T_FRAME, h!!.type)
        assertEquals(Fbp.FRAME_KEYFRAME, h.flags)
        assertEquals(3, h.channel)
        assertEquals(1234, h.seq)
        assertEquals(10L, h.length)
    }

    @Test
    fun `rechaza cabeceras invalidas`() {
        val good = Fbp.message(Fbp.T_PING, 0, 0, 1)
        assertNull(Fbp.readHeader(good, 5), "acepto una cabecera corta")
        val badMagic = good.copyOf(); badMagic[0] = 0
        assertNull(Fbp.readHeader(badMagic), "acepto magia equivocada")
        // Una version distinta NO se negocia a la baja: se rechaza.
        val badVer = good.copyOf(); badVer[2] = 9
        assertNull(Fbp.readHeader(badVer), "negocio a la baja una version desconocida")
    }

    // ---------------------------------------------------------
    //  Cargas que llegan del dispositivo
    // ---------------------------------------------------------
    @Test
    fun `parsea eventos del dispositivo`() {
        val ptr = Fbp.parsePointer(FbpWriter().u8(Fbp.Pointer.DOWN).u16(10).u16(20).u8(1).build())
        assertEquals(Fbp.PointerEvent(Fbp.Pointer.DOWN, 10, 20, 1), ptr)

        val sc = Fbp.parseScroll(FbpWriter().i16(-5).i16(120).build())
        assertEquals(Fbp.ScrollEvent(-5, 120), sc, "el delta con signo no vuelve bien")

        val key = FbpWriter().u8(Fbp.Key.TEXT).u8(0).u8(0)
        Fbp.putString(key, "hola")
        assertEquals(Fbp.KeyEvent(Fbp.Key.TEXT, 0, 0, "hola"), Fbp.parseKey(key.build()))

        val vp = Fbp.parseViewport(FbpWriter().u16(480).u16(800).u8(62).u8(1).u8(100).build())
        assertEquals(Fbp.Viewport(480, 800, 62, 1, 100), vp)
    }

    @Test
    fun `cargas truncadas no revientan`() {
        val w = FbpWriter().u8(1).u16(2).u16(3).u8(4)
        val full = w.build()
        for (cut in 0 until full.size) {
            // Ninguno debe lanzar; los incompletos devuelven null.
            Fbp.parsePointer(full.copyOfRange(0, cut))
        }
        assertNull(Fbp.parsePointer(ByteArray(0)), "acepto una carga vacia")
    }

    @Test
    fun `HELLO ida y vuelta`() {
        val w = FbpWriter()
        w.u8(1).u16(480).u16(800).u8(62).u8(1).u32(0xFF).u8(1).u8(6).u32(196608)
        Fbp.putString(w, "token")
        Fbp.putString(w, "flexos-p4")
        val h = Fbp.parseHello(w.build())
        assertTrue(h != null)
        assertEquals(480, h!!.viewportW)
        assertEquals(800, h.viewportH)
        assertEquals(6, h.maxTabs)
        assertEquals("flexos-p4", h.deviceId)
        // Un HELLO cortado no se da por bueno a medias.
        assertNull(Fbp.parseHello(w.build().copyOfRange(0, 6)), "acepto un HELLO truncado")
    }

    // ---------------------------------------------------------
    //  Mensajes que emite el relay
    // ---------------------------------------------------------
    @Test
    fun `FRAME lleva la region y la imagen`() {
        val img = ByteArray(32) { it.toByte() }
        val m = Fbp.frame(1, 1, x = 0, y = 96, w = 480, h = 600,
            keyframe = true, last = true, frameId = 7, image = img)
        val h = Fbp.readHeader(m)!!
        assertEquals(Fbp.T_FRAME, h.type)
        // 18 bytes fijos + imagen
        assertEquals((18 + img.size).toLong(), h.length)
        val r = FbpReader(m.copyOfRange(Fbp.HDR_SIZE, m.size))
        assertEquals(0, r.u16()); assertEquals(96, r.u16())
        assertEquals(480, r.u16()); assertEquals(600, r.u16())
        assertEquals(Fbp.FORMAT_JPEG, r.u8())
        assertEquals(Fbp.FRAME_KEYFRAME or Fbp.FRAME_LAST, r.u8())
        assertEquals(7L, r.u32())
        assertEquals(img.size.toLong(), r.u32())
    }

    @Test
    fun `WELCOME y ERROR`() {
        val wel = Fbp.welcome(1, 480, 800, caps = 3, maxTabs = 3, maxFrameBytes = 196608, sessionId = "s1")
        assertEquals(Fbp.T_WELCOME, Fbp.readHeader(wel)!!.type)
        val err = Fbp.error(2, 0, 503, "el relay se detuvo")
        val h = Fbp.readHeader(err)!!
        assertEquals(Fbp.T_ERROR, h.type)
        val r = FbpReader(err.copyOfRange(Fbp.HDR_SIZE, err.size))
        assertEquals(503, r.u16())
        assertEquals("el relay se detuvo", r.str())
    }

    @Test
    fun `las cadenas se truncan en frontera UTF-8`() {
        val w = FbpWriter()
        // "café" son 5 bytes; con tope 4 tiene que quedarse en "caf".
        Fbp.putString(w, "café", 4)
        val r = FbpReader(w.build())
        assertEquals("caf", r.str(), "partio un caracter multibyte")
    }
}
