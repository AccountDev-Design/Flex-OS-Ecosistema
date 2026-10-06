package com.flexos.flexphone.domain

import com.flexos.flexphone.protocol.LinkPhase
import com.flexos.flexphone.protocol.MediaState
import com.flexos.flexphone.protocol.NotifPayload
import com.flexos.flexphone.protocol.PairFailure
import com.flexos.flexphone.protocol.RelayInfo
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Estado del enlace, tal como lo ve la app. */
enum class LinkState {
    /** Bluetooth apagado, permisos sin conceder o sin adaptador. */
    UNAVAILABLE,
    /** Todo listo, pero el usuario no lo ha activado. */
    OFF,
    /** Anunciando; esperando a que Flex OS conecte. */
    ADVERTISING,
    /** Hay conexion GATT, aun sin sesion Flex Link. */
    CONNECTING,
    /** Mostrando codigo; falta confirmar en los dos lados. */
    PAIRING,
    /** Sesion abierta. SOLO aqui se puede decir "conectado". */
    READY,
    /** Fallo real; `error` explica cual. */
    ERROR,
    /**
     * Hay un vinculo guardado y la sesion no esta abierta AHORA: se acaba de
     * caer o Flex OS todavia no ha vuelto. NO es "esperando a Flex OS" (eso
     * es para quien aun no se ha emparejado nunca) ni un error.
     */
    RECONNECTING,
}

/** El vinculo con Flex OS, tal como lo ve la pantalla. La verdad la tiene BondStore. */
data class BondInfo(val paired: Boolean = false, val peerName: String? = null)

/**
 * El emparejamiento en curso: lo unico que la pantalla del codigo necesita
 * saber, y que antes se PREGUNTABA al servicio cada segundo.
 *
 * [expiresAtMs] es un instante del reloj del telefono (0 = no hay sesion). La
 * cuenta atras se calcula con el en la pantalla: no hace falta ningun sondeo.
 */
data class PairingInfo(val awaitingCode: Boolean = false, val expiresAtMs: Long = 0L)

/** Traduccion de la fase de la maquina (LinkPhase) al estado que pintan las pantallas. */
fun LinkPhase.toLinkState(): LinkState = when (this) {
    LinkPhase.STOPPED -> LinkState.OFF
    LinkPhase.DISCOVERING -> LinkState.ADVERTISING
    LinkPhase.CONNECTING -> LinkState.CONNECTING
    LinkPhase.CODE_ENTRY, LinkPhase.PAIRING, LinkPhase.PAIRING_RECOVERY -> LinkState.PAIRING
    LinkPhase.PAIRED, LinkPhase.CONNECTED -> LinkState.READY
    LinkPhase.CONNECTION_LOST, LinkPhase.RECONNECTING -> LinkState.RECONNECTING
    LinkPhase.ERROR -> LinkState.ERROR
}

enum class RelayState { OFF, STARTING, UP, ERROR, SUSPENDED }

/**
 * Quien hay al otro lado del relay, con el relay ARRIBA:
 *   · CONNECTED    -> Flex OS conectado: "Relay activo";
 *   · RECONNECTING -> Flex OS se acaba de ir y sus pestanas se conservan
 *                     esperando la reconexion: "Reconectando";
 *   · NONE         -> nadie: "Desconectado (esperando a Flex OS)".
 */
enum class RelayClient { NONE, CONNECTED, RECONNECTING }

/**
 * El texto UNICO del estado del relay, el mismo en la portada, en la pantalla
 * del relay y en cualquier otro sitio: cuatro estados claros y distintos
 * (activo / reconectando / desconectado / detenido por el usuario) mas los de
 * arranque y fallo. [short] es la version para una celda pequena.
 */
fun relayStatusText(state: RelayState, client: RelayClient, stoppedByUser: Boolean, short: Boolean = false): String =
    when (state) {
        RelayState.UP -> when (client) {
            RelayClient.CONNECTED -> "Relay activo"
            RelayClient.RECONNECTING -> "Reconectando"
            RelayClient.NONE -> if (short) "Desconectado" else "Desconectado (esperando a Flex OS)"
        }
        RelayState.STARTING -> "Arrancando"
        RelayState.ERROR -> "Con error"
        RelayState.SUSPENDED -> if (short) "Suspendido" else "Suspendido por Android"
        RelayState.OFF -> if (stoppedByUser) (if (short) "Detenido" else "Detenido por el usuario") else "Parado"
    }

/**
 * Estado observable de Flex Phone.
 *
 * UNA SOLA INSTANCIA, creada por [com.flexos.flexphone.FlexPhoneApp].
 * Se expone con `StateFlow` y NO con sondeo: nada en esta app
 * pregunta "¿ha cambiado algo?" en un bucle. Los cambios llegan por
 * eventos (Android avisa de la notificacion, del estado del enlace y
 * de la sesion multimedia) y la UI se recompone sola.
 *
 * REGLA QUE ATRAVIESA TODO EL FICHERO: ningun campo se rellena "por
 * si acaso". Si no hay dato real, el campo es `null` o el estado es
 * el que corresponde -- nunca un valor de relleno que la pantalla
 * pinte como si fuera cierto.
 */
class FlexPhoneState(
    @Volatile var settings: Settings = Settings(),
) {
    companion object {
        @Volatile var instance: FlexPhoneState? = null
    }

    private val _link = MutableStateFlow(LinkState.OFF)
    val link: StateFlow<LinkState> = _link.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    /** La fase fina del enlace (la que decide [link]). */
    private val _phase = MutableStateFlow(LinkPhase.STOPPED)
    val phase: StateFlow<LinkPhase> = _phase.asStateFlow()

    /**
     * El vinculo guardado, OBSERVABLE. Antes la navegacion lo leia una vez al
     * abrir la app para elegir la pantalla de inicio, asi que emparejar bien
     * no cambiaba nada hasta cerrar y volver a abrir Flex Phone.
     */
    private val _bond = MutableStateFlow(BondInfo())
    val bond: StateFlow<BondInfo> = _bond.asStateFlow()
    fun setBond(paired: Boolean, peerName: String?) { _bond.value = BondInfo(paired, peerName) }

    /** La IPv4 Wi-Fi de este telefono (la que Android da), observable: un cambio de IP se ve al instante. */
    private val _address = MutableStateFlow<String?>(null)
    val address: StateFlow<String?> = _address.asStateFlow()
    fun setAddress(a: String?) { _address.value = a }

    /**
     * El ultimo aviso del enlace que NO es un error de la app (un reloj que no
     * contesto, un canal que se cerro, una prueba que no llego). Se ensena en
     * pequeno; la pantalla de ERROR queda para fallos de verdad.
     */
    private val _notice = MutableStateFlow<String?>(null)
    val notice: StateFlow<String?> = _notice.asStateFlow()
    fun setNotice(n: String?) { _notice.value = n }

    private val _pairing = MutableStateFlow(PairingInfo())
    val pairing: StateFlow<PairingInfo> = _pairing.asStateFlow()
    fun setPairing(awaitingCode: Boolean, remainingMs: Long) {
        _pairing.value = PairingInfo(awaitingCode, if (awaitingCode && remainingMs > 0) System.currentTimeMillis() + remainingMs else 0L)
    }

    // #########################################################
    // ##  AQUI NO HAY NINGUN "CODIGO DE EMPAREJAMIENTO"
    // ##  --------------------------------------------------
    // ##  Habia un `pairCode` que nadie escribia nunca: resto de
    // ##  cuando el enlace iba por BLE y era el telefono quien
    // ##  ensenaba el codigo. Hoy el codigo nace y vive en Flex OS
    // ##  -- una sola fuente --, y este lado solo sabe lo que el
    // ##  usuario TECLEA. Dejar aqui un campo con ese nombre
    // ##  invitaba justo al fallo que se estaba persiguiendo: dos
    // ##  sitios distintos con un codigo cada uno.
    // #########################################################

    /**
     * Lo que el usuario lleva tecleado en la pantalla de
     * emparejamiento.
     *
     * Vive aqui, y no dentro del `remember` de la pantalla, por un
     * motivo concreto: cuando Flex OS abre una sesion NUEVA (porque el
     * usuario pulso otra vez "Emparejar telefono" en el reloj), el
     * codigo a medio teclear pertenece a la sesion anterior y hay que
     * borrarlo. Desde el servicio no se puede tocar un `remember`.
     */
    private val _typedCode = MutableStateFlow("")
    val typedCode: StateFlow<String> = _typedCode.asStateFlow()

    fun setTypedCode(code: String) { _typedCode.value = code }
    fun clearTypedCode() { _typedCode.value = "" }

    /**
     * POR QUE FALLO EL ULTIMO INTENTO, de verdad.
     *
     * La pantalla decia siempre "comprueba que el codigo es el mismo",
     * tambien cuando lo que se habia caido era el socket. Con esto el
     * mensaje corresponde al fallo real.
     */
    private val _pairFailure = MutableStateFlow<PairFailure?>(null)
    val pairFailure: StateFlow<PairFailure?> = _pairFailure.asStateFlow()

    fun setPairFailure(kind: PairFailure?) { _pairFailure.value = kind }

    private val _relay = MutableStateFlow(RelayState.OFF)
    val relay: StateFlow<RelayState> = _relay.asStateFlow()

    private val _relayInfo = MutableStateFlow<RelayInfo?>(null)
    val relayInfo: StateFlow<RelayInfo?> = _relayInfo.asStateFlow()

    /** Cliente del relay (ver [RelayClient]). */
    private val _relayClient = MutableStateFlow(RelayClient.NONE)
    val relayClient: StateFlow<RelayClient> = _relayClient.asStateFlow()
    fun setRelayClient(c: RelayClient) { _relayClient.value = c }

    /** El relay esta parado PORQUE la persona lo paro (no Android, no un error). */
    private val _relayStoppedByUser = MutableStateFlow(false)
    val relayStoppedByUser: StateFlow<Boolean> = _relayStoppedByUser.asStateFlow()

    private val _media = MutableStateFlow<MediaState?>(null)
    val media: StateFlow<MediaState?> = _media.asStateFlow()

    /**
     * Relojes que han CONTESTADO a una busqueda en esta red.
     *
     * Solo entra aqui lo que ha contestado de verdad: la lista no se
     * rellena con "lo que habia la ultima vez" ni con candidatos
     * inventados. Si esta vacia es porque no contesto nadie, y eso es
     * un dato -- no un fallo de la pantalla.
     */
    data class Watch(
        val id: String,
        val name: String,
        val address: String,
        /** El reloj esta AHORA ensenando un codigo de emparejamiento. */
        val pairing: Boolean,
    )
    private val _watches = MutableStateFlow<List<Watch>>(emptyList())
    val watches: StateFlow<List<Watch>> = _watches.asStateFlow()

    fun setWatches(w: List<Watch>) { _watches.value = w }

    /** Contadores de diagnostico. NUNCA contenido de mensajes. */
    data class Diag(
        val sent: Long = 0, val received: Long = 0,
        val dropped: Long = 0, val badFrames: Long = 0,
        val reconnects: Long = 0,
        val repliesOk: Long = 0, val repliesFailed: Long = 0,
        val notificationsForwarded: Long = 0, val notificationsFiltered: Long = 0,
    )
    private val _diag = MutableStateFlow(Diag())
    val diag: StateFlow<Diag> = _diag.asStateFlow()

    /** Callback que instala el servicio del enlace para enviar. */
    @Volatile var sender: ((type: Int, payload: ByteArray) -> Boolean)? = null

    // ---------------------------------------------------------
    //  Transiciones del enlace
    // ---------------------------------------------------------
    /**
     * Pone la FASE del enlace (la maquina `LinkPhaseMachine` del servicio) y
     * deriva de ella el estado de las pantallas.
     */
    fun setPhase(p: LinkPhase, why: String? = null) {
        _phase.value = p
        setLink(p.toLinkState(), why)
    }

    fun setLink(s: LinkState, why: String? = null) {
        _link.value = s
        _error.value = why
        // Un fallo viejo no puede seguir explicando una pantalla que ya
        // no tiene nada que ver: se borra al conseguir sesion o al apagar. NO
        // al cambiar de fase: con el emparejamiento en curso la pantalla del
        // codigo SIGUE viva (PAIRING) y tiene que poder explicar por que fallo.
        if (s == LinkState.READY || s == LinkState.OFF) { _pairFailure.value = null; _notice.value = null }
        if (s == LinkState.READY) _typedCode.value = ""
        if (s != LinkState.READY) {
            // Al perder la sesion, lo que dependia de ella deja de ser
            // cierto: no se conserva un estado multimedia de hace un
            // rato como si siguiera sonando.
            _media.value = null
            // OJO: el RELAY NO se toca. Es un servicio aparte (otro puerto, otro
            // servicio en primer plano) que sigue vivo aunque se caiga el enlace.
            // Antes aqui se ponia a OFF y se borraba su direccion: la pantalla
            // decia "Parado" con el servidor escuchando, y tras CADA reconexion
            // Flex OS creia que el relay estaba apagado porque nadie se lo
            // volvia a decir (ver reannounceRelay).
        }
    }

    fun countReconnect() = _diag.update { it.copy(reconnects = it.reconnects + 1) }
    fun countSent() = _diag.update { it.copy(sent = it.sent + 1) }
    fun countReceived() = _diag.update { it.copy(received = it.received + 1) }
    fun countDropped() = _diag.update { it.copy(dropped = it.dropped + 1) }
    fun countBadFrame() = _diag.update { it.copy(badFrames = it.badFrames + 1) }
    fun countReply(ok: Boolean) = _diag.update {
        if (ok) it.copy(repliesOk = it.repliesOk + 1) else it.copy(repliesFailed = it.repliesFailed + 1)
    }

    // ---------------------------------------------------------
    //  Notificaciones
    // ---------------------------------------------------------
    fun onNotificationPosted(p: NotifPayload) {
        // Sin sesion no se manda nada -- ni se encola "para luego".
        // Una notificacion de hace media hora entregada al reconectar
        // seria ruido, no informacion.
        val s = sender
        if (s == null) {
            _diag.update { it.copy(notificationsFiltered = it.notificationsFiltered + 1) }
            return
        }
        if (s(com.flexos.flexphone.protocol.FlexLink.T_NOTIF_ADD, p.encode()))
            _diag.update { it.copy(notificationsForwarded = it.notificationsForwarded + 1) }
        else
            _diag.update { it.copy(dropped = it.dropped + 1) }
    }

    fun onNotificationRemoved(id: Long) {
        val s = sender ?: return
        val body = com.flexos.flexphone.protocol.PayloadWriter().u32(id).build()
        s(com.flexos.flexphone.protocol.FlexLink.T_NOTIF_REMOVE, body)
    }

    // ---------------------------------------------------------
    //  Multimedia y relay
    // ---------------------------------------------------------
    fun setMedia(m: MediaState?) {
        _media.value = m
        val s = sender ?: return
        if (m != null) s(com.flexos.flexphone.protocol.FlexLink.T_MEDIA_STATE, m.encode())
    }

    /**
     * Vuelve a decirle a Flex OS como esta el relay. Se llama al abrirse una
     * sesion (Flex OS borra su copia del estado del relay al perder el enlace, y
     * sin esto no se enteraba de que el relay seguia arriba) y cuando cambia la
     * direccion Wi-Fi del telefono. No cambia nada local.
     */
    fun reannounceRelay() {
        val state = _relay.value
        val info = _relayInfo.value
        if (state == RelayState.OFF && info == null && !_relayStoppedByUser.value) return
        sendRelayInfo(state, info)
    }

    /**
     * [byUser] solo cuenta con [RelayState.OFF]: la persona lo paro a proposito
     * (boton de la app, de la notificacion o desde Flex OS). Se le dice asi al
     * P4, que lo ensena como "Detenido por el usuario" y no como un error.
     */
    fun setRelay(state: RelayState, info: RelayInfo? = null, byUser: Boolean = false) {
        _relayStoppedByUser.value = state == RelayState.OFF && byUser
        _relay.value = state
        _relayInfo.value = info
        sendRelayInfo(state, info)
    }

    private fun sendRelayInfo(state: RelayState, info: RelayInfo?) {
        val s = sender ?: return
        // Se anuncia SIEMPRE el estado real, error incluido: el P4
        // tiene que poder mostrar "Android suspendio el relay" en vez
        // de quedarse esperando frames que no van a llegar.
        val byUser = state == RelayState.OFF && _relayStoppedByUser.value
        val payload = info ?: RelayInfo(
            ip = byteArrayOf(0, 0, 0, 0), port = 0, protoVer = 1, tls = false, caps = 0,
            error = when (state) {
                RelayState.SUSPENDED -> "Android suspendio el relay"
                RelayState.ERROR -> "el relay no pudo arrancar"
                RelayState.OFF -> if (byUser) "detenido por el usuario" else "relay detenido"
                else -> ""
            },
            stoppedByUser = byUser,
            suspended = state == RelayState.SUSPENDED,
        )
        s(com.flexos.flexphone.protocol.FlexLink.T_RELAY_INFO, payload.encode())
    }
}

/** Pequeña ayuda: `update` sobre un MutableStateFlow inmutable. */
private inline fun <T> MutableStateFlow<T>.update(block: (T) -> T) {
    while (true) {
        val cur = value
        val next = block(cur)
        if (compareAndSet(cur, next)) return
    }
}
