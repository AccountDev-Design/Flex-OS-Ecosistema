package com.flexos.flexphone.link

import android.app.*
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.ConnectivityManager
import android.net.LinkProperties
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import com.flexos.flexphone.MainActivity
import com.flexos.flexphone.R
import com.flexos.flexphone.device.DeviceAdapter
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.domain.LinkState
import com.flexos.flexphone.media.MediaBridge
import com.flexos.flexphone.notifications.FlexNotificationListener
import com.flexos.flexphone.notifications.ReplyManager
import com.flexos.flexphone.protocol.*
import com.flexos.flexphone.relay.BrowserRelayService
import com.flexos.flexphone.storage.BondStore
import kotlinx.coroutines.*

/**
 * SERVICIO DEL ENLACE.
 *
 * Mantiene vivo el servidor Wi-Fi al que se conecta Flex OS. Es un
 * servicio en primer plano porque eso es exactamente lo que Android
 * pide para trabajo de red persistente con la pantalla apagada: no
 * se intenta esquivar ninguna proteccion del sistema.
 *
 * POR QUE Wi-Fi Y NO BLE
 * ----------------------
 * El ESP32-P4 de Flex OS Ultra no tiene radio Bluetooth. Mientras
 * eso siga asi, anunciar por BLE seria gastar bateria del telefono
 * para que no llame nadie. [GattServer] sigue en el repositorio como
 * el transporte BLE preparado para el dia que el co-procesador C6 lo
 * ofrezca; hoy NO se arranca, y la app lo dice en pantalla en vez de
 * ensenar un interruptor que no hace nada.
 *
 * CUANDO CORRE Y CUANDO NO
 * ------------------------
 * Solo mientras el usuario ha activado el enlace. No arranca al
 * encender el telefono salvo que se marque la opcion, no se
 * "auto-revive" y se puede parar desde su propia notificacion.
 *
 * LA NOTIFICACION PERSISTENTE DICE LA VERDAD: mientras no hay sesion
 * pone "Esperando a Flex OS", no "Conectado". Un servicio en primer
 * plano que miente sobre su estado es la razon por la que la gente
 * desinstala este tipo de apps.
 */
class FlexLinkService : Service() {

    companion object {
        private const val TAG = "FlexPhone/LinkSvc"
        private const val CHANNEL = "flexlink"
        private const val NOTIF_ID = 1001
        const val ACTION_START = "com.flexos.flexphone.START_LINK"
        const val ACTION_STOP = "com.flexos.flexphone.STOP_LINK"
        /** Cada cuanto se manda el estado del telefono si NADA cambio. */
        private const val STATE_HEARTBEAT_MS = 60_000L

        fun start(ctx: Context) {
            val i = Intent(ctx, FlexLinkService::class.java).setAction(ACTION_START)
            if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i) else ctx.startService(i)
        }
        fun stop(ctx: Context) {
            ctx.startService(Intent(ctx, FlexLinkService::class.java).setAction(ACTION_STOP))
        }
        /** El servidor vivo, para que la interfaz pueda entregar el codigo. */
        @Volatile var current: FlexLinkService? = null
    }

    private var server: WifiLinkServer? = null
    private var media: MediaBridge? = null
    private lateinit var bonds: BondStore
    private lateinit var device: DeviceAdapter
    // Un fallo dentro de una corrutina de este servicio NO puede tumbar la app: sin un
    // CoroutineExceptionHandler, una excepcion en `loop()` (por ejemplo un
    // SecurityException al preguntar por las sesiones multimedia) llega al manejador
    // de excepciones del hilo y Android MATA EL PROCESO -- y con el, el enlace.
    private val scope = CoroutineScope(
        SupervisorJob() + Dispatchers.Default +
            CoroutineExceptionHandler { _, e -> LinkDiag.w(TAG, "corrutina del servicio fallo: ${e.javaClass.simpleName}") },
    )
    private lateinit var state: FlexPhoneState

    // #########################################################
    // ##  UNA SOLA MAQUINA DE ESTADOS
    // ##  --------------------------------------------------
    // ##  Antes cada evento del servidor escribia el estado de la
    // ##  pantalla por su cuenta (`state.setLink(...)`), y un socket
    // ##  que se caia durante el emparejamiento mandaba la pantalla a
    // ##  "buscando" aunque el codigo siguiera vivo en el servidor.
    // ##  Ahora todos pasan por LinkPhaseMachine (protocol/LinkPhase.kt,
    // ##  probada en el PC) y la pantalla solo OBSERVA su resultado.
    // ##  Los eventos llegan de varios hilos de conexion: se serializan
    // ##  con `phaseLock`.
    // #########################################################
    private lateinit var machine: LinkPhaseMachine
    private val phaseLock = Any()
    private var netCb: ConnectivityManager.NetworkCallback? = null
    @Volatile private var lastWifiIp: String? = null

    // #########################################################
    // ##  AQUI YA NO VIVE NINGUN CODIGO
    // ##  --------------------------------------------------
    // ##  Habia un `typedCode` suelto en este servicio que
    // ##  sobrevivia a los intentos fallidos. Cuando el usuario
    // ##  pulsaba "Emparejar telefono" otra vez, Flex OS abria una
    // ##  sesion NUEVA (codigo y sal nuevos) y esta app contestaba
    // ##  sola con el codigo del intento anterior: Flex OS lo
    // ##  rechazaba y el usuario leia "el codigo no coincide" sin
    // ##  que le hubieran dejado teclear el nuevo.
    // ##
    // ##  El codigo pertenece ahora a la PairingSession que lleva
    // ##  WifiLinkServer, atado a la sal para la que se tecleo.
    // #########################################################

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        // CICLO DE VIDA DEL SERVICIO. Si estas lineas salen justo antes
        // de cada desconexion, el problema no esta en el socket sino en
        // que Android esta recreando el servicio.
        LinkDiag.d(TAG, "SERVICE_CREATED")
        state = FlexPhoneState.instance ?: FlexPhoneState().also { FlexPhoneState.instance = it }
        bonds = BondStore(this)
        device = DeviceAdapter(this)
        machine = LinkPhaseMachine(bonded = bonds.isPaired())
        createChannel()
        current = this
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        LinkDiag.d(TAG, "SERVICE_STARTED action=${intent?.action} server=${if (server == null) "nuevo" else "ya existia"}")
        when (intent?.action) {
            ACTION_STOP -> { shutdown(); return START_NOT_STICKY }
        }
        startForegroundHonestly()

        if (server == null) {
            val s = WifiLinkServer(
                ctx = this,
                phoneId = bonds.selfId(),
                phoneName = device.displayName,
                bondKey = { bonds.key() },
                onPaired = { key, flexosId ->
                    // El vinculo se guarda AQUI, cuando Flex OS ya ha
                    // demostrado que es quien dice ser. Guardarlo antes
                    // dejaria un vinculo con cualquiera que escuchara.
                    //
                    // SE PERSISTE YA (commit, no apply) y la maquina lo
                    // anota en el acto: nada de lo que pase despues -- un
                    // socket que parpadea un segundo -- puede deshacer un
                    // emparejamiento que ya se consiguio.
                    bonds.save(key, flexosId, null)
                    onLinkEvent(LinkEvent.PairingAccepted)
                },
                onEvent = { ev -> onServerEvent(ev) },
                onMessage = { type, payload -> onMessage(type, payload) },
                wifiAddress = { NetAddress.wifiIpv4(this) },
                wifiInterface = { NetAddress.wifiInterface(this) },
            )
            server = s
            if (!device.isOnWifi()) {
                // Sin Wi-Fi no hay a donde conectarse. Se dice y se
                // para: un servicio en primer plano esperando una red
                // que no esta solo gasta bateria.
                state.setLink(LinkState.UNAVAILABLE, getString(R.string.link_needs_wifi))
                updateNotification()
                server = null
                stopSelf()
                return START_NOT_STICKY
            }
            // La maquina arranca ANTES de abrir el puerto: el primer canal puede
            // llegar en cuanto se escucha.
            onLinkEvent(LinkEvent.Started)
            if (!s.start()) {
                updateNotification()
                server = null
                stopSelf()
                return START_NOT_STICKY
            }
            state.sender = { type, payload -> s.send(type, payload) }
            // El relay estaba encendido y Android lo cerro: vuelve ahora, que
            // Flex Phone esta en primer plano y puede arrancarlo. Si la persona
            // lo paro, se queda parado (ver BrowserRelayService.wanted).
            BrowserRelayService.restoreIfWanted(this)
            media = MediaBridge(this, state).also { it.start() }
            lastWifiIp = NetAddress.wifiIpv4(this)
            state.setAddress(lastWifiIp)
            watchNetwork()
            loop()
        }
        // NOT_STICKY a proposito: si Android mata el servicio, no se
        // resucita a espaldas del usuario.
        return START_NOT_STICKY
    }

    // ---------------------------------------------------------
    //  Emparejamiento desde la interfaz
    // ---------------------------------------------------------
    /**
     * El usuario tecleo el codigo que ensena Flex OS.
     *
     * Devuelve el motivo REAL. Antes esto era un booleano y la
     * pantalla tenia que adivinar si un `false` era "aun no ha llegado
     * la sal", "caduco" o "se corto la conexion" -- y acababa diciendo
     * siempre lo mismo.
     */
    fun submitPairingCode(code: String): WifiLinkServer.Result =
        server?.submitPairingCode(code) ?: WifiLinkServer.Result.NO_SESSION

    /** Lo que le queda al codigo del reloj, para que la pantalla no se invente el plazo. */
    fun pairingRemainingMs(): Long = server?.pairingRemainingMs() ?: 0L

    /** ¿Ha llegado la sal de Flex OS? La pantalla lo usa para saber si ya se puede teclear. */
    fun isAwaitingCode(): Boolean = server?.isAwaitingCode() ?: false

    /** Revoca el vinculo: clave fuera y sesion cerrada. */
    fun forgetBond() {
        bonds.clear()
        // Se cierra SOLO la sesion abierta. Antes se paraba y arrancaba el
        // servidor entero (puertos, hilos y cerrojo de multidifusion) para
        // conseguir lo mismo.
        server?.dropSession("vinculo olvidado")
        onLinkEvent(LinkEvent.BondForgotten)
    }

    fun linkAddress(): String? = server?.localWifiAddress() ?: NetAddress.wifiIpv4(this)
    fun linkPort(): Int = server?.port ?: 0
    fun isPaired(): Boolean = bonds.isPaired()
    fun pairedPeer(): String? = bonds.peerId()

    // ---------------------------------------------------------
    //  Buscar relojes
    // ---------------------------------------------------------
    /**
     * Pregunta a la red quien es un Flex OS.
     *
     * Devuelve a cuantos destinos se emitio. CERO significa que este
     * telefono no pudo mandar nada -- sin Wi-Fi o sin direccion --, y
     * la pantalla lo dice en vez de dejar un buscador girando sobre
     * algo que no ha llegado a empezar.
     */
    fun searchWatches(): Int = server?.probeForWatches() ?: 0

    /**
     * Pregunta a UNA direccion, sin difusion.
     *
     * El respaldo para las redes que filtran la difusion o aislan a
     * los clientes: el usuario lee la IP en Flex OS (Flex Phone →
     * Conexion) y la teclea. Al recibir el paquete, el reloj aprende
     * de el la direccion y el puerto de este telefono, asi que puede
     * conectar sin haber descubierto nada por su cuenta.
     */
    fun probeWatchAt(ip: String): Boolean = server?.probeHost(ip) ?: false

    // ---------------------------------------------------------
    //  Eventos del servidor -> maquina de fases -> pantalla
    // ---------------------------------------------------------
    /**
     * Mete un evento en la maquina y publica el resultado. UN solo sitio escribe
     * la fase; la pantalla solo la observa.
     */
    private fun onLinkEvent(e: LinkEvent) {
        synchronized(phaseLock) {
            val before = machine.phase
            val after = machine.on(e)
            if (before != after) LinkDiag.d(TAG, "FASE $before -> $after por ${e::class.simpleName}")
            state.setPhase(after, if (after == LinkPhase.ERROR) machine.fatalReason else null)
            // El estado del emparejamiento y del vinculo, TAL CUAL: la pantalla no
            // tiene que preguntar al servicio cada segundo.
            val srv = server
            state.setPairing(srv?.isAwaitingCode() == true, srv?.pairingRemainingMs() ?: 0L)
            state.setBond(bonds.isPaired(), bonds.peerName()?.takeIf { it.isNotBlank() })
        }
        updateNotification()
    }

    private fun onServerEvent(ev: WifiLinkServer.Event) {
        when (ev) {
            is WifiLinkServer.Event.Listening -> {
                // Informativo: la fase la fija Started (en onStartCommand).
                state.setAddress(NetAddress.wifiIpv4(this) ?: ev.address)
            }
            is WifiLinkServer.Event.Error -> {
                state.setNotice(ev.message)
                // Solo un fallo de ESTE lado lleva a ERROR. Un canal que se
                // descoloca o un reloj que no contesta es del enlace.
                onLinkEvent(if (ev.fatal) LinkEvent.Fatal(ev.message) else LinkEvent.LinkFailure)
            }
            WifiLinkServer.Event.ChannelOpened -> onLinkEvent(LinkEvent.ChannelOpened)
            is WifiLinkServer.Event.SessionOpen -> {
                onLinkEvent(LinkEvent.SessionOpened)
                state.setNotice(null)
                // Al abrir sesion se manda lo que Flex OS necesita para
                // pintar la pantalla: capacidades primero, para que no
                // llegue a ofrecer nada que este telefono no pueda.
                pushCaps()
                pushPhoneState()
                // Flex OS borra su copia del estado del relay cada vez que pierde
                // el enlace: si el relay sigue arriba, se lo volvemos a decir.
                state.reannounceRelay()
            }
            is WifiLinkServer.Event.SessionClosed -> {
                if (ev.authenticated) state.countReconnect()
                onLinkEvent(LinkEvent.ChannelClosed(ev.authenticated))
            }
            is WifiLinkServer.Event.PairingRequested -> {
                // Una sesion NUEVA vacia lo que hubiera tecleado: ese
                // codigo era de la sesion anterior y ya no vale para
                // nada. Dejarlo en el campo invita a pulsar
                // "Emparejar" contra un codigo que ya no existe.
                if (ev.freshSession) { state.clearTypedCode(); state.setPairFailure(null) }
                onLinkEvent(LinkEvent.PairSalt)
            }
            WifiLinkServer.Event.ProofResent -> onLinkEvent(LinkEvent.ProofSent)
            is WifiLinkServer.Event.ProofNotSent -> {
                // El codigo tecleado se CONSERVA: el servidor lo reenvia solo.
                onLinkEvent(LinkEvent.ProofNotSent)
                state.setNotice(ev.why)
            }
            is WifiLinkServer.Event.WatchesFound -> {
                state.setWatches(ev.watches.map {
                    FlexPhoneState.Watch(it.id, it.name, it.address, it.pairing)
                })
                // La lista no cambia el estado del enlace: encontrar un
                // reloj no es estar conectado a el, y decirlo seria
                // exactamente la clase de mentira que hace que nadie
                // entienda por que "conectado" no recibe nada.
                return
            }
            is WifiLinkServer.Event.PairingFailed -> {
                // EL TIPO DE FALLO NO SE ADIVINA POR EL TEXTO, y NO TODO FALLO
                // TIRA LO CONSEGUIDO:
                //  · codigo rechazado -> se vuelve a pedir el codigo (el reloj
                //    sigue ensenando el mismo);
                //  · codigo caducado  -> ese emparejamiento ya no existe;
                //  · fallo del enlace -> el codigo tecleado se CONSERVA (el
                //    servidor lo reenvia solo cuando el reloj vuelva).
                when (ev.kind) {
                    PairFailure.CODE_REJECTED -> {
                        state.clearTypedCode()
                        onLinkEvent(LinkEvent.CodeRejected)
                    }
                    PairFailure.CODE_EXPIRED -> {
                        state.clearTypedCode()
                        onLinkEvent(LinkEvent.PairingEnded)
                    }
                    PairFailure.LINK -> onLinkEvent(LinkEvent.LinkFailure)
                }
                // Despues de la fase: setLink borra el fallo viejo al cambiar de estado.
                state.setPairFailure(ev.kind)
                state.setNotice(ev.why)
            }
        }
    }

    /**
     * Bucle de mantenimiento. NO es sondeo del enlace: el servidor
     * avisa por eventos. Esto solo refresca el estado del telefono de
     * vez en cuando (bateria y red cambian sin que nadie avise) y
     * vuelve a mandar las capacidades si un permiso cambio.
     */
    private fun loop() = scope.launch {
        var lastCaps: CapsPayload? = null
        var lastState: PhoneState? = null
        var lastPush = 0L
        while (isActive) {
            delay(5_000)
            try {
            if (server?.hasSession() != true) {
                // #########################################################
                // ##  SIN SESION SE SIGUE PREGUNTANDO
                // ##  --------------------------------------------------
                // ##  Y no es solo para pintar una lista. El reloj
                // ##  aprende la direccion de este telefono DEL PROPIO
                // ##  paquete de busqueda, asi que emitirlo es lo que
                // ##  permite conectar en las redes donde la difusion
                // ##  del reloj no llega hasta aqui -- que es el caso
                // ##  en el que antes no se encontraban nunca.
                // ##
                // ##  Cuesta unos 40 bytes cada cinco segundos, y solo
                // ##  mientras el usuario tiene el enlace encendido y
                // ##  todavia no hay sesion. Con la sesion abierta se
                // ##  deja de emitir del todo.
                // #########################################################
                server?.probeForWatches()
                continue
            }
            val caps = currentCaps()
            if (caps != lastCaps) { lastCaps = caps; server?.send(FlexLink.T_CAPS, caps.encode()) }
            val st = device.phoneState()
            val now = System.currentTimeMillis()
            // Se manda si CAMBIO, o cada minuto como latido. Mandar el
            // estado entero cada cinco segundos seria trafico inutil y
            // bateria por nada.
            if (st != lastState || now - lastPush > STATE_HEARTBEAT_MS) {
                lastState = st
                lastPush = now
                server?.send(FlexLink.T_PHONE_STATE, st.encode())
            }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                // Una vuelta que falla no mata el mantenimiento: se anota y se sigue.
                LinkDiag.w(TAG, "mantenimiento: ${e.javaClass.simpleName}")
            }
        }
    }

    // ---------------------------------------------------------
    //  La Wi-Fi: detectar un cambio de IP SIN guardar la IP "para siempre"
    // ---------------------------------------------------------
    /**
     * Avisos de red de Android. NO es sondeo: Android avisa cuando la Wi-Fi
     * aparece, desaparece o cambia de direccion, y solo entonces se mira.
     *
     * Lo que se hace con cada aviso:
     *  · cambio de IP: se anota, se renueva el cerrojo de multidifusion, se
     *    vuelve a anunciar al P4 la direccion del relay (la que el P4 tenia
     *    guardada ya no sirve) y se actualiza lo que ensena la pantalla. El
     *    servidor del enlace escucha en TODAS las interfaces: no hay que
     *    reabrirlo, y por eso no se entra en ningun bucle de reconexion.
     *  · Wi-Fi perdida: se anota. Las sesiones se caen solas (el socket muere)
     *    y el P4 vuelve cuando haya red; no se para el servicio.
     */
    private fun watchNetwork() {
        val cm = getSystemService(ConnectivityManager::class.java) ?: return
        val req = NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build()
        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) { onWifiMaybeChanged("disponible") }
            override fun onLinkPropertiesChanged(network: Network, linkProperties: LinkProperties) {
                onWifiMaybeChanged("propiedades")
            }
            override fun onLost(network: Network) { onWifiMaybeChanged("perdida") }
        }
        try {
            cm.registerNetworkCallback(req, cb)
            netCb = cb
        } catch (e: Exception) {
            LinkDiag.w(TAG, "sin avisos de red: ${e.javaClass.simpleName}")
        }
    }

    private fun unwatchNetwork() {
        val cb = netCb ?: return
        netCb = null
        runCatching { getSystemService(ConnectivityManager::class.java)?.unregisterNetworkCallback(cb) }
    }

    private fun onWifiMaybeChanged(why: String) {
        val now = NetAddress.wifiIpv4(this)
        val before = lastWifiIp
        if (now == before) return                       // el aviso no trajo ningun cambio de direccion
        lastWifiIp = now
        LinkDiag.d(TAG, "WIFI $why: ip ${before ?: "ninguna"} -> ${now ?: "ninguna"}")
        state.setAddress(now)
        server?.onNetworkChanged()
        BrowserRelayService.notifyAddressChanged()
        updateNotification()
    }

    private fun currentCaps(): CapsPayload = device.caps(
        notifAccess = FlexNotificationListener.hasAccess(this),
        relayRunning = BrowserRelayService.isRunning(),
        mediaActive = media?.hasActiveSession() == true,
    )

    private fun pushCaps() {
        server?.send(FlexLink.T_CAPS, currentCaps().encode())
    }

    private fun pushPhoneState() {
        server?.send(FlexLink.T_PHONE_STATE, device.phoneState().encode())
    }

    // ---------------------------------------------------------
    //  Mensajes de Flex OS
    // ---------------------------------------------------------
    private fun onMessage(type: Int, payload: ByteArray) {
        state.countReceived()
        when (type) {
            FlexLink.T_REPLY_REQ -> {
                val req = ReplyRequest.decode(payload)
                if (req == null) { state.countBadFrame(); return }
                // El resultado que se devuelve es el REAL: si Android
                // no acepto la accion, se manda el error, no un exito.
                val res = ReplyManager.reply(this, req.notifId, req.action, req.text)
                state.countReply(res.ok)
                server?.send(FlexLink.T_REPLY_RESULT, res.encode())
            }
            FlexLink.T_ACTION_REQ -> {
                val r = PayloadReader(payload)
                val id = r.u32(); val idx = r.u8()
                if (!r.ok) { state.countBadFrame(); return }
                val res = ReplyManager.action(this, id, idx)
                server?.send(FlexLink.T_ACTION_RESULT, res.encode())
            }
            FlexLink.T_NOTIF_REMOVE -> {
                // Flex OS descarto una notificacion: se descarta tambien
                // en el telefono, que es lo que el usuario espera.
                val r = PayloadReader(payload)
                val id = r.u32()
                if (r.ok) FlexNotificationListener.dismiss(id)
            }
            FlexLink.T_MEDIA_CMD -> {
                val r = PayloadReader(payload)
                val cmd = r.u8()
                if (r.ok) media?.command(cmd)
            }
            FlexLink.T_FIND_START -> FindMyPhone.start(this)
            FlexLink.T_FIND_STOP -> FindMyPhone.stop()
            FlexLink.T_PHONE_STATE -> pushPhoneState()   // Flex OS pidio el estado entero
            // `start`/`stop` pueden fallar (Android niega un servicio en primer plano
            // desde segundo plano): se anota y se sigue. NUNCA debe cerrar el enlace.
            FlexLink.T_RELAY_START -> {
                runCatching { BrowserRelayService.start(this) }
                    .onFailure { LinkDiag.w(TAG, "RELAY_START fallo: ${it.javaClass.simpleName}") }
                pushCaps()
            }
            FlexLink.T_RELAY_STOP -> {
                runCatching { BrowserRelayService.stop(this) }
                    .onFailure { LinkDiag.w(TAG, "RELAY_STOP fallo: ${it.javaClass.simpleName}") }
                pushCaps()
            }
            FlexLink.T_TIME_SYNC -> Unit   // el telefono ya tiene la hora del sistema
        }
    }

    // ---------------------------------------------------------
    //  Notificacion persistente
    // ---------------------------------------------------------
    private fun createChannel() {
        if (Build.VERSION.SDK_INT < 26) return
        val ch = NotificationChannel(
            CHANNEL, getString(R.string.channel_link), NotificationManager.IMPORTANCE_LOW,
        ).apply { description = getString(R.string.channel_link_desc); setShowBadge(false) }
        getSystemService(NotificationManager::class.java)?.createNotificationChannel(ch)
    }

    private fun buildNotification(): Notification {
        // El texto refleja el estado REAL del enlace.
        val text = when (state.link.value) {
            LinkState.READY -> getString(R.string.link_connected)
            LinkState.PAIRING -> getString(R.string.link_pairing)
            LinkState.CONNECTING -> getString(R.string.link_connecting)
            LinkState.RECONNECTING -> getString(R.string.link_reconnecting)
            LinkState.ADVERTISING -> getString(R.string.link_searching)
            LinkState.UNAVAILABLE -> state.error.value ?: getString(R.string.link_unavailable)
            LinkState.ERROR -> state.error.value ?: getString(R.string.link_error)
            LinkState.OFF -> getString(R.string.link_off)
        }
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val stop = PendingIntent.getService(
            this, 1, Intent(this, FlexLinkService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        return NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_link)
            .setContentTitle(getString(R.string.app_name))
            .setContentText(text)
            .setContentIntent(open)
            .setOngoing(true)
            .setSilent(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            // Un boton REAL para pararlo. Es lo minimo que se le debe
            // a alguien que ve una notificacion permanente.
            .addAction(R.drawable.ic_stop, getString(R.string.stop), stop)
            .build()
    }

    private fun startForegroundHonestly() {
        val n = buildNotification()
        if (Build.VERSION.SDK_INT >= 29) {
            // CONNECTED_DEVICE es el tipo que corresponde: el servicio
            // existe para mantener un enlace con otro dispositivo.
            startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        } else {
            startForeground(NOTIF_ID, n)
        }
    }

    private fun updateNotification() {
        runCatching {
            getSystemService(NotificationManager::class.java)?.notify(NOTIF_ID, buildNotification())
        }
    }

    private fun shutdown() {
        LinkDiag.d(TAG, "SERVICE_STOPPED (shutdown pedido)")
        // Se libera TODO: sin sockets abiertos, sin hilos vivos y sin
        // callbacks colgando.
        scope.coroutineContext.cancelChildren()
        unwatchNetwork()
        state.sender = null
        onLinkEvent(LinkEvent.Stopped)
        media?.stop(); media = null
        server?.stop(); server = null
        FindMyPhone.stop()
        state.clearTypedCode()
        state.setPairing(false, 0L)
        current = null
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    override fun onDestroy() {
        LinkDiag.d(TAG, "SERVICE_DESTROYED")
        scope.cancel()
        unwatchNetwork()
        state.sender = null
        media?.stop()
        server?.stop()
        FindMyPhone.stop()
        if (current === this) current = null
        // EL ESTADO TIENE QUE DEJAR DE MENTIR.
        //
        // shutdown() ya lo pone en OFF cuando para el usuario, pero
        // este camino es el otro: Android mata el servicio por bateria,
        // por memoria o por politica del fabricante. Sin esta linea el
        // estado se quedaba en "Esperando a Flex OS" con el socket ya
        // cerrado, asi que la pantalla decia que el telefono estaba
        // escuchando cuando no habia nadie escuchando -- justo el fallo
        // que hace imposible entender por que el reloj no encuentra
        // nada.
        if (state.link.value != LinkState.OFF) {
            synchronized(phaseLock) { machine.on(LinkEvent.Stopped) }
            state.setPhase(LinkPhase.STOPPED, getString(R.string.link_stopped_by_system))
            state.setPairing(false, 0L)
        }
        super.onDestroy()
    }
}
