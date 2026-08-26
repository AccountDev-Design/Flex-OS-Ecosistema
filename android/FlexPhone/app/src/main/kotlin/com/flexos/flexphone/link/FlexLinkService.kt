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
import com.flexos.flexphone.domain.FlexPhoneState
import com.flexos.flexphone.domain.LinkState
import com.flexos.flexphone.media.MediaBridge
import com.flexos.flexphone.notifications.ReplyManager
import com.flexos.flexphone.protocol.*
import kotlinx.coroutines.*

/**
 * Servicio del enlace.
 *
 * CUANDO CORRE Y CUANDO NO
 * ------------------------
 * Solo mientras el usuario ha activado el enlace. No arranca al
 * encender el telefono salvo que se marque la opcion, no se
 * "auto-revive" y se puede parar desde su propia notificacion.
 *
 * LA NOTIFICACION PERSISTENTE DICE LA VERDAD: mientras no hay sesion
 * pone "Buscando Flex OS", no "Conectado". Un servicio en primer
 * plano que miente sobre su estado es la razon por la que la gente
 * desinstala este tipo de apps.
 */
class FlexLinkService : Service() {

    companion object {
        private const val CHANNEL = "flexlink"
        private const val NOTIF_ID = 1001
        const val ACTION_START = "com.flexos.flexphone.START_LINK"
        const val ACTION_STOP = "com.flexos.flexphone.STOP_LINK"

        fun start(ctx: Context) {
            val i = Intent(ctx, FlexLinkService::class.java).setAction(ACTION_START)
            if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i) else ctx.startService(i)
        }
        fun stop(ctx: Context) {
            ctx.startService(Intent(ctx, FlexLinkService::class.java).setAction(ACTION_STOP))
        }
    }

    private var gatt: GattServer? = null
    private var media: MediaBridge? = null
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private lateinit var state: FlexPhoneState

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        state = FlexPhoneState.instance ?: FlexPhoneState().also { FlexPhoneState.instance = it }
        createChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> { shutdown(); return START_NOT_STICKY }
        }
        startForegroundHonestly()

        if (gatt == null) {
            val g = GattServer(this, state) { type, payload -> onMessage(type, payload) }
            gatt = g
            if (!g.start()) {
                // No se puede: se dice y se para. Un servicio en primer
                // plano sin enlace posible solo gasta bateria.
                updateNotification()
                stopSelf()
                return START_NOT_STICKY
            }
            state.sender = { type, payload -> g.sendMessage(type, payload) }
            media = MediaBridge(this, state).also { it.start() }
            loop()
        }
        // NOT_STICKY a proposito: si Android mata el servicio, no se
        // resucita a espaldas del usuario.
        return START_NOT_STICKY
    }

    /** Bucle de mantenimiento. Sin sondeo agresivo: un paso por segundo. */
    private fun loop() = scope.launch {
        var lastState = state.link.value
        while (isActive) {
            val now = System.currentTimeMillis()
            gatt?.tick(now)
            if (state.link.value != lastState) { lastState = state.link.value; updateNotification() }
            delay(1_000)
        }
    }

    private fun onMessage(type: Int, payload: ByteArray) {
        when (type) {
            FlexLink.T_HELLO -> gatt?.sendMessage(FlexLink.T_WELCOME, ByteArray(0))
            FlexLink.T_PING -> gatt?.sendMessage(FlexLink.T_PONG, ByteArray(0))
            FlexLink.T_PAIR_CONFIRM -> {
                state.setLink(LinkState.READY)
                pushPhoneState()
                updateNotification()
            }
            FlexLink.T_BYE -> state.setLink(LinkState.ADVERTISING)

            FlexLink.T_REPLY_REQ -> {
                val req = ReplyRequest.decode(payload)
                if (req == null) { state.countBadFrame(); return }
                // El resultado que se devuelve es el REAL: si Android
                // no acepto la accion, se manda el error, no un exito.
                val res = ReplyManager.reply(this, req.notifId, req.action, req.text)
                state.countReply(res.ok)
                gatt?.sendMessage(FlexLink.T_REPLY_RESULT, res.encode())
            }
            FlexLink.T_ACTION_REQ -> {
                val r = PayloadReader(payload)
                val id = r.u32(); val idx = r.u8()
                if (!r.ok) { state.countBadFrame(); return }
                val res = ReplyManager.action(this, id, idx)
                gatt?.sendMessage(FlexLink.T_ACTION_RESULT, res.encode())
            }

            FlexLink.T_MEDIA_CMD -> {
                val r = PayloadReader(payload)
                val cmd = r.u8()
                if (r.ok) media?.command(cmd)
            }
            FlexLink.T_FIND_START -> FindMyPhone.start(this)
            FlexLink.T_FIND_STOP -> FindMyPhone.stop()

            FlexLink.T_RELAY_START -> {
                com.flexos.flexphone.relay.BrowserRelayService.start(this)
            }
            FlexLink.T_RELAY_STOP -> {
                com.flexos.flexphone.relay.BrowserRelayService.stop(this)
            }
            FlexLink.T_TIME_SYNC -> Unit   // el telefono ya tiene la hora del sistema
        }
    }

    private fun pushPhoneState() {
        val bm = getSystemService(Context.BATTERY_SERVICE) as? android.os.BatteryManager
        // Si el sistema no da el nivel, se manda -1 y el codec lo
        // convierte en 255 = DESCONOCIDO. Nunca un 0 que el P4
        // pintaria como bateria vacia.
        val level = bm?.getIntProperty(android.os.BatteryManager.BATTERY_PROPERTY_CAPACITY) ?: -1
        val charging = bm?.isCharging ?: false
        val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as? android.net.ConnectivityManager
        val caps = cm?.getNetworkCapabilities(cm.activeNetwork)
        val net = when {
            caps == null -> NetKind.NONE
            caps.hasTransport(android.net.NetworkCapabilities.TRANSPORT_WIFI) -> NetKind.WIFI
            caps.hasTransport(android.net.NetworkCapabilities.TRANSPORT_CELLULAR) -> NetKind.MOBILE
            else -> NetKind.UNKNOWN
        }
        gatt?.sendMessage(
            FlexLink.T_PHONE_STATE,
            PhoneState(Build.MODEL ?: "Android", level, charging, net).encode(),
        )
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
        // Se libera TODO: sin locks colgando, sin GATT abierto y sin
        // callbacks vivos.
        scope.coroutineContext.cancelChildren()
        state.sender = null
        media?.stop(); media = null
        gatt?.stop(); gatt = null
        FindMyPhone.stop()
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    override fun onDestroy() {
        scope.cancel()
        state.sender = null
        media?.stop()
        gatt?.stop()
        FindMyPhone.stop()
        super.onDestroy()
    }
}
