package com.flexos.flexphone.relay

import android.app.*
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.ConnectivityManager
import android.net.LinkProperties
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiManager
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.util.Log
import androidx.core.app.NotificationCompat
import com.flexos.flexphone.MainActivity
import com.flexos.flexphone.R
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.domain.RelayClient
import com.flexos.flexphone.domain.RelayState
import com.flexos.flexphone.protocol.RelayInfo
import com.flexos.flexphone.storage.SettingsStore
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.first

/**
 * Servicio del Browser Relay.
 *
 * UN SERVICIO QUE SE QUEDA (y solo se va cuando se le pide)
 * ---------------------------------------------------------
 *   · Cerrar la app Flex Phone NO lo para: es un servicio en primer
 *     plano, independiente de la interfaz.
 *   · Tipo `connectedDevice`: el telefono sirve a un dispositivo externo
 *     (el P4) por la red local. Con `dataSync`, Android 15 lo cortaba a
 *     las 6 horas. Es el mismo tipo que ya usa Flex Storage.
 *   · Lo que la persona quiere se GUARDA: "encendido" al arrancarlo,
 *     "detenido por el usuario" al pararlo (boton de la app, de la
 *     notificacion o desde Flex OS). Si Android mata el proceso, vuelve
 *     solo (START_STICKY) -- pero SOLO si estaba encendido.
 *   · Sin cliente NO se apaga: sigue escuchando (sin cerrojos, casi sin
 *     coste). Lo que se suelta tras el tiempo de inactividad son las
 *     PESTANAS, que es lo que pesa (ver RelayEngine).
 *   · Sin Wi-Fi tampoco se apaga: espera a que vuelva y se anuncia de
 *     nuevo con la direccion real.
 *
 * LOS LOCKS SON EL PUNTO DELICADO
 * -------------------------------
 * Un WakeLock olvidado vacia la bateria en una noche, y es la queja
 * numero uno de las apps que hacen esto. Aqui:
 *   · el WakeLock es PARCIAL (solo CPU; la pantalla no se enciende);
 *   · se toma solo mientras hay un Flex OS conectado, CON TOPE, y el
 *     vigilante lo renueva mientras siga conectado (antes caducaba a las
 *     4 horas aunque la sesion siguiera viva);
 *   · se suelta en cuanto el cliente se va y en `onDestroy`;
 *   · el WifiLock, igual: solo con cliente.
 *
 * Y la notificacion dice la verdad: "activo" con Flex OS conectado,
 * "reconectando" si se acaba de ir, "esperando" si no hay nadie.
 */
class BrowserRelayService : Service() {

    companion object {
        private const val TAG = "FlexPhone/RelaySvc"
        private const val CHANNEL = "flexrelay"
        private const val NOTIF_ID = 1002
        const val ACTION_START = "com.flexos.flexphone.START_RELAY"
        const val ACTION_STOP = "com.flexos.flexphone.STOP_RELAY"
        /** Cerrojos con tope: aunque todo falle, caducan solos. El vigilante los renueva. */
        private const val LOCK_CAP_MS = 30 * 60_000L
        private const val PREFS = "flexrelay"
        private const val P_WANTED = "wanted"
        private const val P_USER_STOP = "user_stop"

        private fun prefs(ctx: Context) =
            ctx.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

        /** ¿La persona (o Flex OS) lo quiere encendido? Sobrevive a que Android mate el proceso. */
        fun wanted(ctx: Context): Boolean = prefs(ctx).getBoolean(P_WANTED, false)
        /** ¿Lo paro la persona a proposito? (y no Android, ni un error) */
        fun stoppedByUser(ctx: Context): Boolean = prefs(ctx).getBoolean(P_USER_STOP, false)
        private fun remember(ctx: Context, wanted: Boolean, userStop: Boolean) {
            prefs(ctx).edit().putBoolean(P_WANTED, wanted).putBoolean(P_USER_STOP, userStop).apply()
        }

        fun start(ctx: Context) {
            remember(ctx, wanted = true, userStop = false)
            val i = Intent(ctx, BrowserRelayService::class.java).setAction(ACTION_START)
            if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i) else ctx.startService(i)
        }
        /** Parada PEDIDA: se recuerda, y el estado dice "detenido por el usuario". */
        fun stop(ctx: Context) {
            remember(ctx, wanted = false, userStop = true)
            if (!alive && current == null) {
                // No hay servicio que parar (Android ya lo cerro, o nunca arranco):
                // basta con dejar dicho el estado. Arrancarlo solo para pararlo
                // podria fallar desde segundo plano. (Si esta A MEDIO arrancar,
                // `current` ya existe y la orden de parar SI se le manda.)
                FlexPhoneState.instance?.setRelay(RelayState.OFF, byUser = true)
                return
            }
            runCatching {
                ctx.startService(Intent(ctx, BrowserRelayService::class.java).setAction(ACTION_STOP))
            }
        }

        /**
         * Vuelve a levantarlo si la persona lo queria encendido y no esta
         * corriendo (Android lo cerro). Lo llama el enlace cuando arranca: es un
         * momento en el que Flex Phone esta en primer plano y SI puede arrancar
         * un servicio en primer plano.
         */
        fun restoreIfWanted(ctx: Context) {
            if (!wanted(ctx) || alive) return
            runCatching {
                val i = Intent(ctx, BrowserRelayService::class.java).setAction(ACTION_START)
                if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i) else ctx.startService(i)
            }.onFailure { Log.w(TAG, "no se pudo reanudar el relay: ${it.javaClass.simpleName}") }
        }

        /**
         * ¿Esta el servidor del navegador escuchando AHORA?
         *
         * Lo pregunta el enlace para declarar la capacidad RELAY. Es
         * el estado real del servicio, no una preferencia: si Android
         * lo mato por bateria, esto pasa a false y Flex OS deja de
         * ofrecer el navegador del telefono en vez de esperar
         * fotogramas que no van a llegar.
         */
        @Volatile private var alive: Boolean = false
        fun isRunning(): Boolean = alive

        @Volatile private var current: BrowserRelayService? = null

        /**
         * La Wi-Fi del telefono cambio de direccion (otra IP, se fue y volvio): se
         * vuelve a anunciar al P4 la direccion REAL. El servidor escucha en todas
         * las interfaces, asi que no hay que reabrirlo: lo que quedaba viejo era
         * la IP que el P4 tenia guardada para llegar al relay.
         */
        fun notifyAddressChanged() { current?.refreshAddress() }
    }

    private var server: RelayServer? = null
    private var wakeLock: PowerManager.WakeLock? = null
    private var wifiLock: WifiManager.WifiLock? = null
    private var netCb: ConnectivityManager.NetworkCallback? = null
    // Un fallo dentro de una corrutina de este servicio NO puede tumbar la app
    // (mismo criterio que FlexStorageService).
    private val scope = CoroutineScope(
        SupervisorJob() + Dispatchers.Default +
            CoroutineExceptionHandler { _, e -> Log.w(TAG, "corrutina del relay fallo: ${e.javaClass.simpleName}") },
    )
    private lateinit var state: FlexPhoneState

    /** Hay un arranque en marcha (o ya arriba): otro RELAY_START no lo repite. */
    private val starting = java.util.concurrent.atomic.AtomicBoolean(false)

    @Volatile private var clientConnected = false
    @Volatile private var lastActivityMs = System.currentTimeMillis()
    /** Minutos que se conservan las pestanas sin cliente (ajuste del relay). */
    @Volatile private var keepTabsMin = 10
    @Volatile private var statusLine = ""

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        state = FlexPhoneState.instance ?: FlexPhoneState().also { FlexPhoneState.instance = it }
        createChannel()
        current = this
    }

    /** Vuelve a anunciar la direccion Wi-Fi actual (ver [notifyAddressChanged]). */
    private fun refreshAddress() {
        val srv = server ?: return
        if (!alive) return
        val addr = com.flexos.flexphone.link.NetAddress.wifi(this)?.address?.address
        if (addr == null) {
            statusLine = getString(R.string.relay_no_wifi)
            updateNotification()
            state.setRelay(
                RelayState.ERROR,
                RelayInfo(ip = byteArrayOf(0, 0, 0, 0), port = 0, protoVer = 1, tls = false, caps = 0,
                    error = "el telefono no esta en una red Wi-Fi"),
            )
            return
        }
        onServerEvent(RelayServer.Event.Listening(addr, srv.port))
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            remember(this, wanted = false, userStop = true)
            shutdown(byUser = true)
            return START_NOT_STICKY
        }
        // Reinicio del sistema SIN intent (START_STICKY, Android mato el
        // proceso): solo si la persona lo queria encendido.
        if (intent == null && !wanted(this)) { stopSelf(); return START_NOT_STICKY }

        // startForeground se llama en CADA arranque (Android lo exige tras cada
        // startForegroundService), pero el arranque en si es IDEMPOTENTE.
        //
        // Antes cada RELAY_START repetido -- y el P4 los repetia hasta cinco
        // veces -- ponia el estado en STARTING otra vez (y se lo contaba a Flex OS
        // con port 0, borrando la direccion ya anunciada) y, mientras `server`
        // seguia a null, lanzaba OTRO bringUp: otro RelayServer y otro puerto.
        if (!startForegroundHonestly()) {
            // Android no dejo pasar a primer plano (p. ej. un reinicio desde
            // segundo plano en Android 12+). Se dice; el enlace lo reintentara
            // la proxima vez que arranque (restoreIfWanted).
            if (!alive) {
                state.setRelay(
                    RelayState.ERROR,
                    RelayInfo(ip = byteArrayOf(0, 0, 0, 0), port = 0, protoVer = 1, tls = false, caps = 0,
                        error = "Android no dejo reanudar el relay: abre Flex Phone"),
                )
                stopSelf()
            }
            return START_NOT_STICKY
        }
        if (!starting.compareAndSet(false, true)) {
            state.reannounceRelay()
            return START_STICKY
        }
        statusLine = getString(R.string.relay_starting)
        updateNotification()
        state.setRelay(RelayState.STARTING)
        state.setRelayClient(RelayClient.NONE)
        scope.launch { bringUp() }
        return START_STICKY
    }

    private suspend fun bringUp() {
        val settings = SettingsStore(this).flow.first()
        keepTabsMin = settings.relayIdleTimeoutMin.coerceIn(1, 120)
        RelayEngine.init(this, settings.relayMaxTabs, settings.relayQuality, keepTabsMin)

        // El token de sesion es el que Flex OS presentara en el HELLO.
        // TOKEN DEL RELAY.
        //
        // Antes se derivaba de la direccion BLE del dispositivo
        // emparejado, y una direccion MAC es PUBLICA: cualquiera que
        // la viera podia calcular el token. Ahora sale de la clave del
        // vinculo, que es un secreto compartido de verdad y que solo
        // tienen los dos extremos que emparejaron.
        //
        // Flex OS calcula exactamente lo mismo desde su lado (ver
        // flexPhoneRelayToken en FlexOS_FlexPhone_Bridge.h), asi que
        // el token nunca viaja por la red.
        val bondKey = com.flexos.flexphone.storage.BondStore(this).key()
        if (bondKey == null) {
            // Sin vinculo no hay token, y sin token el relay no podria
            // dejar entrar a nadie. Se dice y se para, en vez de abrir
            // un puerto al que no puede conectarse nadie.
            shutdown(byUser = false)
            state.setRelay(RelayState.ERROR)
            return
        }
        val token = com.flexos.flexphone.protocol.FlexAuth
            .hmacSha256(bondKey, "flexphone-relay-v2".toByteArray(Charsets.US_ASCII))
            .joinToString("") { "%02x".format(it) }
            .take(32)

        val srv = RelayServer(this, token) { ev -> onServerEvent(ev) }
        server = srv
        alive = true
        if (!srv.start(settings.relayPort)) {
            shutdown(byUser = false)
            state.setRelay(RelayState.ERROR)
            return
        }
        watchNetwork()
        watchdog()
    }

    private fun onServerEvent(ev: RelayServer.Event) {
        when (ev) {
            is RelayServer.Event.Listening -> {
                // Se anuncia al P4 la IP y el puerto REALES en los que
                // se esta escuchando -- no un valor supuesto. Con Flex OS
                // conectado la linea sigue diciendo "activo": un cambio de
                // red no convierte una sesion viva en una espera.
                statusLine = when {
                    clientConnected -> getString(R.string.relay_active)
                    state.relayClient.value == RelayClient.RECONNECTING ->
                        getString(R.string.relay_reconnecting, keepTabsMin)
                    else -> getString(
                        R.string.relay_waiting,
                        ev.address.joinToString(".") { (it.toInt() and 0xFF).toString() },
                        ev.port,
                    )
                }
                updateNotification()
                state.setRelay(
                    RelayState.UP,
                    RelayInfo(
                        ip = ev.address, port = ev.port, protoVer = 1,
                        // TLS: false, y se dice. En la red local se
                        // sirve en claro, como el modo de desarrollo
                        // del servidor Ubuntu.
                        tls = false, caps = RelayEngine.capabilities().toInt(),
                    ),
                )
            }
            is RelayServer.Event.Error -> {
                statusLine = ev.message
                updateNotification()
                state.setRelay(RelayState.ERROR)
            }
            RelayServer.Event.NoWifi -> {
                // El servidor SIGUE escuchando: cuando vuelva la Wi-Fi se
                // anuncia la direccion nueva (watchNetwork -> refreshAddress).
                statusLine = getString(R.string.relay_no_wifi)
                updateNotification()
                state.setRelay(
                    RelayState.ERROR,
                    RelayInfo(ip = byteArrayOf(0, 0, 0, 0), port = 0, protoVer = 1, tls = false, caps = 0,
                        error = "el telefono no esta en una red Wi-Fi"),
                )
            }
            RelayServer.Event.ClientConnected -> {
                clientConnected = true
                lastActivityMs = System.currentTimeMillis()
                acquireLocks()
                statusLine = getString(R.string.relay_active)
                updateNotification()
                state.setRelayClient(RelayClient.CONNECTED)
            }
            RelayServer.Event.ClientGone -> {
                clientConnected = false
                lastActivityMs = System.currentTimeMillis()
                releaseLocks()
                statusLine = getString(R.string.relay_reconnecting, keepTabsMin)
                updateNotification()
                state.setRelayClient(RelayClient.RECONNECTING)
            }
        }
    }

    /**
     * Vigilante: renueva los cerrojos mientras haya cliente, pasa de
     * "reconectando" a "esperando" cuando ya no hay pestanas que conservar, y
     * detecta que Android nos ha restringido (y que ya no).
     */
    private fun watchdog() = scope.launch {
        var restricted = false
        while (isActive) {
            delay(15_000)
            if (clientConnected) acquireLocks()            // renueva el tope
            else if (System.currentTimeMillis() - lastActivityMs > keepTabsMin * 60_000L &&
                     state.relayClient.value == RelayClient.RECONNECTING) {
                // Paso el periodo de gracia: las pestanas ya se soltaron. El
                // relay sigue escuchando, pero ya no es una reconexion.
                state.setRelayClient(RelayClient.NONE)
                state.reannounceRelay()
                refreshAddress()
            }
            // Optimizacion de bateria: si el sistema nos ha metido en
            // modo restringido, el relay puede morir en cualquier
            // momento. Se avisa AL P4 en vez de dejarlo esperando.
            val now = isBatteryRestricted()
            if (now && !restricted) {
                state.setRelay(RelayState.SUSPENDED)
                RelayEngine.markSuspended(getString(R.string.relay_suspended))
                statusLine = getString(R.string.relay_suspended)
                updateNotification()
            } else if (!now && restricted) {
                // Ya no: el relay vuelve a su estado real (y lo dice).
                RelayEngine.clearSuspended()
                refreshAddress()
                statusLine = getString(if (clientConnected) R.string.relay_active else R.string.relay_starting)
                updateNotification()
            }
            restricted = now
        }
    }

    // ---------------------------------------------------------
    //  Red: anunciarse de nuevo cuando vuelve la Wi-Fi o cambia la IP
    // ---------------------------------------------------------
    // Propio del relay: antes dependia de que el ENLACE estuviera corriendo
    // para enterarse de un cambio de red, y sin el se quedaba anunciando una
    // direccion vieja (o "sin Wi-Fi") para siempre.
    private fun watchNetwork() {
        if (netCb != null) return
        val cm = getSystemService(ConnectivityManager::class.java) ?: return
        val req = NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build()
        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) { refreshAddress() }
            override fun onLinkPropertiesChanged(network: Network, linkProperties: LinkProperties) { refreshAddress() }
            override fun onLost(network: Network) { refreshAddress() }
        }
        try {
            cm.registerNetworkCallback(req, cb)
            netCb = cb
        } catch (e: Exception) {
            Log.w(TAG, "sin avisos de red: ${e.javaClass.simpleName}")
        }
    }

    private fun unwatchNetwork() {
        val cb = netCb ?: return
        netCb = null
        runCatching { getSystemService(ConnectivityManager::class.java)?.unregisterNetworkCallback(cb) }
    }

    private fun isBatteryRestricted(): Boolean {
        val pm = getSystemService(PowerManager::class.java) ?: return false
        // Si la app NO esta excluida del ahorro y ademas el sistema
        // esta en modo de ahorro, el relay tiene los dias contados.
        val ignoring = runCatching { pm.isIgnoringBatteryOptimizations(packageName) }.getOrDefault(true)
        return !ignoring && pm.isPowerSaveMode
    }

    // ---------------------------------------------------------
    //  Locks
    // ---------------------------------------------------------
    private fun acquireLocks() {
        // PARCIAL: mantiene la CPU, NO enciende la pantalla. Es lo que
        // permite que el WebView siga trabajando con la pantalla
        // apagada, dentro de lo que Android permita.
        //
        // Se llama al conectar Flex OS y en cada vuelta del vigilante
        // mientras siga conectado: acquire() sobre el MISMO lock (no
        // contado) solo renueva el tope, no apila otro.
        if (wakeLock == null) {
            val pm = getSystemService(PowerManager::class.java)
            wakeLock = pm?.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "FlexPhone::relay")?.apply {
                setReferenceCounted(false)
            }
        }
        // Con tope: aunque todo lo demas falle, el lock caduca.
        runCatching { wakeLock?.acquire(LOCK_CAP_MS) }
        if (wifiLock?.isHeld != true) {
            val wm = applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
            wifiLock = wm?.createWifiLock(
                if (Build.VERSION.SDK_INT >= 29) WifiManager.WIFI_MODE_FULL_LOW_LATENCY
                else @Suppress("DEPRECATION") WifiManager.WIFI_MODE_FULL_HIGH_PERF,
                "FlexPhone::relay",
            )?.apply { setReferenceCounted(false); runCatching { acquire() } }
            Log.i(TAG, "locks tomados")
        }
    }

    private fun releaseLocks() {
        // Se sueltan SIEMPRE, pase lo que pase.
        runCatching { if (wakeLock?.isHeld == true) wakeLock?.release() }
        runCatching { if (wifiLock?.isHeld == true) wifiLock?.release() }
        wakeLock = null; wifiLock = null
        Log.i(TAG, "locks liberados")
    }

    // ---------------------------------------------------------
    //  Notificacion
    // ---------------------------------------------------------
    private fun createChannel() {
        if (Build.VERSION.SDK_INT < 26) return
        val ch = NotificationChannel(
            CHANNEL, getString(R.string.channel_relay), NotificationManager.IMPORTANCE_LOW,
        ).apply { description = getString(R.string.channel_relay_desc); setShowBadge(false) }
        getSystemService(NotificationManager::class.java)?.createNotificationChannel(ch)
    }

    private fun buildNotification(): Notification {
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val stop = PendingIntent.getService(
            this, 2, Intent(this, BrowserRelayService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        return NotificationCompat.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_relay)
            .setContentTitle(getString(R.string.relay_title))
            .setContentText(statusLine)
            .setStyle(NotificationCompat.BigTextStyle().bigText(
                statusLine + "\n" + getString(R.string.relay_battery_note)
            ))
            .setContentIntent(open)
            .setOngoing(true)
            .setSilent(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .addAction(R.drawable.ic_stop, getString(R.string.stop), stop)
            .build()
    }

    private fun startForegroundHonestly(): Boolean = try {
        val n = buildNotification()
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        } else startForeground(NOTIF_ID, n)
        true
    } catch (e: Exception) {
        // Android puede negarlo (sin permiso, o arrancado desde segundo plano):
        // se dice en vez de quedarse a medias.
        Log.w(TAG, "no se pudo pasar a primer plano: ${e.javaClass.simpleName}")
        false
    }

    private fun updateNotification() {
        runCatching { getSystemService(NotificationManager::class.java)?.notify(NOTIF_ID, buildNotification()) }
    }

    private fun shutdown(byUser: Boolean) {
        alive = false
        starting.set(false)
        clientConnected = false
        scope.coroutineContext.cancelChildren()
        unwatchNetwork()
        RelayEngine.shutdown()
        server?.stop(); server = null
        releaseLocks()
        state.setRelayClient(RelayClient.NONE)
        state.setRelay(RelayState.OFF, byUser = byUser)
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    override fun onDestroy() {
        if (current === this) current = null
        // Aunque nos maten: los locks se sueltan aqui tambien, y la
        // bandera baja para que el enlace deje de declarar la
        // capacidad RELAY como concedida.
        alive = false
        starting.set(false)
        scope.cancel()
        unwatchNetwork()
        RelayEngine.shutdown()
        server?.stop()
        releaseLocks()
        state.setRelayClient(RelayClient.NONE)
        state.setRelay(RelayState.OFF, byUser = stoppedByUser(this))
        super.onDestroy()
    }
}
