package com.flexos.flexphone.protocol

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

/**
 * LA SESION DE EMPAREJAMIENTO, con el fallo que la hizo necesaria
 * escrito como prueba.
 *
 * El sintoma era: "tecleo el codigo que ensena el reloj y me dice que
 * no coincide". La causa no estaba en la comparacion -- que es
 * correcta y aqui se comprueba contra el firmware -- sino en QUE
 * codigo se comparaba: el de un intento anterior, reenviado solo en
 * cuanto llegaba la sal de un emparejamiento NUEVO.
 *
 * Todo esto es JVM puro: se ejecuta con `gradle :protocol:test`, sin
 * emulador y sin placa.
 */
class PairingSessionTest {

    private val hostId = "flexos-0123456789abcdef"
    private val phoneId = "phone-fedcba9876543210"

    private fun salt(seed: Byte) = ByteArray(FlexAuth.SALT_SIZE) { (seed + it).toByte() }
    private fun nonce(seed: Byte) = ByteArray(FlexAuth.NONCE_SIZE) { (seed + it).toByte() }

    private fun session(
        s: Byte = 1,
        n: Byte = 40,
        startedAt: Long = 1_000L,
    ) = PairingSession(salt(s), nonce(n), hostId, phoneId, startedAt)

    // =========================================================
    //  1) El codigo correcto empareja
    // =========================================================
    @Test
    fun `codigo correcto produce la prueba que espera Flex OS`() {
        val p = session()
        val r = p.submit("123456", 1_000L)
        assertTrue(r is PairingSession.Submit.Ready, "no acepto un codigo bien formado")

        // La prueba tiene que ser EXACTAMENTE la que el firmware
        // verifica: HMAC(clave, "flexphone-peer-v2" || reto || 0).
        val expectedKey = FlexAuth.deriveKey("123456", p.salt, hostId, phoneId)
        val expectedProof = FlexAuth.proof(expectedKey, FlexAuth.ROLE_PHONE, p.nonce, 0)
        assertContentEquals(expectedProof, r.proof)
        assertContentEquals(expectedKey, r.key)
        assertEquals(PairingSession.State.CONFIRM_SENT, p.state)
    }

    // =========================================================
    //  2) Un codigo equivocado da OTRA clave (y Flex OS lo rechaza)
    // =========================================================
    @Test
    fun `un digito distinto da una clave distinta`() {
        val p = session()
        val a = p.submit("123456", 1_000L) as PairingSession.Submit.Ready
        val q = session()
        val b = q.submit("123457", 1_000L) as PairingSession.Submit.Ready
        assertFalse(a.key.contentEquals(b.key), "dos codigos distintos dieron la MISMA clave")
        assertFalse(a.proof.contentEquals(b.proof), "dos codigos distintos dieron la MISMA prueba")
    }

    @Test
    fun `lo que no son seis digitos no deriva nada`() {
        val p = session()
        for (bad in listOf("", "12345", "1234567", "12a456", "12 456", " 12345", "123456\n")) {
            assertEquals(
                PairingSession.Submit.BadFormat, p.submit(bad, 1_000L),
                "acepto \"$bad\" como codigo",
            )
        }
        assertNull(p.key, "derivo una clave con algo que no es un codigo")
    }

    // =========================================================
    //  3) Caducado se dice como caducado
    // =========================================================
    @Test
    fun `pasado el plazo no se manda nada`() {
        val p = session(startedAt = 1_000L)
        assertEquals(PairingSession.WINDOW_MS, p.remainingMs(1_000L))
        assertFalse(p.isExpired(1_000L + PairingSession.WINDOW_MS - 1))
        assertTrue(p.isExpired(1_000L + PairingSession.WINDOW_MS))
        assertEquals(
            PairingSession.Submit.Expired,
            p.submit("123456", 1_000L + PairingSession.WINDOW_MS + 1),
        )
        assertEquals(0L, p.remainingMs(9_999_999L))
    }

    /**
     * MISMAS UNIDADES EN LOS DOS LADOS. La ventana de Android tiene
     * que ser la misma que `FLP_LINK_PAIR_WINDOW_MS` del firmware
     * (120000 ms). Si una fuera de segundos y la otra de
     * milisegundos, un codigo perfectamente valido se rechazaria por
     * "caducado" a los dos segundos -- o no caducaria nunca.
     */
    @Test
    fun `la ventana son los mismos dos minutos que el firmware`() {
        assertEquals(120_000L, PairingSession.WINDOW_MS)
    }

    // =========================================================
    //  4) Un codigo que empieza por cero
    // =========================================================
    /**
     * "012345" tiene que seguir siendo "012345". Pasarlo por un
     * entero le come el cero y produce justo el sintoma que se estaba
     * persiguiendo -- "el codigo no coincide" con el codigo bien
     * tecleado -- en uno de cada diez emparejamientos.
     */
    @Test
    fun `un codigo que empieza por cero conserva el cero`() {
        assertTrue(FlexAuth.isValidCode("012345"))
        assertTrue(FlexAuth.isValidCode("000000"))

        val p = session()
        val conCero = p.submit("012345", 1_000L) as PairingSession.Submit.Ready
        val q = session()
        val sinCero = q.submit("123450", 1_000L) as PairingSession.Submit.Ready
        assertFalse(
            conCero.key.contentEquals(sinCero.key),
            "\"012345\" y \"123450\" derivaron la misma clave",
        )
        assertEquals("012345", p.code, "el codigo guardado perdio el cero de cabeza")

        // Y la clave es la del texto tal cual, no la del numero.
        assertContentEquals(
            FlexAuth.deriveKey("012345", p.salt, hostId, phoneId),
            conCero.key,
        )
    }

    // =========================================================
    //  5) EL FALLO: un codigo viejo NO se reenvia en una sesion nueva
    // =========================================================
    /**
     * Esta es la prueba del fallo reportado.
     *
     * El usuario intenta emparejar, falla, y vuelve a pulsar
     * "Emparejar telefono" en el reloj. Flex OS abre una sesion NUEVA:
     * codigo nuevo y SAL NUEVA. Antes, la app contestaba sola con el
     * codigo del intento anterior en cuanto llegaba esa sal, Flex OS
     * lo rechazaba y el usuario leia "el codigo no coincide" sin que
     * le hubieran dejado teclear nada.
     *
     * Una sal distinta es una sesion distinta, y una sesion nueva nace
     * SIN codigo.
     */
    @Test
    fun `una sal nueva no hereda el codigo del intento anterior`() {
        val primera = session(s = 1, n = 40, startedAt = 1_000L)
        primera.submit("111111", 1_000L)
        assertEquals("111111", primera.code)
        primera.onRejected()

        // Flex OS manda otra sal: es otra sesion.
        val segunda = session(s = 77, n = 90, startedAt = 50_000L)
        assertFalse(
            segunda.hasSalt(primera.salt),
            "dos sales distintas se tomaron por la misma sesion",
        )
        assertNull(segunda.code, "LA SESION NUEVA HEREDO EL CODIGO DE LA ANTERIOR")
        assertNull(segunda.key, "la sesion nueva heredo la clave de la anterior")
        assertEquals(PairingSession.State.AWAITING_CODE, segunda.state)
        assertEquals(0, segunda.rejects, "la sesion nueva heredo los rechazos de la anterior")
    }

    /**
     * La MISMA sal, en cambio, SI es la misma sesion. Flex OS reenvia
     * su sal cada vez que se reabre el canal, y eso no puede tirar lo
     * que el usuario ya tenia tecleado ni reiniciarle el plazo.
     */
    @Test
    fun `la misma sal es la misma sesion`() {
        val p = session(s = 5, startedAt = 1_000L)
        assertTrue(p.hasSalt(salt(5)), "no reconocio su propia sal al reenviarse")
        p.submit("222222", 1_000L)
        assertEquals("222222", p.code)
        assertEquals(1_000L + PairingSession.WINDOW_MS, p.expiresAt)
    }

    /**
     * Al reabrirse el canal, la prueba que murio con el socket
     * anterior se rehace sola. El usuario no tiene que volver a
     * teclear lo que ya tecleo, y el resultado es EXACTAMENTE el
     * mismo: misma clave, misma prueba.
     */
    @Test
    fun `al reconectar se rehace la prueba del codigo ya tecleado`() {
        val p = session()
        val primera = p.submit("135790", 1_000L) as PairingSession.Submit.Ready
        val otra = p.proofForTypedCode(2_000L)
        assertTrue(otra != null, "no rehizo la prueba tras la reconexion")
        assertContentEquals(primera.proof, otra.proof, "la prueba rehecha no es la misma")
        assertContentEquals(primera.key, otra.key)
    }

    @Test
    fun `sin codigo tecleado no se rehace ninguna prueba`() {
        val p = session()
        assertNull(p.proofForTypedCode(1_000L), "se invento una prueba sin codigo")
        p.submit("135790", 1_000L)
        assertNull(
            p.proofForTypedCode(1_000L + PairingSession.WINDOW_MS + 1),
            "rehizo la prueba de un codigo ya caducado",
        )
    }

    // =========================================================
    //  6) Pulsaciones rapidas: una sola prueba en vuelo
    // =========================================================
    @Test
    fun `pulsar dos veces no manda dos pruebas`() {
        val p = session()
        assertTrue(p.submit("123456", 1_000L) is PairingSession.Submit.Ready)
        assertEquals(
            PairingSession.Submit.AlreadyInFlight, p.submit("123456", 1_100L),
            "un segundo toque con el mismo codigo abrio otro intento",
        )
        // Corregir un digito SI vuelve a mandar: es otro intento de verdad.
        assertTrue(
            p.submit("123457", 1_200L) is PairingSession.Submit.Ready,
            "no dejo corregir el codigo",
        )
    }

    /**
     * UN ENVIO QUE NO SALE NO ES UN RECHAZO.
     *
     * Si el socket estaba caido al pulsar "Emparejar", el codigo
     * tecleado tiene que sobrevivir: el reloj sigue ensenando
     * exactamente el mismo codigo, y en cuanto reabra el canal se
     * reenvia solo. Tratarlo como un rechazo borraba el codigo y
     * obligaba al usuario a teclear otra vez lo mismo.
     */
    @Test
    fun `si la prueba no sale el codigo se conserva y se reenvia`() {
        val p = session()
        val primera = p.submit("246813", 1_000L) as PairingSession.Submit.Ready
        p.onSendFailed()
        assertEquals("246813", p.code, "PERDIO EL CODIGO PORQUE NO SALIO EL ENVIO")
        assertEquals(0, p.rejects, "conto un rechazo que no existio")
        assertEquals(PairingSession.State.AWAITING_CODE, p.state)

        // Al reabrirse el canal se reenvia lo mismo, sin teclear nada.
        val otra = p.proofForTypedCode(2_000L)
        assertTrue(otra != null, "no reenvio el codigo al volver el canal")
        assertContentEquals(primera.proof, otra.proof)
    }

    // =========================================================
    //  7) Rechazo y correccion sobre el MISMO codigo del reloj
    // =========================================================
    @Test
    fun `un rechazo no tira la sesion y deja corregir`() {
        val p = session()
        p.submit("123456", 1_000L)
        p.onRejected()
        assertEquals(PairingSession.State.REJECTED, p.state)
        assertEquals(1, p.rejects)
        assertNull(p.code, "conservo el codigo rechazado para reenviarlo solo")
        assertTrue(p.acceptsCode(1_000L), "no deja volver a teclear tras un rechazo")

        val r = p.submit("654321", 1_500L)
        assertTrue(r is PairingSession.Submit.Ready, "no acepto el segundo intento")
        p.onAccepted()
        assertEquals(PairingSession.State.DONE, p.state)
        assertEquals(PairingSession.Submit.AlreadyDone, p.submit("654321", 1_600L))
        assertFalse(p.acceptsCode(1_600L), "sigue pidiendo codigo con la sesion cerrada")
    }

    // =========================================================
    //  8) La prueba es la que verifica el firmware
    // =========================================================
    /**
     * Vector cruzado: la prueba que sale de aqui tiene que pasar
     * `flexAuthVerify(key, FLXA_ROLE_PHONE, nonce, 0, proof)`. Se
     * comprueba con la propia derivacion, que `FlexAuthTest` ya fija
     * contra los vectores dorados de `tests/host/test_flexauth.cpp`.
     */
    @Test
    fun `la prueba se verifica con la clave derivada del mismo codigo`() {
        val p = session()
        val r = p.submit("098765", 1_000L) as PairingSession.Submit.Ready
        assertTrue(
            FlexAuth.verify(r.key, FlexAuth.ROLE_PHONE, p.nonce, 0, r.proof),
            "la prueba no se verifica con su propia clave",
        )
        // Y con la clave de OTRO codigo, no.
        val otra = FlexAuth.deriveKey("098766", p.salt, hostId, phoneId)
        assertFalse(
            FlexAuth.verify(otra, FlexAuth.ROLE_PHONE, p.nonce, 0, r.proof),
            "la prueba se verifico con la clave de otro codigo",
        )
    }
}
