package com.flexos.flexphone.link

import android.app.*
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
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
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private lateinit var state: FlexPhoneState

    /** Codigo que el usuario acaba de teclear, si lo hay. */
    @Volatile private var typedCode: String? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        state = FlexPhoneState.instance ?: FlexPhoneState().also { FlexPhoneState.instance = it }
        bonds = BondStore(this)
        device = DeviceAdapter(this)
        createChannel()
        current = this
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
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
                pairingCode = { typedCode },
                onPaired = { key, flexosId ->
                    // El vinculo se guarda AQUI, cuando Flex OS ya ha
                    // demostrado que es quien dice ser. Guardarlo antes
                    // dejaria un vinculo con cualquiera que escuchara.
                    bonds.save(key, flexosId, null)
                    typedCode = null
                },
                onEvent = { ev -> onServerEvent(ev) },
                onMessage = { type, payload -> onMessage(type, payload) },
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
            if (!s.start()) {
                updateNotification()
                server = null
                stopSelf()
                return START_NOT_STICKY
            }
            state.setLink(LinkState.ADVERTISING)
            state.sender = { type, payload -> s.send(type, payload) }
            media = MediaBridge(this, state).also { it.start() }
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
     * El usuario tecleo el codigo que ensena Flex OS. Devuelve false
     * si el codigo no tiene forma de codigo: asi la pantalla avisa en
     * el acto en vez de esperar un fallo de autenticacion.
     */
    fun submitPairingCode(code: String): Boolean {
        if (!FlexAuth.isValidCode(code)) return false
        typedCode = code
        // EL RESULTADO REAL sube hasta la pantalla. Antes se descartaba
        // y se devolvia true siempre, asi que cuando el envio no salia
        // la pantalla se quedaba en "Comprobando..." para siempre. Un
        // false aqui significa "todavia no": Flex OS aun no ha mandado
        // su sal, y el codigo queda guardado para usarlo en cuanto
        // llegue.
        return server?.submitPairingCode(code) ?: false
    }

    /** ¿Ha llegado la sal de Flex OS? La pantalla lo usa para saber si ya se puede teclear. */
    fun isAwaitingCode(): Boolean = server?.isAwaitingCode() ?: false

    /** Revoca el vinculo: clave fuera y sesion cerrada. */
    fun forgetBond() {
        bonds.clear()
        typedCode = null
        server?.stop()
        server?.start()
        state.setLink(LinkState.ADVERTISING)
        updateNotification()
    }

    fun linkAddress(): String? = server?.localWifiAddress()
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
    //  Eventos del servidor
    // ---------------------------------------------------------
    private fun onServerEvent(ev: WifiLinkServer.Event) {
        when (ev) {
            is WifiLinkServer.Event.Listening -> state.setLink(LinkState.ADVERTISING)
            is WifiLinkServer.Event.Error -> state.setLink(LinkState.ERROR, ev.message)
            is WifiLinkServer.Event.SessionOpen -> {
                state.setLink(LinkState.READY)
                // Al abrir sesion se manda lo que Flex OS necesita para
                // pintar la pantalla: capacidades primero, para que no
                // llegue a ofrecer nada que este telefono no pueda.
                pushCaps()
                pushPhoneState()
            }
            is WifiLinkServer.Event.SessionClosed -> {
                state.setLink(LinkState.ADVERTISING)
                state.countReconnect()
            }
            is WifiLinkServer.Event.PairingRequested -> state.setLink(LinkState.PAIRING)
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
                typedCode = null
                state.setLink(LinkState.ERROR, ev.why)
            }
        }
        updateNotification()
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
        }
    }

    private fun currentCaps(): CapsPayload = device.caps(
        notifAccess = FlexNotificationListener.hasAccess(this),
        relayRunning = com.flexos.flexphone.relay.BrowserRelayService.isRunning(),
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
            FlexLink.T_RELAY_START -> {
                com.flexos.flexphone.relay.BrowserRelayService.start(this)
                pushCaps()
            }
            FlexLink.T_RELAY_STOP -> {
                com.flexos.flexphone.relay.BrowserRelayService.stop(this)
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
        (getSystemService(NotificationManager::class.java)).createNotificationChannel(ch)
    }

    private fun buildNotification(): Notification {
        // El texto refleja el estado REAL del enlace.
        val text = when (state.link.value) {
            LinkState.READY -> getString(R.string.link_connected)
            LinkState.PAIRING -> getString(R.string.link_pairing)
            LinkState.CONNECTING -> getString(R.string.link_connecting)
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
            (getSystemService(NotificationManager::class.java)).notify(NOTIF_ID, buildNotification())
        }
    }

    private fun shutdown() {
        // Se libera TODO: sin sockets abiertos, sin hilos vivos y sin
        // callbacks colgando.
        scope.coroutineContext.cancelChildren()
        state.sender = null
        state.setLink(LinkState.OFF)
        media?.stop(); media = null
        server?.stop(); server = null
        FindMyPhone.stop()
        typedCode = null
        current = null
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    override fun onDestroy() {
        scope.cancel()
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
            state.setLink(LinkState.OFF, getString(R.string.link_stopped_by_system))
        }
        super.onDestroy()
    }
}
