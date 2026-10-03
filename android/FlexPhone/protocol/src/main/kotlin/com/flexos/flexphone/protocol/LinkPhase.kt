package com.flexos.flexphone.protocol

/**
 * LAS FASES DEL ENLACE -- una sola maquina de estados, explicita y sin Android.
 *
 * POR QUE EXISTE
 * --------------
 * Hasta aqui el estado de la pantalla (`LinkState`) lo escribia cada evento del
 * servidor por su cuenta, y el resultado era que un hipo de la red DESTRUIA lo
 * que ya se habia conseguido:
 *
 *  · se cae el socket mientras se teclea el codigo -> la pantalla pasaba a
 *    "buscando" y el campo del codigo desaparecia, aunque la sesion de
 *    emparejamiento seguia viva en el servidor;
 *  · se cierra una conexion que nunca se presento (un puerto que alguien
 *    sondea) -> `PairingFailed(LINK)` -> ERROR, y la app ensenaba "No se pudo
 *    emparejar" a quien llevaba horas conectado;
 *  · se empareja bien y el socket parpadea un segundo -> "Esperando a Flex OS",
 *    como si no se hubiera emparejado nada, cuando el vinculo YA estaba
 *    guardado en el Keystore.
 *
 * Las reglas que lo arreglan viven aqui, en un sitio, y se prueban en el PC
 * (`LinkPhaseTest`):
 *
 *  1. UN SOCKET QUE SE CAE NO DESTRUYE LO CONSEGUIDO. Con sesion de
 *     emparejamiento viva se pasa a [LinkPhase.PAIRING_RECOVERY] (se conserva);
 *     con vinculo guardado, a [LinkPhase.RECONNECTING]; solo sin ninguna de las
 *     dos cosas se vuelve a [LinkPhase.DISCOVERING].
 *  2. EL EMPAREJAMIENTO CONSEGUIDO SE GUARDA YA. [Event.PairingAccepted] marca
 *     `bonded` en el acto y nada de lo que venga despues (un cierre, un error
 *     de red) lo deshace: solo [Event.BondForgotten] lo hace.
 *  3. UN FALLO DE ENLACE NO ES UN ERROR DE LA APP. Solo [Event.Fatal] lleva a
 *     [LinkPhase.ERROR] (no se pudo abrir el puerto, version incompatible).
 *
 * Kotlin/JVM puro: lo usa `FlexLinkService` y lo prueba un JDK.
 */
enum class LinkPhase {
    /** El servicio esta apagado. (DISCONNECTED) */
    STOPPED,
    /** Escuchando; ningun Flex OS ha abierto canal. (DISCOVERING) */
    DISCOVERING,
    /** Hay un canal abierto y todavia no hay vinculo ni emparejamiento. (CONNECTING) */
    CONNECTING,
    /** Hay sesion de emparejamiento viva: se espera que la persona teclee el codigo. (CODE_DISPLAYED) */
    CODE_ENTRY,
    /** Se mando la prueba del codigo; se espera la respuesta de Flex OS. (PAIRING) */
    PAIRING,
    /** Se cayo el canal MIENTRAS habia emparejamiento vivo: se conserva. (PAIRING_RECOVERY) */
    PAIRING_RECOVERY,
    /** Flex OS demostro su identidad y el vinculo esta guardado; la sesion se abre ya. (PAIRED) */
    PAIRED,
    /** Sesion abierta y autenticada. SOLO aqui se puede decir "conectado". (CONNECTED) */
    CONNECTED,
    /** Se acaba de caer una sesion que estaba abierta. (CONNECTION_LOST) */
    CONNECTION_LOST,
    /** Vinculado, sin sesion: se espera a que Flex OS vuelva. (RECONNECTING) */
    RECONNECTING,
    /** Fallo de verdad de este lado (puerto ocupado, version). (ERROR) */
    ERROR,
}

/** Lo que puede pasar. Cada evento lo produce UN sitio del servidor. */
sealed class LinkEvent {
    object Started : LinkEvent()
    object Stopped : LinkEvent()
    /** Un Flex OS abrio un canal TCP. */
    object ChannelOpened : LinkEvent()
    /** Se cerro un canal. [authenticated]: ¿habia sesion abierta? */
    data class ChannelClosed(val authenticated: Boolean) : LinkEvent()
    /** Llego la sal de Flex OS: hay una sesion de emparejamiento viva. */
    object PairSalt : LinkEvent()
    /** El emparejamiento termino sin vinculo: caduco, se cancelo o se rindio. */
    object PairingEnded : LinkEvent()
    /** La persona tecleo el codigo y la prueba salio (o se reenvia tras reconectar). */
    object ProofSent : LinkEvent()
    /** Flex OS dijo que el codigo no coincide: se puede corregir. */
    object CodeRejected : LinkEvent()
    /** La prueba NO pudo salir (canal caido): el codigo se conserva. */
    object ProofNotSent : LinkEvent()
    /** Flex OS demostro su identidad y el vinculo YA esta guardado. */
    object PairingAccepted : LinkEvent()
    /** Sesion abierta y autenticada. */
    object SessionOpened : LinkEvent()
    /** Un fallo de enlace que no es de la app (se cerro un canal sin presentarse...). */
    object LinkFailure : LinkEvent()
    /** Fallo de este lado: el servicio no puede seguir. */
    data class Fatal(val why: String) : LinkEvent()
    /** La persona olvido el vinculo. */
    object BondForgotten : LinkEvent()
}

/**
 * La maquina. NO es segura entre hilos por si sola: el servicio la usa bajo un
 * cerrojo (los eventos llegan de varios hilos de conexion).
 */
class LinkPhaseMachine(
    bonded: Boolean = false,
) {
    var phase: LinkPhase = LinkPhase.STOPPED
        private set

    /** Hay un vinculo GUARDADO. Solo [LinkEvent.BondForgotten] lo apaga. */
    var bonded: Boolean = bonded
        private set

    /** Hay una sesion de emparejamiento viva (codigo por teclear o ya enviado). */
    var pairingAlive: Boolean = false
        private set

    /** Hay un canal abierto ahora mismo. */
    var channelOpen: Boolean = false
        private set

    /** Por que se esta en ERROR, si se esta. */
    var fatalReason: String? = null
        private set

    fun on(e: LinkEvent): LinkPhase {
        when (e) {
            LinkEvent.Started -> {
                fatalReason = null
                channelOpen = false
                phase = idlePhase(afterSession = false)
            }
            LinkEvent.Stopped -> {
                // Parar es una orden de la persona: el emparejamiento en curso
                // se cierra con el servidor (el vinculo guardado, no).
                pairingAlive = false
                channelOpen = false
                phase = LinkPhase.STOPPED
            }
            LinkEvent.ChannelOpened -> {
                channelOpen = true
                // Un canal NUEVO no cambia lo que ya se sabe: con emparejamiento
                // vivo se sigue en recuperacion hasta que llegue la sal; con
                // vinculo se sigue "reconectando".
                if (phase != LinkPhase.STOPPED && phase != LinkPhase.ERROR && phase != LinkPhase.CONNECTED &&
                    phase != LinkPhase.PAIRED) {
                    phase = when {
                        pairingAlive -> LinkPhase.PAIRING_RECOVERY
                        bonded -> LinkPhase.RECONNECTING
                        else -> LinkPhase.CONNECTING
                    }
                }
            }
            is LinkEvent.ChannelClosed -> {
                channelOpen = false
                if (phase == LinkPhase.STOPPED || phase == LinkPhase.ERROR) return phase
                phase = when {
                    // 1) Se cayo una sesion abierta (o un vinculo recien guardado).
                    e.authenticated || phase == LinkPhase.CONNECTED || phase == LinkPhase.PAIRED ->
                        LinkPhase.CONNECTION_LOST
                    // 2) Habia emparejamiento vivo: se conserva el codigo y la pantalla.
                    pairingAlive -> LinkPhase.PAIRING_RECOVERY
                    // 3) Vinculado: se espera a que Flex OS vuelva.
                    bonded -> if (phase == LinkPhase.CONNECTION_LOST) LinkPhase.CONNECTION_LOST else LinkPhase.RECONNECTING
                    else -> LinkPhase.DISCOVERING
                }
            }
            LinkEvent.PairSalt -> {
                pairingAlive = true
                if (phase != LinkPhase.STOPPED && phase != LinkPhase.ERROR && phase != LinkPhase.CONNECTED)
                    phase = LinkPhase.CODE_ENTRY
            }
            LinkEvent.PairingEnded -> {
                pairingAlive = false
                if (phase == LinkPhase.CODE_ENTRY || phase == LinkPhase.PAIRING || phase == LinkPhase.PAIRING_RECOVERY)
                    phase = idlePhase(afterSession = false)
            }
            LinkEvent.ProofSent -> {
                if (pairingAlive && phase != LinkPhase.STOPPED && phase != LinkPhase.ERROR) phase = LinkPhase.PAIRING
            }
            LinkEvent.CodeRejected, LinkEvent.ProofNotSent -> {
                if (pairingAlive && phase != LinkPhase.STOPPED && phase != LinkPhase.ERROR)
                    phase = if (channelOpen) LinkPhase.CODE_ENTRY else LinkPhase.PAIRING_RECOVERY
            }
            LinkEvent.PairingAccepted -> {
                // EL VINCULO SE GUARDA YA. Desde aqui nada lo deshace salvo olvidarlo.
                bonded = true
                pairingAlive = false
                phase = LinkPhase.PAIRED
            }
            LinkEvent.SessionOpened -> {
                pairingAlive = false
                channelOpen = true
                phase = LinkPhase.CONNECTED
            }
            LinkEvent.LinkFailure -> {
                // Un canal que no se presento, una prueba sin respuesta...: del
                // enlace, no de la app. NO se pasa a ERROR ni se pierde nada.
                if (phase == LinkPhase.STOPPED || phase == LinkPhase.ERROR || phase == LinkPhase.CONNECTED) return phase
                phase = when {
                    pairingAlive -> LinkPhase.PAIRING_RECOVERY
                    bonded -> LinkPhase.RECONNECTING
                    else -> if (channelOpen) LinkPhase.CONNECTING else LinkPhase.DISCOVERING
                }
            }
            is LinkEvent.Fatal -> {
                fatalReason = e.why
                pairingAlive = false
                channelOpen = false
                phase = LinkPhase.ERROR
            }
            LinkEvent.BondForgotten -> {
                bonded = false
                pairingAlive = false
                if (phase != LinkPhase.STOPPED && phase != LinkPhase.ERROR) phase = idlePhase(afterSession = false)
            }
        }
        return phase
    }

    /** A donde se vuelve cuando no queda nada en curso. */
    private fun idlePhase(@Suppress("UNUSED_PARAMETER") afterSession: Boolean): LinkPhase = when {
        pairingAlive -> LinkPhase.CODE_ENTRY
        channelOpen && !bonded -> LinkPhase.CONNECTING
        bonded -> LinkPhase.RECONNECTING
        else -> LinkPhase.DISCOVERING
    }
}
