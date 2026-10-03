package com.flexos.flexphone.protocol

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

/**
 * LA MAQUINA DE FASES DEL ENLACE, con cada sintoma de los videos escrito como
 * una secuencia de eventos.
 *
 * Todo JVM puro: `gradle :protocol:test`.
 */
class LinkPhaseTest {

    private fun m(bonded: Boolean = false) = LinkPhaseMachine(bonded).also { it.on(LinkEvent.Started) }
    private fun LinkPhaseMachine.go(vararg e: LinkEvent): LinkPhase { var p = phase; for (x in e) p = on(x); return p }

    // =========================================================
    //  El recorrido feliz, con los nombres de la peticion
    // =========================================================
    @Test
    fun `el emparejamiento recorre las fases en orden`() {
        val s = LinkPhaseMachine()
        assertEquals(LinkPhase.STOPPED, s.phase)                    // DISCONNECTED
        assertEquals(LinkPhase.DISCOVERING, s.on(LinkEvent.Started))
        assertEquals(LinkPhase.CONNECTING, s.on(LinkEvent.ChannelOpened))
        assertEquals(LinkPhase.CODE_ENTRY, s.on(LinkEvent.PairSalt)) // el codigo esta en la pantalla de Flex OS
        assertEquals(LinkPhase.PAIRING, s.on(LinkEvent.ProofSent))
        assertEquals(LinkPhase.PAIRED, s.on(LinkEvent.PairingAccepted))
        assertEquals(LinkPhase.CONNECTED, s.on(LinkEvent.SessionOpened))
        assertTrue(s.bonded)
    }

    // =========================================================
    //  SINTOMA 2: el socket parpadea MIENTRAS se empareja
    // =========================================================
    @Test
    fun `un socket que se cae con codigo en pantalla NO borra el emparejamiento`() {
        val s = m()
        s.go(LinkEvent.ChannelOpened, LinkEvent.PairSalt)
        assertEquals(LinkPhase.CODE_ENTRY, s.phase)
        // Se cae el canal (el reloj lo reabre en un segundo).
        assertEquals(LinkPhase.PAIRING_RECOVERY, s.on(LinkEvent.ChannelClosed(authenticated = false)))
        assertTrue(s.pairingAlive, "el emparejamiento tiene que seguir vivo")
        // Vuelve el canal y la sal: otra vez a teclear, sin pasar por "buscando".
        assertEquals(LinkPhase.PAIRING_RECOVERY, s.on(LinkEvent.ChannelOpened))
        assertEquals(LinkPhase.CODE_ENTRY, s.on(LinkEvent.PairSalt))
    }

    @Test
    fun `el codigo ya enviado sobrevive a la caida y se reenvia`() {
        val s = m()
        s.go(LinkEvent.ChannelOpened, LinkEvent.PairSalt, LinkEvent.ProofSent)
        assertEquals(LinkPhase.PAIRING, s.phase)
        assertEquals(LinkPhase.PAIRING_RECOVERY, s.on(LinkEvent.ChannelClosed(false)))
        s.on(LinkEvent.ChannelOpened)
        assertEquals(LinkPhase.PAIRING, s.on(LinkEvent.ProofSent))   // la prueba se reenvia sola
    }

    @Test
    fun `una prueba que no pudo salir no es un codigo rechazado`() {
        val s = m()
        s.go(LinkEvent.ChannelOpened, LinkEvent.PairSalt, LinkEvent.ProofSent, LinkEvent.ChannelClosed(false))
        assertEquals(LinkPhase.PAIRING_RECOVERY, s.on(LinkEvent.ProofNotSent))
        assertTrue(s.pairingAlive)
    }

    @Test
    fun `codigo rechazado vuelve a pedir el codigo y no tira la sesion`() {
        val s = m()
        s.go(LinkEvent.ChannelOpened, LinkEvent.PairSalt, LinkEvent.ProofSent)
        assertEquals(LinkPhase.CODE_ENTRY, s.on(LinkEvent.CodeRejected))
        assertTrue(s.pairingAlive)
    }

    @Test
    fun `un emparejamiento que caduca vuelve a buscar`() {
        val s = m()
        s.go(LinkEvent.ChannelOpened, LinkEvent.PairSalt)
        assertEquals(LinkPhase.CONNECTING, s.on(LinkEvent.PairingEnded))   // canal abierto, sin vinculo
        s.on(LinkEvent.ChannelClosed(false))
        assertEquals(LinkPhase.DISCOVERING, s.phase)
    }

    // =========================================================
    //  SINTOMA 2/3: emparejar bien y que el socket parpadee un segundo
    // =========================================================
    @Test
    fun `tras emparejar, un corte de un segundo NO vuelve a Esperando a Flex OS`() {
        val s = m()
        s.go(LinkEvent.ChannelOpened, LinkEvent.PairSalt, LinkEvent.ProofSent, LinkEvent.PairingAccepted)
        assertTrue(s.bonded, "el vinculo se guarda en el acto")
        // El socket parpadea ANTES de que llegue SessionOpened.
        assertEquals(LinkPhase.CONNECTION_LOST, s.on(LinkEvent.ChannelClosed(authenticated = false)))
        assertTrue(s.bonded, "un corte no deshace el vinculo")
        assertFalse(s.phase == LinkPhase.DISCOVERING, "no puede volver a 'buscando'")
        // Vuelve el canal y la sesion.
        assertEquals(LinkPhase.RECONNECTING, s.on(LinkEvent.ChannelOpened))
        assertEquals(LinkPhase.CONNECTED, s.on(LinkEvent.SessionOpened))
    }

    @Test
    fun `una sesion abierta que se cae pasa a CONNECTION_LOST y luego a RECONNECTING`() {
        val s = m(bonded = true)
        s.go(LinkEvent.ChannelOpened, LinkEvent.SessionOpened)
        assertEquals(LinkPhase.CONNECTED, s.phase)
        assertEquals(LinkPhase.CONNECTION_LOST, s.on(LinkEvent.ChannelClosed(true)))
        // Un canal que se abre y se cae sin autenticar no inventa 'buscando'.
        assertEquals(LinkPhase.RECONNECTING, s.on(LinkEvent.ChannelOpened))
        assertEquals(LinkPhase.RECONNECTING, s.on(LinkEvent.ChannelClosed(false)))
        assertEquals(LinkPhase.CONNECTED, s.go(LinkEvent.ChannelOpened, LinkEvent.SessionOpened))
    }

    @Test
    fun `reinicio de la app con vinculo guardado arranca en RECONNECTING, no en buscando`() {
        val s = LinkPhaseMachine(bonded = true)
        assertEquals(LinkPhase.RECONNECTING, s.on(LinkEvent.Started))
    }

    // =========================================================
    //  SINTOMA 1: un fallo de enlace NO es un error de la app
    // =========================================================
    @Test
    fun `un canal que no se presento no lleva a ERROR`() {
        val s = m(bonded = true)
        s.go(LinkEvent.ChannelOpened, LinkEvent.SessionOpened)
        // Alguien sondea el puerto: se cierra sin presentarse. La sesion buena no se toca.
        assertEquals(LinkPhase.CONNECTED, s.on(LinkEvent.LinkFailure))
        s.on(LinkEvent.ChannelClosed(true))
        assertEquals(LinkPhase.RECONNECTING, s.on(LinkEvent.LinkFailure))
        assertFalse(s.phase == LinkPhase.ERROR)
    }

    @Test
    fun `solo un Fatal lleva a ERROR y arrancar de nuevo lo limpia`() {
        val s = m()
        assertEquals(LinkPhase.ERROR, s.on(LinkEvent.Fatal("el puerto 47820 ya esta en uso")))
        assertEquals("el puerto 47820 ya esta en uso", s.fatalReason)
        assertEquals(LinkPhase.DISCOVERING, s.on(LinkEvent.Started))
        assertEquals(null, s.fatalReason)
    }

    // =========================================================
    //  Parar y olvidar
    // =========================================================
    @Test
    fun `parar cierra el emparejamiento pero conserva el vinculo`() {
        val s = m(bonded = true)
        s.go(LinkEvent.ChannelOpened, LinkEvent.PairSalt)
        assertEquals(LinkPhase.STOPPED, s.on(LinkEvent.Stopped))
        assertFalse(s.pairingAlive)
        assertTrue(s.bonded)
        assertEquals(LinkPhase.RECONNECTING, s.on(LinkEvent.Started))
    }

    @Test
    fun `olvidar el vinculo es lo unico que lo apaga`() {
        val s = m(bonded = true)
        s.go(LinkEvent.ChannelOpened, LinkEvent.SessionOpened, LinkEvent.ChannelClosed(true))
        assertTrue(s.bonded)
        s.on(LinkEvent.BondForgotten)
        assertFalse(s.bonded)
        assertEquals(LinkPhase.DISCOVERING, s.phase)
    }

    @Test
    fun `tras parar no hay eventos que resuciten la fase`() {
        val s = m()
        s.on(LinkEvent.Stopped)
        assertEquals(LinkPhase.STOPPED, s.on(LinkEvent.ChannelOpened))
        assertEquals(LinkPhase.STOPPED, s.on(LinkEvent.ChannelClosed(false)))
        assertEquals(LinkPhase.STOPPED, s.on(LinkEvent.LinkFailure))
    }
}
