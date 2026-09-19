package com.flexos.flexphone.protocol

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * Los bytes del descubrimiento.
 *
 * LOS MISMOS VECTORES que `tests/host/test_flexphone_discovery.cpp`.
 * Si alguien mueve un campo en uno de los dos lados, una de las dos
 * baterias falla -- que es la unica forma de ver este fallo, porque
 * en la practica se manifiesta como "el reloj no aparece en la
 * lista", sin ningun error por ninguna de las dos puntas.
 */
class DiscoveryTest {

    private fun hex(b: ByteArray) = b.joinToString("") { "%02X".format(it) }

    // ---------------------------------------------------------
    //  1) VECTORES DORADOS
    // ---------------------------------------------------------
    @Test
    fun `vector dorado - la pregunta del reloj`() {
        // "FLEXPHONE?" = 464C455850484F4E453F, y la version detras.
        assertEquals("464C455850484F4E453F02", hex(Discovery.buildProbe(2)))
    }

    @Test
    fun `vector dorado - la pregunta del telefono`() {
        // "FLEXOS?" = 464C45584F533F · v2 · puerto 47820 (CC BA) · "Galaxy A55"
        assertEquals(
            "464C45584F533F02CCBA0A47616C61787920413535",
            hex(Discovery.buildAsk(47820, "Galaxy A55", 2)),
        )
    }

    @Test
    fun `vector dorado - la respuesta del reloj`() {
        assertEquals(
            "464C45584F5321020008666C65786F732D310D466C6578204F5320556C747261",
            hex(Discovery.buildAnswer(0, "flexos-1", "Flex OS Ultra", 2)),
        )
    }

    @Test
    fun `la bandera de emparejamiento viaja en su propio byte`() {
        val b = Discovery.buildAnswer(Discovery.FLAG_PAIRING, "flexos-1", "Flex OS Ultra", 2)
        assertEquals(0x01, b[Discovery.ANS.size + 1].toInt() and 0xFF)
        val a = assertNotNull(Discovery.parseAnswer(b))
        assertTrue(a.pairing)
        assertFalse(assertNotNull(Discovery.parseAnswer(
            Discovery.buildAnswer(0, "flexos-1", "Flex OS Ultra", 2))).pairing)
    }

    // ---------------------------------------------------------
    //  2) Ida y vuelta
    // ---------------------------------------------------------
    @Test
    fun `lo que se escribe es lo que se lee`() {
        val ask = assertNotNull(Discovery.parseAsk(Discovery.buildAsk(47820, "Galaxy A55")))
        assertEquals(FlexLink.VERSION, ask.ver)
        assertEquals(47820, ask.port)
        assertEquals("Galaxy A55", ask.name)

        val ans = assertNotNull(
            Discovery.parseAnswer(Discovery.buildAnswer(Discovery.FLAG_PAIRING, "flexos-1", "Flex OS Ultra"))
        )
        assertEquals("flexos-1", ans.id)
        assertEquals("Flex OS Ultra", ans.name)
        assertTrue(ans.pairing)

        val rep = assertNotNull(Discovery.parseReply(Discovery.buildReply(47820, "Galaxy A55")))
        assertEquals(47820, rep.port)
        assertEquals("Galaxy A55", rep.name)
    }

    // ---------------------------------------------------------
    //  3) Cada paquete solo lo reconoce quien debe
    // ---------------------------------------------------------
    @Test
    fun `los cuatro paquetes no se confunden entre si`() {
        val probe = Discovery.buildProbe()
        val reply = Discovery.buildReply(47820, "t")
        val ask = Discovery.buildAsk(47820, "t")
        val ans = Discovery.buildAnswer(0, "r", "reloj")

        assertTrue(Discovery.isProbe(probe));  assertFalse(Discovery.isReply(probe))
        assertFalse(Discovery.isAsk(probe));   assertFalse(Discovery.isAnswer(probe))

        assertTrue(Discovery.isReply(reply));  assertFalse(Discovery.isProbe(reply))
        assertFalse(Discovery.isAsk(reply));   assertFalse(Discovery.isAnswer(reply))

        assertTrue(Discovery.isAsk(ask));      assertFalse(Discovery.isAnswer(ask))
        assertTrue(Discovery.isAnswer(ans));   assertFalse(Discovery.isAsk(ans))

        // "FLEXOS?" y "FLEXOS!" solo se diferencian en el ultimo byte
        // de la marca. Es justo la clase de par que se reconoce mal si
        // alguien compara solo los primeros bytes.
        assertNull(Discovery.parseAsk(ans))
        assertNull(Discovery.parseAnswer(ask))
    }

    // ---------------------------------------------------------
    //  4) PAQUETES HOSTILES
    // ---------------------------------------------------------
    @Test
    fun `un paquete cortado en cualquier punto no revienta`() {
        val full = Discovery.buildAnswer(0, "flexos-1", "Flex OS Ultra")
        for (cut in 0..full.size) {
            val a = Discovery.parseAnswer(full, cut)
            if (a != null) {
                // Si dice que lo entendio, lo leido tiene que ser un
                // prefijo de lo de verdad -- nunca texto inventado.
                assertTrue("flexos-1".startsWith(a.id), "identificador inventado con $cut B: ${a.id}")
            }
        }
    }

    @Test
    fun `un paquete que miente sobre su longitud no se cree`() {
        // Dice que el identificador mide 200 bytes y solo trae dos.
        val liar = PayloadWriter(32).bytes(Discovery.ANS).u8(2).u8(0).build() +
            byteArrayOf(200.toByte(), 'A'.code.toByte(), 'B'.code.toByte())
        // El lector marca overflow y el identificador sale vacio, asi
        // que no hay nada que ensenar: se descarta.
        assertNull(Discovery.parseAnswer(liar))
    }

    @Test
    fun `una respuesta sin identificador se descarta`() {
        assertNull(Discovery.parseAnswer(Discovery.buildAnswer(0, "", "Flex OS Ultra")))
    }

    @Test
    fun `una respuesta sin nombre usa el identificador`() {
        val a = assertNotNull(Discovery.parseAnswer(Discovery.buildAnswer(0, "flexos-1", "")))
        assertEquals("flexos-1", a.name)
    }

    @Test
    fun `una pregunta sin nombre sigue siendo valida`() {
        val ask = assertNotNull(Discovery.parseAsk(Discovery.buildAsk(47820, null)))
        assertEquals(47820, ask.port)
        assertEquals("", ask.name)
    }

    @Test
    fun `un nombre mas largo que el tope se trunca en frontera UTF-8`() {
        // Emojis de cuatro bytes: 31 bytes no es multiplo de cuatro, asi
        // que el corte cae dentro de uno. Partirlo produciria UTF-8
        // invalido en el reloj, que es como se ve un nombre a medias
        // sin ningun error que lo explique.
        val name = "😀".repeat(12)          // 48 bytes
        val b = Discovery.buildAsk(47820, name)
        val ask = assertNotNull(Discovery.parseAsk(b))
        assertEquals(28, ask.name.toByteArray(Charsets.UTF_8).size)
        assertEquals("😀".repeat(7), ask.name)
    }

    @Test
    fun `un paquete vacio no es ninguno de los cuatro`() {
        val empty = ByteArray(0)
        assertFalse(Discovery.isProbe(empty))
        assertFalse(Discovery.isAsk(empty))
        assertFalse(Discovery.isAnswer(empty))
        assertFalse(Discovery.isReply(empty))
        assertNull(Discovery.parseAnswer(empty))
    }
}
