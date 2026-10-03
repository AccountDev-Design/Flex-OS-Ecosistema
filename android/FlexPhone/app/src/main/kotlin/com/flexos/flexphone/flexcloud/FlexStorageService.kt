package com.flexos.flexphone.flexcloud

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
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
import com.flexos.flexphone.MainActivity
import com.flexos.flexphone.R
import kotlinx.coroutines.CoroutineExceptionHandler
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * SERVICIO DE FLEX CLOUD EN EL TELEFONO (Flex Storage).
 *
 * Mantiene escuchando el servidor de Flex Cloud ([FlexCloudPhone]) para que el
 * Flex OS emparejado pueda guardar, leer y reproducir lo que la persona le
 * pide. Tipo `connectedDevice`: es exactamente eso, el telefono sirviendo a un
 * dispositivo externo por la red local (y, a diferencia de `dataSync`, Android
 * 15 no lo corta a las 6 horas).
 *
 * BATERIA (lo delicado):
 *  · NO hay WakeLock permanente. Los cerrojos (CPU parcial y Wi-Fi) se toman
 *    solo mientras Flex OS esta hablando con el telefono (ultima peticion hace
 *    menos de 2 minutos), con tope, y se sueltan en cuanto calla.
 *  · Sin Wi-Fi no escucha nada; cuando vuelve la Wi-Fi (o cambia la IP) se
 *    vuelve a escuchar solo, sin bucles: lo dispara el aviso de red de Android.
 *  · La notificacion dice la verdad: "esperando a Flex OS" hasta que llega la
 *    primera peticion, y el motivo si no puede escuchar.
 */
class FlexStorageService : Service() {

    companion object {
        private const val TAG = "FlexPhone/FlexCloudSvc"
        private const val CHANNEL = "flexcloud"
        private const val NOTIF_ID = 1003
        private const val ACTIVE_MS = 120_000L
        private const val LOCK_CAP_MS = 10 * 60_000L
        const val ACTION_START = "com.flexos.flexphone.START_FLEXCLOUD"
        const val ACTION_STOP = "com.flexos.flexphone.STOP_FLEXCLOUD"

        fun start(ctx: Context) {
            val i = Intent(ctx, FlexStorageService::class.java).setAction(ACTION_START)
            if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i) else ctx.startService(i)
        }

        fun stop(ctx: Context) {
            ctx.startService(Intent(ctx, FlexStorageService::class.java).setAction(ACTION_STOP))
        }

        @Volatile private var alive = false
        fun isRunning(): Boolean = alive
    }

    // Un fallo dentro de una corrutina de este servicio NO puede tumbar la app: sin un
    // CoroutineExceptionHandler la excepcion llega al manejador del hilo y Android MATA el
    // proceso (y con el, el enlace de Flex Phone y un emparejamiento en curso).
    private val scope = CoroutineScope(
        SupervisorJob() + Dispatchers.IO +
            CoroutineExceptionHandler { _, e -> Log.w(TAG, "corrutina del servicio fallo: ${e.javaClass.simpleName}") },
    )
    private var wakeLock: PowerManager.WakeLock? = null
    private var wifiLock: WifiManager.WifiLock? = null
    private var netCb: ConnectivityManager.NetworkCallback? = null
    @Volatile private var line = "Arrancando..."
    @Volatile private var started = false

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        createChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) {
            FlexCloudPhone.setEnabled(this, false)
            shutdown()
            return START_NOT_STICKY
        }
        // Reinicio del sistema sin intent (START_STICKY): solo si sigue activado.
        if (intent == null && !FlexCloudPhone.isEnabled(this)) { stopSelf(); return START_NOT_STICKY }
        if (!startForegroundHonestly()) { stopSelf(); return START_NOT_STICKY }
        alive = true
        if (!started) {
            started = true
            scope.launch { bringUp() }
        }
        return START_STICKY
    }

    private suspend fun bringUp() {
        val err = FlexCloudPhone.startServer(this)
        line = err ?: waitingLine()
        updateNotification()
        watchNetwork()
        // Vigilante: estado, cerrojos y notificacion. Barato (lecturas locales).
        while (scope.isActive) {
            delay(5_000)
            FlexCloudPhone.refresh(this)
            val s = FlexCloudPhone.status.value
            val active = s.running && s.lastContactMs > 0 && System.currentTimeMillis() - s.lastContactMs < ACTIVE_MS
            if (active) acquireLocks() else releaseLocks()
            val att = StorageAttach.state.value
            val want = when {
                att is StorageAttach.UiState.Code -> "Emparejando: compara el código con el de Flex OS"
                !s.running -> s.error ?: "Sin servidor"
                active -> "Flex OS conectado" + (s.p4Name?.let { " · $it" } ?: "")
                else -> waitingLine()
            }
            if (want != line) { line = want; updateNotification() }
        }
    }

    private fun waitingLine(): String {
        val s = FlexCloudPhone.status.value
        return when {
            !s.paired -> "Esperando a que Flex OS lo apruebe"
            s.error != null -> s.error
            else -> "Esperando a Flex OS" + (s.address?.let { " · $it" } ?: "")
        }
    }

    // ---------------------------------------------------------
    //  Red: volver a escuchar cuando vuelve la Wi-Fi o cambia la IP
    // ---------------------------------------------------------
    private fun watchNetwork() {
        val cm = getSystemService(ConnectivityManager::class.java) ?: return
        val req = NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build()
        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) { kick() }
            override fun onLinkPropertiesChanged(network: Network, linkProperties: LinkProperties) { kick() }
            override fun onLost(network: Network) {
                if (FlexCloudPhone.wifiAddress(this@FlexStorageService) == null) {
                    FlexCloudPhone.stopServer(this@FlexStorageService)
                    line = "Sin Wi-Fi: conecta el teléfono a la misma red que Flex OS"
                    updateNotification()
                }
            }
        }
        try {
            cm.registerNetworkCallback(req, cb)
            netCb = cb
        } catch (e: Exception) {
            Log.w(TAG, "sin avisos de red: ${e.javaClass.simpleName}")
        }
    }

    private fun kick() {
        scope.launch {
            val err = FlexCloudPhone.rebind(this@FlexStorageService)
            line = err ?: waitingLine()
            updateNotification()
        }
    }

    // ---------------------------------------------------------
    //  Cerrojos: solo con Flex OS hablando, con tope
    // ---------------------------------------------------------
    private fun acquireLocks() {
        if (wakeLock?.isHeld != true) {
            val pm = getSystemService(PowerManager::class.java)
            wakeLock = pm?.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "FlexPhone::flexcloud")?.apply {
                setReferenceCounted(false)
                acquire(LOCK_CAP_MS)
            }
        }
        if (wifiLock?.isHeld != true) {
            val wm = applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
            @Suppress("DEPRECATION")
            wifiLock = wm?.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "FlexPhone::flexcloud")?.apply {
                setReferenceCounted(false)
                acquire()
            }
        }
    }

    private fun releaseLocks() {
        try { if (wakeLock?.isHeld == true) wakeLock?.release() } catch (e: Exception) { }
        try { if (wifiLock?.isHeld == true) wifiLock?.release() } catch (e: Exception) { }
        wakeLock = null
        wifiLock = null
    }

    // ---------------------------------------------------------
    //  Notificacion
    // ---------------------------------------------------------
    private fun createChannel() {
        if (Build.VERSION.SDK_INT < 26) return
        val ch = NotificationChannel(CHANNEL, "Flex Cloud", NotificationManager.IMPORTANCE_LOW).apply {
            description = "Aviso permanente mientras el teléfono guarda archivos para Flex OS."
            setShowBadge(false)
        }
        getSystemService(NotificationManager::class.java)?.createNotificationChannel(ch)
    }

    private fun buildNotification(): Notification {
        val open = PendingIntent.getActivity(
            this, 3, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val stop = PendingIntent.getService(
            this, 4, Intent(this, FlexStorageService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val b = if (Build.VERSION.SDK_INT >= 26) Notification.Builder(this, CHANNEL)
                else @Suppress("DEPRECATION") Notification.Builder(this)
        return b.setSmallIcon(R.drawable.ic_flexcloud)
            .setContentTitle("Flex Cloud en este teléfono")
            .setContentText(line)
            .setStyle(Notification.BigTextStyle().bigText(
                line + "\nSolo Flex OS emparejado puede usar el espacio que le has dado, y solo desde tu Wi-Fi."))
            .setContentIntent(open)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .addAction(Notification.Action.Builder(null, "Detener", stop).build())
            .build()
    }

    private fun startForegroundHonestly(): Boolean = try {
        val n = buildNotification()
        if (Build.VERSION.SDK_INT >= 29) startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        else startForeground(NOTIF_ID, n)
        true
    } catch (e: Exception) {
        // Android puede negarlo (sin permiso, o arrancado desde segundo plano):
        // se dice en la app en vez de quedarse a medias.
        Log.w(TAG, "no se pudo pasar a primer plano: ${e.javaClass.simpleName}")
        FlexCloudPhone.stopServer(this)
        false
    }

    private fun updateNotification() {
        try { getSystemService(NotificationManager::class.java)?.notify(NOTIF_ID, buildNotification()) } catch (e: Exception) { }
    }

    private fun shutdown() {
        alive = false
        scope.coroutineContext[kotlinx.coroutines.Job]?.cancel()
        unwatchNetwork()
        FlexCloudPhone.stopServer(this)
        releaseLocks()
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private fun unwatchNetwork() {
        val cb = netCb ?: return
        netCb = null
        try { getSystemService(ConnectivityManager::class.java)?.unregisterNetworkCallback(cb) } catch (e: Exception) { }
    }

    override fun onDestroy() {
        // Aunque Android nos mate: el servidor se para y los cerrojos se sueltan.
        alive = false
        scope.cancel()
        unwatchNetwork()
        FlexCloudPhone.stopServer(this)
        releaseLocks()
        super.onDestroy()
    }
}
