package com.flexos.flexphone.relay

import android.app.*
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.wifi.WifiManager
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.util.Log
import androidx.core.app.NotificationCompat
import com.flexos.flexphone.MainActivity
import com.flexos.flexphone.R
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.domain.RelayState
import com.flexos.flexphone.protocol.RelayInfo
import com.flexos.flexphone.storage.SettingsStore
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.first

/**
 * Servicio del Browser Relay.
 *
 * LOS LOCKS SON EL PUNTO DELICADO
 * -------------------------------
 * Un WakeLock olvidado vacia la bateria en una noche, y es la queja
 * numero uno de las apps que hacen esto. Aqui:
 *   · el WakeLock es PARCIAL (solo CPU; la pantalla no se enciende);
 *   · se pide al ARRANCAR la sesion y se suelta en `onDestroy` y en
 *     cuanto el cliente se va;
 *   · el WifiLock solo mientras hay sesion;
 *   · hay un TIEMPO MUERTO configurable: si nadie usa el relay
 *     durante ese rato, se cierra solo con sus locks.
 *
 * Y la notificacion dice la verdad: mientras nadie ha conectado pone
 * "esperando", no "activo".
 */
class BrowserRelayService : Service() {

    companion object {
        private const val TAG = "FlexPhone/RelaySvc"
        private const val CHANNEL = "flexrelay"
        private const val NOTIF_ID = 1002
        const val ACTION_START = "com.flexos.flexphone.START_RELAY"
        const val ACTION_STOP = "com.flexos.flexphone.STOP_RELAY"

        fun start(ctx: Context) {
            val i = Intent(ctx, BrowserRelayService::class.java).setAction(ACTION_START)
            if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i) else ctx.startService(i)
        }
        fun stop(ctx: Context) {
            ctx.startService(Intent(ctx, BrowserRelayService::class.java).setAction(ACTION_STOP))
        }
    }

    private var server: RelayServer? = null
    private var wakeLock: PowerManager.WakeLock? = null
    private var wifiLock: WifiManager.WifiLock? = null
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private lateinit var state: FlexPhoneState

    @Volatile private var clientConnected = false
    @Volatile private var lastActivityMs = System.currentTimeMillis()
    @Volatile private var idleTimeoutMs = 10 * 60_000L
    @Volatile private var statusLine = ""

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        state = FlexPhoneState.instance ?: FlexPhoneState().also { FlexPhoneState.instance = it }
        createChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) { shutdown(); return START_NOT_STICKY }

        statusLine = getString(R.string.relay_starting)
        startForegroundHonestly()
        state.setRelay(RelayState.STARTING)

        if (server == null) scope.launch { bringUp() }
        return START_NOT_STICKY
    }

    private suspend fun bringUp() {
        val settings = SettingsStore(this).flow.first()
        idleTimeoutMs = settings.relayIdleTimeoutMin.coerceIn(1, 120) * 60_000L
        RelayEngine.init(this, settings.relayMaxTabs, settings.relayQuality)

        // El token de sesion es el que Flex OS presentara en el HELLO.
        // Se deriva del vinculo BLE: sin haber emparejado no se tiene.
        val token = settings.bondedDeviceAddress?.let { addr ->
            java.security.MessageDigest.getInstance("SHA-256")
                .digest(addr.toByteArray())
                .joinToString("") { "%02x".format(it) }
                .take(32)
        }.orEmpty()

        val srv = RelayServer(this, token) { ev -> onServerEvent(ev) }
        server = srv
        if (!srv.start(settings.relayPort)) {
            state.setRelay(RelayState.ERROR)
            shutdown()
            return
        }
        acquireLocks()
        watchdog()
    }

    private fun onServerEvent(ev: RelayServer.Event) {
        when (ev) {
            is RelayServer.Event.Listening -> {
                // Se anuncia al P4 la IP y el puerto REALES en los que
                // se esta escuchando -- no un valor supuesto.
                statusLine = getString(
                    R.string.relay_waiting,
                    ev.address.joinToString(".") { (it.toInt() and 0xFF).toString() },
                    ev.port,
                )
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
            RelayServer.Event.ClientConnected -> {
                clientConnected = true
                lastActivityMs = System.currentTimeMillis()
                statusLine = getString(R.string.relay_active)
                updateNotification()
            }
            RelayServer.Event.ClientGone -> {
                clientConnected = false
                lastActivityMs = System.currentTimeMillis()
                statusLine = getString(R.string.relay_client_gone)
                updateNotification()
            }
        }
    }

    /**
     * Vigilante: cierra la sesion inactiva y detecta que Android nos
     * ha restringido.
     */
    private fun watchdog() = scope.launch {
        while (isActive) {
            delay(15_000)
            val idle = System.currentTimeMillis() - lastActivityMs
            if (!clientConnected && idle > idleTimeoutMs) {
                Log.i(TAG, "relay inactivo: se cierra solo")
                shutdown()
                return@launch
            }
            // Optimizacion de bateria: si el sistema nos ha metido en
            // modo restringido, el relay puede morir en cualquier
            // momento. Se avisa AL P4 en vez de dejarlo esperando.
            if (isBatteryRestricted()) {
                state.setRelay(RelayState.SUSPENDED)
                RelayEngine.markSuspended(getString(R.string.relay_suspended))
                statusLine = getString(R.string.relay_suspended)
                updateNotification()
            }
        }
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
        val pm = getSystemService(PowerManager::class.java)
        // PARCIAL: mantiene la CPU, NO enciende la pantalla. Es lo que
        // permite que el WebView siga trabajando con la pantalla
        // apagada, dentro de lo que Android permita.
        wakeLock = pm?.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "FlexPhone::relay")?.apply {
            setReferenceCounted(false)
            // Con tope: aunque todo lo demas falle, el lock caduca.
            acquire(4 * 60 * 60 * 1000L)
        }
        val wm = applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
        wifiLock = wm?.createWifiLock(
            if (Build.VERSION.SDK_INT >= 29) WifiManager.WIFI_MODE_FULL_LOW_LATENCY
            else @Suppress("DEPRECATION") WifiManager.WIFI_MODE_FULL_HIGH_PERF,
            "FlexPhone::relay",
        )?.apply { setReferenceCounted(false); acquire() }
        Log.i(TAG, "locks tomados")
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
        getSystemService(NotificationManager::class.java).createNotificationChannel(ch)
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

    private fun startForegroundHonestly() {
        val n = buildNotification()
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC)
        } else startForeground(NOTIF_ID, n)
    }

    private fun updateNotification() {
        runCatching { getSystemService(NotificationManager::class.java).notify(NOTIF_ID, buildNotification()) }
    }

    private fun shutdown() {
        scope.coroutineContext.cancelChildren()
        RelayEngine.detach()
        server?.stop(); server = null
        releaseLocks()
        state.setRelay(RelayState.OFF)
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    override fun onDestroy() {
        // Aunque nos maten: los locks se sueltan aqui tambien.
        scope.cancel()
        RelayEngine.detach()
        server?.stop()
        releaseLocks()
        state.setRelay(RelayState.OFF)
        super.onDestroy()
    }
}
