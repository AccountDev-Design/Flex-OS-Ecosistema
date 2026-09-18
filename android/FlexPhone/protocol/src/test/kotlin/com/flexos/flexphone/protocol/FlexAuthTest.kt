package com.flexos.flexphone.protocol

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/**
 * Emparejamiento y sesion, lado Android.
 *
 * LOS VECTORES DORADOS DE ESTE FICHERO SON LOS MISMOS que los de
 * `tests/host/test_flexauth.cpp` y `test_flexlink_vectors.cpp`. Es
 * lo unico que garantiza que el telefono y el reloj derivan LA MISMA
 * clave del mismo codigo.
 *
 * Sin esto, un cambio en la derivacion hecho en un solo lado no
 * rompe ninguna compilacion: simplemente el emparejamiento deja de
 * funcionar y no hay error que lo explique, porque cada extremo
 * calcula algo perfectamente valido... y distinto.
 */
class FlexAuthTest {

    private fun hex(b: ByteArray) = b.joinToString("") { "%02x".format(it) }

    private val salt = ByteArray(FlexAuth.SALT_SIZE) { it.toByte() }
    private val nonce = ByteArray(FlexAuth.NONCE_SIZE) { (0xA0 + it).toByte() }

    // ---------------------------------------------------------
    //  1) Primitivas contra los vectores publicados
    // ---------------------------------------------------------
    @Test
    fun `sha256 contra FIPS 180-4`() {
        assertEquals(
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            hex(FlexAuth.sha256(ByteArray(0))),
        )
        assertEquals(
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            hex(FlexAuth.sha256("abc".toByteArray())),
        )
    }

    @Test
    fun `hmac sha256 contra RFC 4231`() {
        assertEquals(
            "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
            hex(FlexAuth.hmacSha256(ByteArray(20) { 0x0b }, "Hi There".toByteArray())),
        )
        assertEquals(
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
            hex(FlexAuth.hmacSha256("Jefe".toByteArray(), "what do ya want for nothing?".toByteArray())),
        )
    }

    // ---------------------------------------------------------
    //  2) VECTOR DORADO compartido con el firmware
    // ---------------------------------------------------------
    @Test
    fun `vector dorado - clave del vinculo`() {
        val key = FlexAuth.deriveKey("012345", salt, "flexos-1", "phone-1")
        assertEquals(
            "66840f7c66ababb9f042d87c18e1f6b28d1bf08db58316823bb99edb38954176",
            hex(key),
            "la clave derivada tiene que coincidir con la del firmware, byte a byte",
        )
    }

    @Test
    fun `vector dorado - pruebas de sesion`() {
        val key = FlexAuth.deriveKey("012345", salt, "flexos-1", "phone-1")
        assertEquals(
            "a259af02d131f300fd204569b46a9d16ea7754c2d88e62768566ce72f169786a",
            hex(FlexAuth.proof(key, FlexAuth.ROLE_PHONE, nonce, 0x1234)),
        )
        assertEquals(
            "df2848a174c2210e9ae82e2bb5b0888aa50fbf5cc32476612844ad3359385377",
            hex(FlexAuth.proof(key, FlexAuth.ROLE_HOST, nonce, 0x1234)),
        )
    }

    // ---------------------------------------------------------
    //  3) Lo que la derivacion tiene que IMPEDIR
    // ---------------------------------------------------------
    @Test
    fun `un codigo equivocado da otra clave`() {
        val a = FlexAuth.deriveKey("012345", salt, "flexos-1", "phone-1")
        val b = FlexAuth.deriveKey("012346", salt, "flexos-1", "phone-1")
        assertFalse(FlexAuth.equalsConstantTime(a, b))
    }

    @Test
    fun `cada emparejamiento usa su propia sal`() {
        val other = salt.copyOf().also { it[0] = (it[0].toInt() xor 0xFF).toByte() }
        val a = FlexAuth.deriveKey("012345", salt, "flexos-1", "phone-1")
        val b = FlexAuth.deriveKey("012345", other, "flexos-1", "phone-1")
        assertFalse(FlexAuth.equalsConstantTime(a, b))
    }

    @Test
    fun `los identificadores no se pueden confundir`() {
        // ("ab","c") y ("a","bc") tienen que dar claves DISTINTAS.
        val a = FlexAuth.deriveKey("012345", salt, "ab", "c")
        val b = FlexAuth.deriveKey("012345", salt, "a", "bc")
        assertFalse(FlexAuth.equalsConstantTime(a, b))
    }

    // ---------------------------------------------------------
    //  4) Reto-respuesta mutuo
    // ---------------------------------------------------------
    @Test
    fun `reenviar la prueba ajena no cuela`() {
        val key = FlexAuth.deriveKey("012345", salt, "flexos-1", "phone-1")
        val mine = FlexAuth.proof(key, FlexAuth.ROLE_PHONE, nonce, 0x1234)
        assertTrue(FlexAuth.verify(key, FlexAuth.ROLE_PHONE, nonce, 0x1234, mine))
        // Si las dos pruebas fueran iguales, quien escuche la del
        // telefono podria hacerse pasar por Flex OS.
        assertFalse(FlexAuth.verify(key, FlexAuth.ROLE_HOST, nonce, 0x1234, mine))
    }

    @Test
    fun `otra sesion o otro reto no valen`() {
        val key = FlexAuth.deriveKey("012345", salt, "flexos-1", "phone-1")
        val mine = FlexAuth.proof(key, FlexAuth.ROLE_PHONE, nonce, 0x1234)
        assertFalse(FlexAuth.verify(key, FlexAuth.ROLE_PHONE, nonce, 0x1235, mine))
        val other = nonce.copyOf().also { it[0] = (it[0].toInt() xor 1).toByte() }
        assertFalse(FlexAuth.verify(key, FlexAuth.ROLE_PHONE, other, 0x1234, mine))
    }

    // ---------------------------------------------------------
    //  5) El codigo se valida ANTES de derivar nada
    // ---------------------------------------------------------
    @Test
    fun `el codigo tiene que ser seis digitos`() {
        assertTrue(FlexAuth.isValidCode("000000"))
        assertTrue(FlexAuth.isValidCode("948271"))
        assertFalse(FlexAuth.isValidCode("12345"))
        assertFalse(FlexAuth.isValidCode("1234567"))
        assertFalse(FlexAuth.isValidCode("12a456"))
        assertFalse(FlexAuth.isValidCode(""))
    }

    @Test
    fun `la comparacion de igual tamano distingue en cualquier posicion`() {
        val a = byteArrayOf(1, 2, 3, 4)
        assertTrue(FlexAuth.equalsConstantTime(a, byteArrayOf(1, 2, 3, 4)))
        assertFalse(FlexAuth.equalsConstantTime(a, byteArrayOf(9, 2, 3, 4)))
        assertFalse(FlexAuth.equalsConstantTime(a, byteArrayOf(1, 2, 3, 9)))
        assertFalse(FlexAuth.equalsConstantTime(a, byteArrayOf(1, 2, 3)))
    }
}
