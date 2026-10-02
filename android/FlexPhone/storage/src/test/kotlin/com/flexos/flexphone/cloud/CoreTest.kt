package com.flexos.flexphone.cloud

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * JSON, nombres y criptografia: las piezas que tienen que comportarse EXACTAMENTE
 * como sus gemelas del servicio de Node (Flex Developer Studio) y del firmware.
 * Los valores esperados de los nombres salen de ejecutar `cloud/src/cloud/names.js`;
 * los de la criptografia, de `crypto` de Node y del RFC 5903, y son los MISMOS que
 * fija tests/host/test_storagecore.cpp.
 */
class CoreTest {
    // ------------------------------------------------------------ JSON
    @Test fun `json ida y vuelta conserva tipos y orden`() {
        val src = """{"a":1,"b":-2.5,"c":"x\"y\\z\u00e1\n","d":[true,false,null],"e":{"f":9007199254740991}}"""
        val v = Json.parseObject(src)
        assertEquals(1L, v["a"]); assertEquals(-2.5, v["b"]); assertEquals("x\"y\\zá\n", v["c"])
        assertEquals(listOf(true, false, null), v["d"])
        assertEquals(9007199254740991L, (v["e"] as Map<*, *>)["f"])
        assertEquals(v, Json.parseObject(Json.write(v)))
        assertEquals(listOf("a", "b", "c", "d", "e"), v.keys.toList())
    }

    @Test fun `json hostil se rechaza entero`() {
        val bad = listOf(
            "", "{", "{\"a\":}", "{\"a\":1,}", "[1,2", "{\"a\":1} x", "\"\u0001\"", "\"\\x\"", "\"\\u12\"",
            "01", "1.", "-", "1e", "{'a':1}", "nul", "[" + "[".repeat(40) + "]".repeat(41),
        )
        for (b in bad) assertFailsWith<JsonException>("debia fallar: $b") { Json.parse(b) }
        assertFailsWith<JsonException> { Json.parseObject("[1]") }
    }

    @Test fun `json escribe escapes seguros`() {
        assertEquals("\"a\\u2028b\\u0000\"", Json.write("a\u2028b\u0000"))
        assertEquals("[1,2.5,null,\"x\"]", Json.write(listOf(1, 2.5, Double.NaN, "x")))
        assertEquals("{\"k\":3}", Json.write(mapOf("k" to 3.0)))
    }

    // ------------------------------------------------------------ nombres
    @Test fun `nombres validos e invalidos como el servicio`() {
        assertEquals("Vídeo de mamá", Names.normalizeName("  Vi\u0301deo de mama\u0301 "))   // NFC + recorte
        for (b in listOf("", "  ", ".", "..", "a/b", "a\\b", "a\u0000b", "a\u007fb", "x\u202ey", "\ud800"))
            assertFailsWith<CloudError>("debia fallar: $b") { Names.normalizeName(b) }
        assertFailsWith<CloudError> { Names.normalizeName(42) }
        assertEquals("x".repeat(255), Names.normalizeName("x".repeat(255)))
        assertFailsWith<CloudError> { Names.normalizeName("x".repeat(256)) }
        assertFailsWith<CloudError> { Names.normalizeName("é".repeat(128)) }   // 256 bytes UTF-8
        assertEquals(Names.nameKey("FOTO.JPG"), Names.nameKey("foto.jpg"))
    }

    @Test fun `nombre numerado sin pasar de 255 bytes`() {
        assertEquals("foto (1).jpg", Names.numberedName("foto.jpg", 1))
        assertEquals(".bashrc (2)", Names.numberedName(".bashrc", 2))
        val long = Names.numberedName("x".repeat(250) + ".jpg", 12)
        assertEquals("x".repeat(246) + " (12).jpg", long)
        assertTrue(long.toByteArray().size <= 255)
    }

    @Test fun `content-disposition identico al del servicio de Node`() {
        val cases = mapOf(
            "foto.jpg" to "filename=\"foto.jpg\"; filename*=UTF-8''foto.jpg",
            "Vídeo de mamá (1).mp4" to "filename=\"Vi_deo de mama_ (1).mp4\"; filename*=UTF-8''V%C3%ADdeo%20de%20mam%C3%A1%20%281%29.mp4",
            "a\"b;c.txt" to "filename=\"a_b_c.txt\"; filename*=UTF-8''a%22b%3Bc.txt",
            "日本語 ☁️.png" to "filename=\"___ __.png\"; filename*=UTF-8''%E6%97%A5%E6%9C%AC%E8%AA%9E%20%E2%98%81%EF%B8%8F.png",
            "it's *weird*~!.pdf" to "filename=\"it's *weird*~!.pdf\"; filename*=UTF-8''it%27s%20%2Aweird%2A~!.pdf",
        )
        for ((n, tail) in cases) {
            assertEquals("attachment; $tail", Names.contentDisposition(n, false), n)
            assertEquals("inline; $tail", Names.contentDisposition(n, true), n)
        }
    }

    @Test fun `tipos MIME y clases como el servicio`() {
        assertEquals("image/heic", Names.mimeFor("a.HEIC", null))
        assertEquals("video/mp4", Names.mimeFor("a.bin", "video/mp4"))
        assertEquals("application/octet-stream", Names.mimeFor("a.zzz", "application/octet-stream"))
        assertEquals("document", Names.kindFor("application/vnd.ms-excel"))
        assertEquals("archive", Names.kindFor("application/x-7z-compressed"))
        assertEquals("video", Names.kindFor("video/x-msvideo"))
        assertFalse(Names.inlineSafe("image/svg+xml"))
        assertTrue(Names.inlineSafe("video/x-msvideo"))
        assertTrue(Names.ID_FILE.matches(Names.newId("fil")))
        assertEquals(24, Names.newId("fld").length - 4)
    }

    // ------------------------------------------------------------ cripto
    private val zRfc = "d6840f6b42f6edafd13116e0e12565202fef8e9ece7dce03812464d04b9442de"
    private val offer = "00112233445566778899aabbccddeeff"
    private val p4 = "flexos-a1b2c3d4e5f6"
    private val ph = "a55-0f1e2d3c4b5a6978"
    private val pair = "0123456789abcdef0123456789abcdef"
    private val nonce = "fedcba9876543210fedcba9876543210"
    private val token = "00112233445566778899aabbccddeeff0011223344556677"

    @Test fun `ECDH P-256 con los vectores del RFC 5903`() {
        val i = StorageCrypto.unhex("c88f01f510d9ac3f70a292daa2316de544e9aab8afe84049c62a9c57862d1433", 32)!!
        val r = StorageCrypto.unhex("c6ef9c5d78ae012a011164acb397ce2088685d8f06bf9be0b283ab46476bee53", 32)!!
        val a = StorageCrypto.Ecdh.fromPrivate(i)
        val b = StorageCrypto.Ecdh.fromPrivate(r)
        assertEquals("04dad0b65394221cf9b051e1feca5787d098dfe637fc90b9ef945d0c37725811805271a0461cdb8252d61f1c456fa3e59ab1f45b33accf5f58389e0577b8990bb3",
            StorageCrypto.hex(a.publicPoint()))
        assertEquals("04d12dfb5289c8d4f81208b70270398c342296970a0bccb74c736fc7554494bf6356fbf3ca366cc23e8157854c13c58d6aac23f046ada30f8353e74f33039872ab",
            StorageCrypto.hex(b.publicPoint()))
        assertEquals(zRfc, StorageCrypto.hex(a.shared(b.publicPoint())))
        assertEquals(zRfc, StorageCrypto.hex(b.shared(a.publicPoint())))
    }

    @Test fun `ECDH rechaza puntos fuera de la curva`() {
        val a = StorageCrypto.Ecdh.generate()
        val p = a.publicPoint()
        val bad = p.copyOf(); bad[64] = (bad[64].toInt() xor 1).toByte()
        assertFailsWith<IllegalArgumentException> { a.shared(bad) }
        assertFailsWith<IllegalArgumentException> { a.shared(p.copyOf(64)) }
        val compressed = p.copyOf(); compressed[0] = 2
        assertFailsWith<IllegalArgumentException> { a.shared(compressed) }
        // Dos pares aleatorios llegan al mismo secreto.
        val b = StorageCrypto.Ecdh.generate()
        assertEquals(StorageCrypto.hex(a.shared(b.publicPoint())), StorageCrypto.hex(b.shared(a.publicPoint())))
    }

    @Test fun `derivaciones con los vectores dorados compartidos con el firmware`() {
        val k = StorageCrypto.deriveKey(StorageCrypto.unhex(zRfc, 32)!!, offer, p4, ph)
        assertEquals("d80ca9748fade0c3dcc6a8df78aa9acb8a4e6aec13b3277716fd5c957aed097c", StorageCrypto.hex(k))
        assertEquals("119168", StorageCrypto.sas(k))
        assertEquals("20d687744e9c94e2d562be6f4c94840058a04a7ad93753dddcfe8e50f2586dc6", StorageCrypto.hex(StorageCrypto.phoneProof(k, pair)))
        assertEquals("6e6fef9fbe2bfb39364bca62080214c59bbacbb935e8060b4de11cdf623e65e1", StorageCrypto.hex(StorageCrypto.p4Proof(k, pair)))
        assertEquals("b08f4049448d95087446408936384c22d0cc8178b2f8b8f15095527a26fef563", StorageCrypto.hex(StorageCrypto.knownProof(k, offer)))
        assertEquals("1516b0c29c91791b1415367353843c1cf9f43c19dc7dd07a034c66fd011bbb2b", StorageCrypto.hex(StorageCrypto.sessionMac(k, nonce, p4)))
        assertEquals("9be1ad4b63fe1e602ce8c0ce9030bd77f68c4ac316931e94e1c047f28e70bdb1", StorageCrypto.hex(StorageCrypto.sessionOk(k, nonce, token)))
        // La longitud delante separa los campos: ("ab","c") no es ("a","bc").
        val z = StorageCrypto.unhex(zRfc, 32)!!
        assertFalse(StorageCrypto.deriveKey(z, offer, "ab", "c").contentEquals(StorageCrypto.deriveKey(z, offer, "a", "bc")))
        assertNull(StorageCrypto.unhex("zz", 1))
        assertNull(StorageCrypto.unhex("abc", 2))
    }

    // ------------------------------------------------------------ sesiones
    @Test fun `retos de un solo uso y sesiones ligadas a una IP`() {
        var t = 1_000_000L
        val sm = SessionManager { t }
        val n = sm.newChallenge()
        assertTrue(sm.consumeChallenge(n))
        assertFalse(sm.consumeChallenge(n), "un reto no vale dos veces")
        val n2 = sm.newChallenge()
        t += SessionManager.CHALLENGE_TTL_MS + 1
        assertFalse(sm.consumeChallenge(n2), "un reto caducado no vale")
        val s = sm.create("192.168.1.50")
        assertEquals(48, s.token.length)
        assertTrue(sm.validate(s.token, "192.168.1.50") != null)
        assertNull(sm.validate(s.token, "192.168.1.51"), "otra IP no puede usar el token")
        assertNull(sm.validate(s.token.replaceRange(0, 1, if (s.token[0] == 'a') "b" else "a"), "192.168.1.50"))
        t += SessionManager.IDLE_MS + 1
        assertNull(sm.validate(s.token, "192.168.1.50"), "sin uso, la sesion caduca")
        // Como mucho cuatro: la mas vieja cede su sitio.
        val list = (1..5).map { t += 10; sm.create("10.0.0.$it") }
        assertNull(sm.validate(list[0].token, "10.0.0.1"))
        assertTrue(sm.validate(list[4].token, "10.0.0.5") != null)
        // Limitador: 10 fallos y la IP espera.
        repeat(9) { sm.fail("10.0.0.9") }
        assertFalse(sm.blocked("10.0.0.9"))
        sm.fail("10.0.0.9")
        assertTrue(sm.blocked("10.0.0.9"))
        t += SessionManager.BLOCK_MS + 1
        assertFalse(sm.blocked("10.0.0.9"))
    }
}
